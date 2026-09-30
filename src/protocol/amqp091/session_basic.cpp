#include "mq/protocol/amqp091/connection_session.hpp"

#include <vector>

namespace mq::amqp091 {

SessionResult ConnectionSession::handleBasicMethod(
    uint16_t channel, const MethodHeader& header) {
    switch (static_cast<BasicMethodId>(header.method_id)) {
        case BasicMethodId::Qos:
            return handleBasicQos(channel, header.arguments);
        case BasicMethodId::Consume:
            return handleBasicConsume(channel, header.arguments);
        case BasicMethodId::Cancel:
            return handleBasicCancel(channel, header.arguments);
        case BasicMethodId::Publish:
            return handleBasicPublish(channel, header.arguments);
        case BasicMethodId::Get:
            return handleBasicGet(channel, header.arguments);
        case BasicMethodId::Ack:
            return handleBasicAck(channel, header.arguments);
        case BasicMethodId::Reject:
            return handleBasicReject(channel, header.arguments);
        case BasicMethodId::RecoverAsync:
            return handleBasicRecover(channel, header.arguments, header.method_id);
        case BasicMethodId::Recover:
            return handleBasicRecover(channel, header.arguments, header.method_id);
        // Server-to-client only.
        case BasicMethodId::QosOk:
        case BasicMethodId::ConsumeOk:
        case BasicMethodId::CancelOk:
        case BasicMethodId::Return:
        case BasicMethodId::Deliver:
        case BasicMethodId::GetOk:
        case BasicMethodId::GetEmpty:
        case BasicMethodId::RecoverOk:
        case BasicMethodId::Nack:
            break;
    }

    return sendChannelError(channel, 540, kBasicClassId,
                            static_cast<uint16_t>(header.method_id),
                            "basic method not implemented");
}

SessionResult ConnectionSession::handleBasicPublish(
    uint16_t channel, std::string_view arguments) {
    BasicPublish publish;
    std::string error;
    if (!decodeBasicPublish(arguments, publish, error)) {
        return sendChannelError(channel, 502, kBasicClassId,
                                static_cast<uint16_t>(BasicMethodId::Publish),
                                "invalid basic.publish");
    }

    if (pending_content_.find(channel) != pending_content_.end()) {
        return fail("basic.publish while content frames are pending", 505,
                    kBasicClassId, static_cast<uint16_t>(BasicMethodId::Publish));
    }

    PendingContent pending;
    pending.publish = std::move(publish);
    pending_content_[channel] = std::move(pending);
    return SessionResult{};
}

SessionResult ConnectionSession::handleBasicConsume(
    uint16_t channel, std::string_view arguments) {
    BasicConsume consume;
    std::string error;
    if (!decodeBasicConsume(arguments, consume, error)) {
        return sendChannelError(channel, 502, kBasicClassId,
                                static_cast<uint16_t>(BasicMethodId::Consume),
                                "invalid basic.consume");
    }

    if (!virtual_host_->hasQueue(consume.queue)) {
        return sendChannelError(channel, 404, kBasicClassId,
                                static_cast<uint16_t>(BasicMethodId::Consume),
                                "queue not found");
    }

    const std::string client_tag = consume.consumer_tag.empty()
                                       ? "ctag-" +
                                             std::to_string(
                                                 ++generated_consumer_seq_)
                                       : consume.consumer_tag;
    const std::string broker_tag =
        "b-" + std::to_string(++generated_consumer_seq_);
    const auto lookup_key =
        std::make_pair(channel, client_tag);
    if (client_consumer_lookup_.find(lookup_key) !=
        client_consumer_lookup_.end()) {
        return sendChannelError(channel, 530, kBasicClassId,
                                static_cast<uint16_t>(BasicMethodId::Consume),
                                "consumer tag already in use");
    }

    SessionConsumer consumer;
    consumer.channel = channel;
    consumer.queue = consume.queue;
    consumer.client_tag = client_tag;
    consumer.broker_tag = broker_tag;
    broker_consumers_[broker_tag] = consumer;
    client_consumer_lookup_[lookup_key] = broker_tag;

    if (!consume.no_wait) {
        const SessionResult ok = sendMethodOnChannel(
            channel, kBasicClassId,
            static_cast<uint16_t>(BasicMethodId::ConsumeOk),
            encodeBasicConsumeOk(client_tag));
        if (!ok.ok) return ok;
    }

    const broker::BrokerResult result = virtual_host_->registerConsumer(
        consume.queue, broker_tag, this,
        [this](const std::string& tag, const std::string& queue,
               const broker::Message& message) {
            deliverToConsumer(tag, queue, message);
        },
        consume.no_ack, channels_[channel].prefetch, consume.no_local,
        consume.exclusive);
    if (!result.ok) {
        broker_consumers_.erase(broker_tag);
        client_consumer_lookup_.erase(lookup_key);
        return sendChannelError(channel, result.reply_code, kBasicClassId,
                                static_cast<uint16_t>(BasicMethodId::Consume),
                                result.error);
    }

    return SessionResult{};
}

SessionResult ConnectionSession::handleBasicQos(
    uint16_t channel, std::string_view arguments) {
    BasicQos qos;
    std::string error;
    if (!decodeBasicQos(arguments, qos, error)) {
        return sendChannelError(channel, 502, kBasicClassId,
                                static_cast<uint16_t>(BasicMethodId::Qos),
                                "invalid basic.qos");
    }

    channels_[channel].prefetch = qos.prefetch_count;
    return sendMethodOnChannel(
        channel, kBasicClassId,
        static_cast<uint16_t>(BasicMethodId::QosOk), "");
}

SessionResult ConnectionSession::handleBasicCancel(
    uint16_t channel, std::string_view arguments) {
    BasicCancel cancel;
    std::string error;
    if (!decodeBasicCancel(arguments, cancel, error)) {
        return sendChannelError(channel, 502, kBasicClassId,
                                static_cast<uint16_t>(BasicMethodId::Cancel),
                                "invalid basic.cancel");
    }

    const auto lookup_key =
        std::make_pair(channel, cancel.consumer_tag);
    const auto lookup_it = client_consumer_lookup_.find(lookup_key);
    if (lookup_it == client_consumer_lookup_.end()) {
        return sendChannelError(channel, 530, kBasicClassId,
                                static_cast<uint16_t>(BasicMethodId::Cancel),
                                "consumer not found");
    }

    const std::string broker_tag = lookup_it->second;
    const auto broker_it = broker_consumers_.find(broker_tag);
    if (broker_it == broker_consumers_.end()) {
        client_consumer_lookup_.erase(lookup_it);
        return sendChannelError(channel, 530, kBasicClassId,
                                static_cast<uint16_t>(BasicMethodId::Cancel),
                                "consumer not found");
    }

    const std::string queue = broker_it->second.queue;
    virtual_host_->unregisterConsumer(queue, broker_tag, this);
    broker_consumers_.erase(broker_it);
    client_consumer_lookup_.erase(lookup_it);
    if (!cancel.no_wait) {
        return sendMethodOnChannel(
            channel, kBasicClassId,
            static_cast<uint16_t>(BasicMethodId::CancelOk),
            encodeBasicCancelOk(cancel.consumer_tag));
    }

    return SessionResult{};
}

SessionResult ConnectionSession::handleBasicAck(
    uint16_t channel, std::string_view arguments) {
    BasicAck ack;
    std::string error;
    if (!decodeBasicAck(arguments, ack, error)) {
        return sendChannelError(channel, 502, kBasicClassId,
                                static_cast<uint16_t>(BasicMethodId::Ack),
                                "invalid basic.ack");
    }

    auto& channel_map = channels_[channel].delivery_tag_to_message;
    if (ack.multiple) {
        std::vector<uint64_t> message_ids;
        for (auto it = channel_map.begin(); it != channel_map.end();) {
            if (it->first <= ack.delivery_tag) {
                message_ids.push_back(it->second);
                it = channel_map.erase(it);
            } else {
                ++it;
            }
        }

        for (const uint64_t message_id : message_ids) {
            const broker::BrokerResult result =
                virtual_host_->ackMessage(message_id);
            if (!result.ok) {
                return sendChannelError(channel, result.reply_code,
                                        kBasicClassId,
                                        static_cast<uint16_t>(BasicMethodId::Ack),
                                        result.error);
            }
        }

        return SessionResult{};
    }

    const auto it = channel_map.find(ack.delivery_tag);
    if (it == channel_map.end()) {
        return sendChannelError(channel, 406, kBasicClassId,
                                static_cast<uint16_t>(BasicMethodId::Ack),
                                "unknown delivery tag");
    }

    const uint64_t message_id = it->second;
    channel_map.erase(it);
    const broker::BrokerResult result =
        virtual_host_->ackMessage(message_id);
    if (!result.ok) {
        return sendChannelError(channel, result.reply_code, kBasicClassId,
                                static_cast<uint16_t>(BasicMethodId::Ack),
                                result.error);
    }

    return SessionResult{};
}

SessionResult ConnectionSession::handleBasicReject(
    uint16_t channel, std::string_view arguments) {
    BasicReject reject;
    std::string error;
    if (!decodeBasicReject(arguments, reject, error)) {
        return sendChannelError(channel, 502, kBasicClassId,
                                static_cast<uint16_t>(BasicMethodId::Reject),
                                "invalid basic.reject");
    }

    auto& channel_map = channels_[channel].delivery_tag_to_message;
    const auto it = channel_map.find(reject.delivery_tag);
    if (it == channel_map.end()) {
        return sendChannelError(channel, 406, kBasicClassId,
                                static_cast<uint16_t>(BasicMethodId::Reject),
                                "unknown delivery tag");
    }

    const broker::BrokerResult result = virtual_host_->rejectMessage(
        it->second, reject.requeue);
    if (!result.ok) {
        return sendChannelError(channel, result.reply_code, kBasicClassId,
                                static_cast<uint16_t>(BasicMethodId::Reject),
                                result.error);
    }

    channel_map.erase(it);
    return SessionResult{};
}

SessionResult ConnectionSession::handleBasicRecover(
    uint16_t channel, std::string_view arguments,
    uint16_t method_id) {
    BasicRecover recover;
    std::string error;
    if (!decodeBasicRecover(arguments, recover, error)) {
        return sendChannelError(channel, 502, kBasicClassId,
                                method_id,
                                "invalid basic.recover");
    }

    virtual_host_->requeueUnacked(this);
    if (method_id == static_cast<uint16_t>(BasicMethodId::Recover)) {
        return sendMethodOnChannel(
            channel, kBasicClassId,
            static_cast<uint16_t>(BasicMethodId::RecoverOk), "");
    }

    return SessionResult{};
}

SessionResult ConnectionSession::handleBasicGet(
    uint16_t channel, std::string_view arguments) {
    BasicGet get;
    std::string error;
    if (!decodeBasicGet(arguments, get, error)) {
        return sendChannelError(channel, 502, kBasicClassId,
                                static_cast<uint16_t>(BasicMethodId::Get),
                                "invalid basic.get");
    }

    if (!virtual_host_->hasQueue(get.queue)) {
        return sendChannelError(channel, 404, kBasicClassId,
                                static_cast<uint16_t>(BasicMethodId::Get),
                                "queue not found");
    }

    broker::Message message;
    bool has_message = false;
    uint32_t remaining = 0;
    const broker::BrokerResult result = virtual_host_->getMessage(
        get.queue, get.no_ack, this, &message, &has_message, &remaining);
    if (!result.ok) {
        return sendChannelError(channel, result.reply_code, kBasicClassId,
                                static_cast<uint16_t>(BasicMethodId::Get),
                                result.error);
    }

    if (!has_message) {
        return sendMethodOnChannel(
            channel, kBasicClassId,
            static_cast<uint16_t>(BasicMethodId::GetEmpty), "");
    }

    auto& channel_state = channels_[channel];
    const uint64_t delivery_tag = ++channel_state.delivery_seq;
    channel_state.delivery_tag_to_message[delivery_tag] = message.id;
    BasicGetOk ok;
    ok.delivery_tag = delivery_tag;
    ok.redelivered = message.redelivered;
    ok.exchange = message.exchange;
    ok.routing_key = message.routing_key;
    ok.message_count = remaining;
    const SessionResult sent = sendMethodOnChannel(
        channel, kBasicClassId,
        static_cast<uint16_t>(BasicMethodId::GetOk),
        encodeBasicGetOk(ok));
    if (!sent.ok) return sent;
    const std::string header_payload =
        message.header_payload.empty()
            ? encodeContentHeader(message.body.size())
            : message.header_payload;
    sendContent(channel, header_payload, message.body);
    return SessionResult{};
}

}  // namespace mq::amqp091
