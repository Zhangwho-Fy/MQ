#include "mq/protocol/amqp091/connection_session.hpp"

#include <algorithm>
#include <cstring>

namespace mq::amqp091 {

namespace {

bool decodePlainResponse(const std::string& response, std::string& user,
                         std::string& password) {
    const size_t first = response.find('\0');
    if (first == std::string::npos) return false;
    const size_t second = response.find('\0', first + 1);
    if (second == std::string::npos) return false;
    user = response.substr(first + 1, second - first - 1);
    password = response.substr(second + 1);
    return true;
}

}  // namespace

ConnectionSession::ConnectionSession(const ConnectionConfig& config,
                                     SendCallback send,
                                     std::shared_ptr<broker::VirtualHost>
                                         virtual_host)
    : config_(config),
      send_(std::move(send)),
      decoder_(config.frame_max),
      channel_max_(config.channel_max),
      frame_max_(config.frame_max),
      heartbeat_(config.heartbeat),
      virtual_host_(std::move(virtual_host)) {}

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
    }
    return sendChannelError(channel, 540, header.class_id, header.method_id,
                            "method not implemented");
}

SessionResult ConnectionSession::handleExchangeMethod(
    uint16_t channel, const MethodHeader& header) {
    const auto method = static_cast<ExchangeMethodId>(header.method_id);

    if (method == ExchangeMethodId::Declare) {
        ExchangeDeclare declare;
        std::string error;
        if (!decodeExchangeDeclare(header.arguments, declare, error)) {
            return sendChannelError(channel, 502, kExchangeClassId,
                                    static_cast<uint16_t>(method),
                                    "invalid exchange.declare");
        }
        if (declare.passive && !virtual_host_->hasExchange(declare.exchange)) {
            return sendChannelError(channel, 404, kExchangeClassId,
                                    static_cast<uint16_t>(method),
                                    "exchange not found");
        }
        broker::ExchangeSpec spec;
        spec.name = declare.exchange;
        spec.type = declare.type;
        spec.durable = declare.durable;
        spec.auto_delete = declare.auto_delete;
        spec.internal = declare.internal;
        const broker::BrokerResult result =
            virtual_host_->declareExchange(spec);
        if (!result.ok) {
            return sendChannelError(channel, result.reply_code,
                                    kExchangeClassId,
                                    static_cast<uint16_t>(method),
                                    result.error);
        }
        if (!declare.no_wait) {
            return sendMethodOnChannel(
                channel, kExchangeClassId,
                static_cast<uint16_t>(ExchangeMethodId::DeclareOk), "");
        }
        return SessionResult{};
    }

    if (method == ExchangeMethodId::Delete) {
        ExchangeDelete delete_exchange;
        std::string error;
        if (!decodeExchangeDelete(header.arguments, delete_exchange, error)) {
            return sendChannelError(channel, 502, kExchangeClassId,
                                    static_cast<uint16_t>(method),
                                    "invalid exchange.delete");
        }
        const broker::BrokerResult result = virtual_host_->deleteExchange(
            delete_exchange.exchange, delete_exchange.if_unused);
        if (!result.ok) {
            return sendChannelError(channel, result.reply_code,
                                    kExchangeClassId,
                                    static_cast<uint16_t>(method),
                                    result.error);
        }
        if (!delete_exchange.no_wait) {
            return sendMethodOnChannel(
                channel, kExchangeClassId,
                static_cast<uint16_t>(ExchangeMethodId::DeleteOk), "");
        }
        return SessionResult{};
    }

    return sendChannelError(channel, 540, kExchangeClassId,
                            static_cast<uint16_t>(method),
                            "exchange method not implemented");
}

