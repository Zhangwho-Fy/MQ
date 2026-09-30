#include "mq/protocol/amqp091/connection_session.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <vector>

namespace mq::amqp091 {

ConnectionSession::ConnectionSession(const ConnectionConfig& config,
                                     SendCallback send,
                                     std::shared_ptr<broker::VirtualHost>
                                         virtual_host,
                                     AuthCallback auth_callback,
                                     VhostResolver vhost_resolver)
    : config_(config),
      send_(std::move(send)),
      decoder_(config.frame_max),
      channel_max_(config.channel_max),
      frame_max_(config.frame_max),
      heartbeat_(config.heartbeat),
      auth_callback_(std::move(auth_callback)),
      vhost_resolver_(std::move(vhost_resolver)),
      virtual_host_(std::move(virtual_host)) {}

ConnectionSession::~ConnectionSession() {
    if (virtual_host_) virtual_host_->disconnectOwner(this);
}

SessionResult ConnectionSession::feed(std::string_view bytes) {
    if (state_ == ConnectionState::kClosed) {
        return SessionResult{false, "connection already closed"};
    }

    if (state_ == ConnectionState::kAwaitProtocolHeader) {
        header_buffer_.append(bytes.data(), bytes.size());
        if (header_buffer_.size() < kAmqp091ProtocolHeader.size()) {
            return SessionResult{};
        }

        if (std::memcmp(header_buffer_.data(), kAmqp091ProtocolHeader.data(),
                        kAmqp091ProtocolHeader.size()) != 0) {
            return fail("invalid AMQP protocol header", 501);
        }

        bytes = std::string_view(header_buffer_).substr(
            kAmqp091ProtocolHeader.size());
        header_buffer_.clear();

        ConnectionStart start;
        start.version_major = 0;
        start.version_minor = 9;
        start.server_properties.addString("product", "FyMQ");
        start.server_properties.addString("version", "0.1.0");
        FieldTable capabilities;
        capabilities.addBool("publisher_confirms", true);
        capabilities.addBool("consumer_cancel_notify", true);
        capabilities.addBool("basic.nack", true);
        start.server_properties.addTable(
            "capabilities", encodeFieldTable(capabilities));
        start.mechanisms = config_.mechanisms;
        start.locales = config_.locales;
        const SessionResult sent = sendMethod(
            kConnectionClassId,
            static_cast<uint16_t>(ConnectionMethodId::Start),
            encodeConnectionStart(start));
        if (!sent.ok) return sent;
        state_ = ConnectionState::kWaitStartOk;
        if (bytes.empty()) return SessionResult{};
    }

    return processFrames(bytes);
}

SessionResult ConnectionSession::processFrames(std::string_view bytes) {
    std::vector<Frame> frames;
    DecodeResult decoded = decoder_.feed(bytes, frames);
    if (decoded.status == DecodeStatus::kError) {
        return fail(decoded.error, 501);
    }

    for (const Frame& frame : frames) {
        if (frame.type == kFrameHeartbeat) {
            continue;
        }

        if (frame.type == kFrameMethod) {
            const SessionResult result =
                handleMethod(frame.channel, frame.payload);
            if (!result.ok) return result;
            continue;
        }

        const SessionResult result =
            handleContentFrame(frame.channel, frame);
        if (!result.ok) return result;
    }

    return SessionResult{};
}

SessionResult ConnectionSession::handleMethod(uint16_t channel,
                                              std::string_view payload) {
    MethodHeader header;
    std::string decode_error;
    if (!decodeMethodHeader(payload, header, decode_error)) {
        return fail(decode_error, 501);
    }

    if (header.class_id == kConnectionClassId) {
        if (channel != 0) {
            return fail("connection method received on non-zero channel", 504);
        }

        return handleConnectionMethod(header);
    }

    if (state_ != ConnectionState::kReady) {
        return fail("non-connection method before connection is ready", 503);
    }

    if (header.class_id == kChannelClassId) {
        if (channel == 0) {
            return fail("channel method received on channel 0", 504);
        }

        return handleChannelMethod(channel, header);
    }

    if (channel == 0) {
        return fail("business method received on channel 0", 503);
    }

    if (!isChannelOpen(channel)) {
        return sendChannelError(channel, 504, header.class_id,
                                header.method_id, "channel is not open");
    }

    if (virtual_host_) {
        if (header.class_id == kExchangeClassId) {
            return handleExchangeMethod(channel, header);
        }

        if (header.class_id == kQueueClassId) {
            return handleQueueMethod(channel, header);
        }

        if (header.class_id == kBasicClassId) {
            return handleBasicMethod(channel, header);
        }

        if (header.class_id == kConfirmClassId) {
            return handleConfirmMethod(channel, header);
        }
    }

    return sendChannelError(channel, 540, header.class_id, header.method_id,
                            "method not implemented");
}

void ConnectionSession::sendContent(uint16_t channel,
                                    const std::string& header_payload,
                                    const std::string& body) {
    sendFrame(kFrameHeader, channel, header_payload);
    const size_t max_body = frame_max_ > 8 ? frame_max_ - 8 : 0;
    size_t offset = 0;
    while (offset < body.size()) {
        const size_t count = std::min(max_body, body.size() - offset);
        sendFrame(kFrameBody, channel,
                  std::string_view(body).substr(offset, count));
        offset += count;
    }
}

void ConnectionSession::deliverToConsumer(
    const std::string& consumer_tag, const std::string&,
    const broker::Message& message) {
    const auto it = broker_consumers_.find(consumer_tag);
    if (it == broker_consumers_.end()) return;
    const uint16_t channel = it->second.channel;
    const std::string client_tag = it->second.client_tag;
    const auto channel_it = channels_.find(channel);
    if (channel_it == channels_.end()) return;

    BasicDeliver deliver;
    deliver.consumer_tag = client_tag;
    deliver.delivery_tag = ++channel_it->second.delivery_seq;
    deliver.redelivered = message.redelivered;
    deliver.exchange = message.exchange;
    deliver.routing_key = message.routing_key;
    channel_it->second.delivery_tag_to_message[deliver.delivery_tag] =
        message.id;
    sendMethodOnChannel(
        channel, kBasicClassId,
        static_cast<uint16_t>(BasicMethodId::Deliver),
        encodeBasicDeliver(deliver));
    const std::string header_payload =
        message.header_payload.empty()
            ? encodeContentHeader(message.body.size())
            : message.header_payload;
    sendContent(channel, header_payload, message.body);
}

SessionResult ConnectionSession::handleContentFrame(uint16_t channel,
                                                    const Frame& frame) {
    auto it = pending_content_.find(channel);
    if (it == pending_content_.end()) {
        return fail("content frame without a pending publish", 505);
    }

    PendingContent& pending = it->second;

    if (frame.type == kFrameHeader) {
        if (pending.header_received) {
            return fail("duplicate content header frame", 505);
        }

        ContentHeaderInfo info;
        std::string error;
        if (!decodeContentHeader(frame.payload, info, error)) {
            return fail(error, 502);
        }

        pending.header = info;
        pending.header_payload = frame.payload;
        pending.header_received = true;
        return SessionResult{};
    }

    if (frame.type == kFrameBody) {
        if (!pending.header_received) {
            return fail("content body before content header", 505);
        }

        pending.body.append(frame.payload.data(), frame.payload.size());
        if (pending.body.size() > pending.header.body_size) {
            return fail("content body exceeds declared body size", 505);
        }

        if (pending.body.size() == pending.header.body_size) {
            return finishPendingContent(channel, pending);
        }

        return SessionResult{};
    }

    return fail("unexpected frame while receiving content", 505);
}

SessionResult ConnectionSession::finishPendingContent(
    uint16_t channel, PendingContent& pending) {
    broker::Message message;
    message.body = std::move(pending.body);
    message.persistent = pending.header.persistent;
    message.exchange = pending.publish.exchange;
    message.routing_key = pending.publish.routing_key;
    message.publisher_owner = this;
    message.header_payload = pending.header_payload;
    if (!pending.header.expiration.empty()) {
        try {
            const uint64_t expiration_ms =
                std::stoull(pending.header.expiration);
            message.ttl_ms =
                static_cast<uint32_t>(std::min<uint64_t>(
                    expiration_ms, std::numeric_limits<uint32_t>::max()));
        } catch (...) {
            // Invalid expiration text is ignored, matching RabbitMQ.
        }
    }

    auto& channel_state = channels_[channel];
    const bool confirm_mode = channel_state.confirm_mode;
    uint64_t publish_tag = 0;
    if (confirm_mode) {
        publish_tag = ++channel_state.publish_seq;
    }

    size_t delivered = 0;
    broker::BrokerResult result = virtual_host_->publish(
        pending.publish.exchange, pending.publish.routing_key, message,
        &delivered);
    if (!result.ok) {
        if (confirm_mode) {
            BasicNack nack;
            nack.delivery_tag = publish_tag;
            sendMethodOnChannel(
                channel, kBasicClassId,
                static_cast<uint16_t>(BasicMethodId::Nack),
                encodeBasicNack(nack));
        }

        return sendChannelError(channel, result.reply_code, kBasicClassId,
                                static_cast<uint16_t>(
                                    BasicMethodId::Publish),
                                result.error);
    }

    const bool returned = pending.publish.mandatory && delivered == 0;
    const std::string header_payload = std::move(pending.header_payload);
    pending_content_.erase(channel);

    if (returned) {
        BasicReturn ret;
        ret.reply_code = 312;
        ret.reply_text = "NO_ROUTE";
        ret.exchange = pending.publish.exchange;
        ret.routing_key = pending.publish.routing_key;
        sendMethodOnChannel(
            channel, kBasicClassId,
            static_cast<uint16_t>(BasicMethodId::Return),
            encodeBasicReturn(ret));
        sendFrame(kFrameHeader, channel, header_payload);
        const size_t max_body = frame_max_ > 8 ? frame_max_ - 8 : 0;
        size_t offset = 0;
        while (offset < message.body.size()) {
            const size_t count =
                std::min(max_body, message.body.size() - offset);
            sendFrame(kFrameBody, channel,
                      std::string_view(message.body).substr(offset, count));
            offset += count;
        }
    }

    if (confirm_mode) {
        BasicAck confirm_ack;
        confirm_ack.delivery_tag = publish_tag;
        sendMethodOnChannel(
            channel, kBasicClassId,
            static_cast<uint16_t>(BasicMethodId::Ack),
            encodeBasicAck(confirm_ack));
    }

    return SessionResult{};
}

SessionResult ConnectionSession::sendChannelError(
    uint16_t channel, uint16_t reply_code, uint16_t failing_class_id,
    uint16_t failing_method_id, const std::string& text) {
    ChannelClose close;
    close.reply_code = reply_code;
    close.reply_text = text;
    close.class_id = failing_class_id;
    close.method_id = failing_method_id;
    sendFrame(kFrameMethod, channel,
              encodeMethodHeader(kChannelClassId,
                                 static_cast<uint16_t>(ChannelMethodId::Close),
                                 encodeChannelClose(close)));
    ChannelState& channel_state = channels_[channel];
    channel_state.lifecycle = ChannelLifecycle::kClosing;
    channel_state.flow_active = true;
    return SessionResult{};
}

bool ConnectionSession::isChannelOpen(uint16_t channel) const {
    const auto it = channels_.find(channel);
    return it != channels_.end() &&
           it->second.lifecycle == ChannelLifecycle::kOpen;
}

size_t ConnectionSession::openChannelCount() const {
    size_t count = 0;
    for (const auto& entry : channels_) {
        if (entry.second.lifecycle == ChannelLifecycle::kOpen) ++count;
    }

    return count;
}

SessionResult ConnectionSession::sendMethod(uint16_t class_id,
                                            uint16_t method_id,
                                            const std::string& arguments) {
    const std::string payload =
        encodeMethodHeader(class_id, method_id, arguments);
    sendFrame(kFrameMethod, 0, payload);
    return SessionResult{};
}

SessionResult ConnectionSession::sendMethodOnChannel(
    uint16_t channel, uint16_t class_id, uint16_t method_id,
    const std::string& arguments) {
    const std::string payload =
        encodeMethodHeader(class_id, method_id, arguments);
    sendFrame(kFrameMethod, channel, payload);
    return SessionResult{};
}

SessionResult ConnectionSession::fail(const std::string& message,
                                      uint16_t reply_code,
                                      uint16_t failing_class_id,
                                      uint16_t failing_method_id) {
    if (reply_code != 0 && send_ && state_ != ConnectionState::kClosed) {
        ConnectionClose close;
        close.reply_code = reply_code;
        close.reply_text = message;
        close.class_id = failing_class_id;
        close.method_id = failing_method_id;
        const std::string payload = encodeMethodHeader(
            kConnectionClassId,
            static_cast<uint16_t>(ConnectionMethodId::Close),
            encodeConnectionClose(close));
        sendFrame(kFrameMethod, 0, payload);
        state_ = ConnectionState::kClosed;
    }

    return SessionResult{false, message, reply_code, failing_class_id,
                         failing_method_id};
}

void ConnectionSession::sendFrame(uint8_t type, uint16_t channel,
                                  std::string_view payload) {
    Frame frame;
    frame.type = type;
    frame.channel = channel;
    frame.payload.assign(payload.data(), payload.size());
    const std::string encoded = FrameEncoder::encode(frame, frame_max_);
    if (send_) send_(encoded);
}

}  // namespace mq::amqp091
