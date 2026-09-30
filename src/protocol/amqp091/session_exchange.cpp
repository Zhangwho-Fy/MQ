#include "mq/protocol/amqp091/connection_session.hpp"


namespace mq::amqp091 {

SessionResult ConnectionSession::handleExchangeMethod(
    uint16_t channel, const MethodHeader& header) {
    switch (static_cast<ExchangeMethodId>(header.method_id)) {
        case ExchangeMethodId::Declare:
            return handleExchangeDeclare(channel, header.arguments);
        case ExchangeMethodId::Delete:
            return handleExchangeDelete(channel, header.arguments);
        // Server-to-client only.
        case ExchangeMethodId::DeclareOk:
        case ExchangeMethodId::DeleteOk:
            break;
    }

    return sendChannelError(channel, 540, kExchangeClassId,
                            static_cast<uint16_t>(header.method_id),
                            "exchange method not implemented");
}

SessionResult ConnectionSession::handleExchangeDeclare(
    uint16_t channel, std::string_view arguments) {
    ExchangeDeclare declare;
    std::string error;
    if (!decodeExchangeDeclare(arguments, declare, error)) {
        return sendChannelError(channel, 502, kExchangeClassId,
                                static_cast<uint16_t>(ExchangeMethodId::Declare),
                                "invalid exchange.declare");
    }

    if (declare.passive) {
        // Spec: "When set, all other method fields except name and no-wait are
        // ignored" — passive is a pure existence check, it never declares.
        if (!virtual_host_->hasExchange(declare.exchange)) {
            return sendChannelError(channel, 404, kExchangeClassId,
                                    static_cast<uint16_t>(ExchangeMethodId::Declare),
                                    "exchange not found");
        }
    } else {
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
                                    static_cast<uint16_t>(ExchangeMethodId::Declare),
                                    result.error);
        }
    }

    if (!declare.no_wait) {
        return sendMethodOnChannel(
            channel, kExchangeClassId,
            static_cast<uint16_t>(ExchangeMethodId::DeclareOk), "");
    }

    return SessionResult{};
}

SessionResult ConnectionSession::handleExchangeDelete(
    uint16_t channel, std::string_view arguments) {
    ExchangeDelete delete_exchange;
    std::string error;
    if (!decodeExchangeDelete(arguments, delete_exchange, error)) {
        return sendChannelError(channel, 502, kExchangeClassId,
                                static_cast<uint16_t>(ExchangeMethodId::Delete),
                                "invalid exchange.delete");
    }

    const broker::BrokerResult result = virtual_host_->deleteExchange(
        delete_exchange.exchange, delete_exchange.if_unused);
    if (!result.ok) {
        return sendChannelError(channel, result.reply_code,
                                kExchangeClassId,
                                static_cast<uint16_t>(ExchangeMethodId::Delete),
                                result.error);
    }

    if (!delete_exchange.no_wait) {
        return sendMethodOnChannel(
            channel, kExchangeClassId,
            static_cast<uint16_t>(ExchangeMethodId::DeleteOk), "");
    }

    return SessionResult{};
}

}  // namespace mq::amqp091