SessionResult ConnectionSession::handleQueueMethod(
    uint16_t channel, const MethodHeader& header) {
    const auto method = static_cast<QueueMethodId>(header.method_id);

    if (method == QueueMethodId::Declare) {
        QueueDeclare declare;
        std::string error;
        if (!decodeQueueDeclare(header.arguments, declare, error)) {
            return sendChannelError(channel, 502, kQueueClassId,
                                    static_cast<uint16_t>(method),
                                    "invalid queue.declare");
        }
        if (declare.queue.empty()) {
            declare.queue =
                "amq.gen-" + std::to_string(++generated_queue_seq_);
        }
        if (declare.passive && !virtual_host_->hasQueue(declare.queue)) {
            return sendChannelError(channel, 404, kQueueClassId,
                                    static_cast<uint16_t>(method),
                                    "queue not found");
        }
        broker::QueueSpec spec;
        spec.name = declare.queue;
        spec.durable = declare.durable;
        spec.exclusive = declare.exclusive;
        spec.auto_delete = declare.auto_delete;
        const broker::BrokerResult result =
            virtual_host_->declareQueue(spec);
        if (!result.ok) {
            return sendChannelError(channel, result.reply_code, kQueueClassId,
                                    static_cast<uint16_t>(method),
                                    result.error);
        }
        if (!declare.no_wait) {
            QueueDeclareOk ok;
            ok.queue = declare.queue;
            ok.message_count = virtual_host_->messageCount(declare.queue);
            ok.consumer_count = 0;
            return sendMethodOnChannel(
                channel, kQueueClassId,
                static_cast<uint16_t>(QueueMethodId::DeclareOk),
                encodeQueueDeclareOk(ok));
        }
        return SessionResult{};
    }

    if (method == QueueMethodId::Bind) {
        QueueBind bind;
        std::string error;
        if (!decodeQueueBind(header.arguments, bind, error)) {
            return sendChannelError(channel, 502, kQueueClassId,
                                    static_cast<uint16_t>(method),
                                    "invalid queue.bind");
        }
        const broker::BrokerResult result =
            virtual_host_->bind(bind.exchange, bind.queue, bind.routing_key);
        if (!result.ok) {
            return sendChannelError(channel, result.reply_code, kQueueClassId,
                                    static_cast<uint16_t>(method),
                                    result.error);
        }
        if (!bind.no_wait) {
            return sendMethodOnChannel(
                channel, kQueueClassId,
                static_cast<uint16_t>(QueueMethodId::BindOk), "");
        }
        return SessionResult{};
    }

    if (method == QueueMethodId::Unbind) {
        QueueUnbind unbind;
        std::string error;
        if (!decodeQueueUnbind(header.arguments, unbind, error)) {
            return sendChannelError(channel, 502, kQueueClassId,
                                    static_cast<uint16_t>(method),
                                    "invalid queue.unbind");
        }
        const broker::BrokerResult result = virtual_host_->unbind(
            unbind.exchange, unbind.queue, unbind.routing_key);
        if (!result.ok) {
            return sendChannelError(channel, result.reply_code, kQueueClassId,
                                    static_cast<uint16_t>(method),
                                    result.error);
        }
        return sendMethodOnChannel(
            channel, kQueueClassId,
            static_cast<uint16_t>(QueueMethodId::UnbindOk), "");
    }

    if (method == QueueMethodId::Purge) {
        QueuePurge purge;
        std::string error;
        if (!decodeQueuePurge(header.arguments, purge, error)) {
            return sendChannelError(channel, 502, kQueueClassId,
                                    static_cast<uint16_t>(method),
                                    "invalid queue.purge");
        }
        const broker::BrokerResult result =
            virtual_host_->purgeQueue(purge.queue);
        if (!result.ok) {
            return sendChannelError(channel, result.reply_code, kQueueClassId,
                                    static_cast<uint16_t>(method),
                                    result.error);
        }
        if (!purge.no_wait) {
            QueuePurgeOk ok;
            ok.message_count = result.count;
            return sendMethodOnChannel(
                channel, kQueueClassId,
                static_cast<uint16_t>(QueueMethodId::PurgeOk),
                encodeQueuePurgeOk(ok));
        }
        return SessionResult{};
    }

    if (method == QueueMethodId::Delete) {
        QueueDelete delete_queue;
        std::string error;
        if (!decodeQueueDelete(header.arguments, delete_queue, error)) {
            return sendChannelError(channel, 502, kQueueClassId,
                                    static_cast<uint16_t>(method),
                                    "invalid queue.delete");
        }
        const broker::BrokerResult result = virtual_host_->deleteQueue(
            delete_queue.queue, delete_queue.if_unused, delete_queue.if_empty);
        if (!result.ok) {
            return sendChannelError(channel, result.reply_code, kQueueClassId,
                                    static_cast<uint16_t>(method),
                                    result.error);
        }
        if (!delete_queue.no_wait) {
            QueueDeleteOk ok;
            ok.message_count = result.count;
            return sendMethodOnChannel(
                channel, kQueueClassId,
                static_cast<uint16_t>(QueueMethodId::DeleteOk),
                encodeQueueDeleteOk(ok));
        }
        return SessionResult{};
    }

    return sendChannelError(channel, 540, kQueueClassId,
                            static_cast<uint16_t>(method),
                            "queue method not implemented");
}

