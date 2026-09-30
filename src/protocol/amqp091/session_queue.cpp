#include "mq/protocol/amqp091/connection_session.hpp"


namespace mq::amqp091 {

SessionResult ConnectionSession::handleQueueMethod(
    uint16_t channel, const MethodHeader& header) {
    switch (static_cast<QueueMethodId>(header.method_id)) {
        case QueueMethodId::Declare:
            return handleQueueDeclare(channel, header.arguments);
        case QueueMethodId::Bind:
            return handleQueueBind(channel, header.arguments);
        case QueueMethodId::Purge:
            return handleQueuePurge(channel, header.arguments);
        case QueueMethodId::Delete:
            return handleQueueDelete(channel, header.arguments);
        case QueueMethodId::Unbind:
            return handleQueueUnbind(channel, header.arguments);
        // Server-to-client only.
        case QueueMethodId::DeclareOk:
        case QueueMethodId::BindOk:
        case QueueMethodId::PurgeOk:
        case QueueMethodId::DeleteOk:
        case QueueMethodId::UnbindOk:
            break;
    }

    return sendChannelError(channel, 540, kQueueClassId,
                            static_cast<uint16_t>(header.method_id),
                            "queue method not implemented");
}

SessionResult ConnectionSession::handleQueueDeclare(
    uint16_t channel, std::string_view arguments) {
    QueueDeclare declare;
    std::string error;
    if (!decodeQueueDeclare(arguments, declare, error)) {
        return sendChannelError(channel, 502, kQueueClassId,
                                static_cast<uint16_t>(QueueMethodId::Declare),
                                "invalid queue.declare");
    }

    if (declare.passive) {
        // Spec: with passive set every other field except name and no-wait is
        // ignored, so this is only an existence check. The reply still carries
        // the queue's real message and consumer counts.
        if (!virtual_host_->hasQueue(declare.queue)) {
            return sendChannelError(channel, 404, kQueueClassId,
                                    static_cast<uint16_t>(QueueMethodId::Declare),
                                    "queue not found");
        }

        if (declare.no_wait) return SessionResult{};

        QueueDeclareOk ok;
        ok.queue = declare.queue;
        ok.message_count = virtual_host_->messageCount(declare.queue);
        ok.consumer_count = static_cast<uint32_t>(
            virtual_host_->consumerCount(declare.queue));
        return sendMethodOnChannel(
            channel, kQueueClassId,
            static_cast<uint16_t>(QueueMethodId::DeclareOk),
            encodeQueueDeclareOk(ok));
    }

    if (declare.queue.empty()) {
        declare.queue =
            "amq.gen-" + std::to_string(++generated_queue_seq_);
    }

    broker::QueueSpec spec;
    spec.name = declare.queue;
    spec.durable = declare.durable;
    spec.exclusive = declare.exclusive;
    spec.auto_delete = declare.auto_delete;
    if (const std::string* dlx =
            declare.arguments.findString("x-dead-letter-exchange");
        dlx != nullptr) {
        spec.dead_letter_exchange = *dlx;
    }
    if (const std::string* dlx_rk =
            declare.arguments.findString("x-dead-letter-routing-key");
        dlx_rk != nullptr) {
        spec.dead_letter_routing_key = *dlx_rk;
    }
    int64_t message_ttl_ms = 0;
    if (declare.arguments.findInt64("x-message-ttl", message_ttl_ms) &&
        message_ttl_ms > 0) {
        spec.message_ttl_ms = message_ttl_ms;
    }

    const broker::BrokerResult result =
        virtual_host_->declareQueue(spec, this);
    if (!result.ok) {
        return sendChannelError(channel, result.reply_code, kQueueClassId,
                                static_cast<uint16_t>(QueueMethodId::Declare),
                                result.error);
    }

    if (!declare.no_wait) {
        QueueDeclareOk ok;
        ok.queue = declare.queue;
        ok.message_count = virtual_host_->messageCount(declare.queue);
        ok.consumer_count = static_cast<uint32_t>(
            virtual_host_->consumerCount(declare.queue));
        return sendMethodOnChannel(
            channel, kQueueClassId,
            static_cast<uint16_t>(QueueMethodId::DeclareOk),
            encodeQueueDeclareOk(ok));
    }

    return SessionResult{};
}

SessionResult ConnectionSession::handleQueueBind(
    uint16_t channel, std::string_view arguments) {
    QueueBind bind;
    std::string error;
    if (!decodeQueueBind(arguments, bind, error)) {
        return sendChannelError(channel, 502, kQueueClassId,
                                static_cast<uint16_t>(QueueMethodId::Bind),
                                "invalid queue.bind");
    }

    const broker::BrokerResult result =
        virtual_host_->bind(bind.exchange, bind.queue, bind.routing_key);
    if (!result.ok) {
        return sendChannelError(channel, result.reply_code, kQueueClassId,
                                static_cast<uint16_t>(QueueMethodId::Bind),
                                result.error);
    }

    if (!bind.no_wait) {
        return sendMethodOnChannel(
            channel, kQueueClassId,
            static_cast<uint16_t>(QueueMethodId::BindOk), "");
    }

    return SessionResult{};
}

SessionResult ConnectionSession::handleQueueUnbind(
    uint16_t channel, std::string_view arguments) {
    QueueUnbind unbind;
    std::string error;
    if (!decodeQueueUnbind(arguments, unbind, error)) {
        return sendChannelError(channel, 502, kQueueClassId,
                                static_cast<uint16_t>(QueueMethodId::Unbind),
                                "invalid queue.unbind");
    }

    const broker::BrokerResult result = virtual_host_->unbind(
        unbind.exchange, unbind.queue, unbind.routing_key);
    if (!result.ok) {
        return sendChannelError(channel, result.reply_code, kQueueClassId,
                                static_cast<uint16_t>(QueueMethodId::Unbind),
                                result.error);
    }

    return sendMethodOnChannel(
        channel, kQueueClassId,
        static_cast<uint16_t>(QueueMethodId::UnbindOk), "");
}

SessionResult ConnectionSession::handleQueuePurge(
    uint16_t channel, std::string_view arguments) {
    QueuePurge purge;
    std::string error;
    if (!decodeQueuePurge(arguments, purge, error)) {
        return sendChannelError(channel, 502, kQueueClassId,
                                static_cast<uint16_t>(QueueMethodId::Purge),
                                "invalid queue.purge");
    }

    const broker::BrokerResult result =
        virtual_host_->purgeQueue(purge.queue);
    if (!result.ok) {
        return sendChannelError(channel, result.reply_code, kQueueClassId,
                                static_cast<uint16_t>(QueueMethodId::Purge),
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

SessionResult ConnectionSession::handleQueueDelete(
    uint16_t channel, std::string_view arguments) {
    QueueDelete delete_queue;
    std::string error;
    if (!decodeQueueDelete(arguments, delete_queue, error)) {
        return sendChannelError(channel, 502, kQueueClassId,
                                static_cast<uint16_t>(QueueMethodId::Delete),
                                "invalid queue.delete");
    }

    const broker::BrokerResult result = virtual_host_->deleteQueue(
        delete_queue.queue, delete_queue.if_unused, delete_queue.if_empty);
    if (!result.ok) {
        return sendChannelError(channel, result.reply_code, kQueueClassId,
                                static_cast<uint16_t>(QueueMethodId::Delete),
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

}  // namespace mq::amqp091