SessionResult ConnectionSession::handleBasicMethod(
    uint16_t channel, const MethodHeader& header) {
    const auto method = static_cast<BasicMethodId>(header.method_id);

    if (method == BasicMethodId::Publish) {
        BasicPublish publish;
        std::string error;
        if (!decodeBasicPublish(header.arguments, publish, error)) {
            return sendChannelError(channel, 502, kBasicClassId,
                                    static_cast<uint16_t>(method),
                                    "invalid basic.publish");
        }
        PendingContent pending;
        pending.publish = std::move(publish);
        pending_content_[channel] = std::move(pending);
        return SessionResult{};
    }

    return sendChannelError(channel, 540, kBasicClassId,
                            static_cast<uint16_t>(method),
                            "basic method not implemented");
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

    size_t delivered = 0;
    broker::BrokerResult result = virtual_host_->publish(
        pending.publish.exchange, pending.publish.routing_key, message,
        &delivered);
    if (!result.ok) {
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
    return SessionResult{};
}

SessionResult ConnectionSession::handleChannelMethod(
    uint16_t channel, const MethodHeader& header) {
    const auto method = static_cast<ChannelMethodId>(header.method_id);
    auto it = channels_.find(channel);

    if (method == ChannelMethodId::Open) {
        std::string error;
        if (!decodeChannelOpen(header.arguments, error)) {
            return sendChannelError(channel, 502, kChannelClassId,
                                    static_cast<uint16_t>(method),
                                    "invalid channel.open");
        }
        if (it != channels_.end()) {
            return sendChannelError(channel, 504, kChannelClassId,
                                    static_cast<uint16_t>(method),
                                    "channel already open");
        }
        channels_[channel] = ChannelState{};
        sendFrame(kFrameMethod, channel,
                  encodeMethodHeader(kChannelClassId,
                                     static_cast<uint16_t>(
                                         ChannelMethodId::OpenOk),
                                     encodeChannelOpenOk()));
        return SessionResult{};
    }

    if (it == channels_.end()) {
        return sendChannelError(channel, 504, kChannelClassId,
                                static_cast<uint16_t>(method),
                                "channel is not open");
    }

    if (it->second.lifecycle == ChannelLifecycle::kClosing) {
        if (method == ChannelMethodId::CloseOk) {
            channels_.erase(channel);
        }
        return SessionResult{};
    }

    if (method == ChannelMethodId::Flow) {
        ChannelFlow flow;
        std::string error;
        if (!decodeChannelFlow(header.arguments, flow, error)) {
            return sendChannelError(channel, 502, kChannelClassId,
                                    static_cast<uint16_t>(method),
                                    "invalid channel.flow");
        }
        it->second.flow_active = flow.active;
        sendFrame(kFrameMethod, channel,
                  encodeMethodHeader(kChannelClassId,
                                     static_cast<uint16_t>(
                                         ChannelMethodId::FlowOk),
                                     encodeChannelFlowOk(flow)));
        return SessionResult{};
    }

    if (method == ChannelMethodId::CloseOk) {
        return sendChannelError(channel, 503, kChannelClassId,
                                static_cast<uint16_t>(method),
                                "unexpected channel.close-ok");
    }

    if (method == ChannelMethodId::Close) {
        ChannelClose close;
        std::string error;
        if (!decodeChannelClose(header.arguments, close, error)) {
            return sendChannelError(channel, 502, kChannelClassId,
                                    static_cast<uint16_t>(method),
                                    "invalid channel.close");
        }
        sendFrame(kFrameMethod, channel,
                  encodeMethodHeader(kChannelClassId,
                                     static_cast<uint16_t>(
                                         ChannelMethodId::CloseOk),
                                     encodeChannelCloseOk()));
        channels_.erase(channel);
        return SessionResult{};
    }

    return sendChannelError(channel, 540, kChannelClassId,
                            static_cast<uint16_t>(method),
                            "channel method not implemented");
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
    channels_[channel] = ChannelState{ChannelLifecycle::kClosing, true};
    return SessionResult{};
}

SessionResult ConnectionSession::handleConnectionMethod(
    const MethodHeader& header) {
    const auto method = static_cast<ConnectionMethodId>(header.method_id);

    if (method == ConnectionMethodId::StartOk) {
        if (state_ != ConnectionState::kWaitStartOk) {
            return fail("unexpected connection.start-ok", 503);
        }
        std::string error;
        ConnectionStartOk start_ok;
        if (!decodeConnectionStartOk(header.arguments, start_ok, error)) {
            return fail(error, 502);
        }
        if (start_ok.mechanism != "PLAIN") {
            return fail("unsupported SASL mechanism", 530);
        }
        std::string user;
        std::string password;
        if (!decodePlainResponse(start_ok.response, user, password) ||
            user != config_.username || password != config_.password) {
            return fail("authentication failed", 403);
        }

        ConnectionTune tune;
        tune.channel_max = config_.channel_max;
        tune.frame_max = config_.frame_max;
        tune.heartbeat = config_.heartbeat;
        const SessionResult sent =
            sendMethod(kConnectionClassId,
                       static_cast<uint16_t>(ConnectionMethodId::Tune),
                       encodeConnectionTune(tune));
        if (!sent.ok) return sent;
        state_ = ConnectionState::kWaitTuneOk;
        return SessionResult{};
    }

    if (method == ConnectionMethodId::TuneOk) {
        if (state_ != ConnectionState::kWaitTuneOk) {
            return fail("unexpected connection.tune-ok", 503);
        }
        std::string error;
        ConnectionTune tune_ok;
        if (!decodeConnectionTune(header.arguments, tune_ok, error)) {
            return fail(error, 502);
        }
        channel_max_ =
            tune_ok.channel_max == 0
                ? config_.channel_max
                : std::min(config_.channel_max, tune_ok.channel_max);
        frame_max_ =
            tune_ok.frame_max == 0
                ? config_.frame_max
                : std::min(config_.frame_max, tune_ok.frame_max);
        heartbeat_ =
            (config_.heartbeat == 0 || tune_ok.heartbeat == 0)
                ? 0
                : std::min(config_.heartbeat, tune_ok.heartbeat);
        decoder_.setFrameMax(frame_max_);
        state_ = ConnectionState::kWaitOpen;
        return SessionResult{};
    }

    if (method == ConnectionMethodId::Open) {
        if (state_ != ConnectionState::kWaitOpen) {
            return fail("unexpected connection.open", 503);
        }
        std::string error;
        ConnectionOpen open;
        if (!decodeConnectionOpen(header.arguments, open, error)) {
            return fail(error, 502);
        }
        if (!open.virtual_host.empty() &&
            open.virtual_host != config_.virtual_host) {
            return fail("unknown virtual host", 402);
        }
        const std::string open_ok = encodeMethodHeader(
            kConnectionClassId,
            static_cast<uint16_t>(ConnectionMethodId::OpenOk), "");
        sendFrame(kFrameMethod, 0, open_ok);
        state_ = ConnectionState::kReady;
        return SessionResult{};
    }

    if (method == ConnectionMethodId::Close) {
        std::string error;
        ConnectionClose close;
        if (!decodeConnectionClose(header.arguments, close, error)) {
            return fail(error, 502);
        }
        const std::string close_ok = encodeMethodHeader(
            kConnectionClassId,
            static_cast<uint16_t>(ConnectionMethodId::CloseOk), "");
        sendFrame(kFrameMethod, 0, close_ok);
        state_ = ConnectionState::kClosed;
        return SessionResult{};
    }

    if (method == ConnectionMethodId::CloseOk) {
        if (state_ != ConnectionState::kClosing) {
            return fail("unexpected connection.close-ok", 503);
        }
        state_ = ConnectionState::kClosed;
        return SessionResult{};
    }

    return fail("unsupported or unexpected connection method", 503);
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
