#include "mq/protocol/amqp091/connection_session.hpp"


namespace mq::amqp091 {

SessionResult ConnectionSession::handleConfirmMethod(
    uint16_t channel, const MethodHeader& header) {
    switch (static_cast<ConfirmMethodId>(header.method_id)) {
        case ConfirmMethodId::Select:
            return handleConfirmSelect(channel, header.arguments);
        // Server-to-client only.
        case ConfirmMethodId::SelectOk:
            break;
    }

    return sendChannelError(channel, 540, kConfirmClassId,
                            static_cast<uint16_t>(header.method_id),
                            "confirm method not implemented");
}

SessionResult ConnectionSession::handleConfirmSelect(
    uint16_t channel, std::string_view arguments) {
    ConfirmSelect select;
    std::string error;
    if (!decodeConfirmSelect(arguments, select, error)) {
        return sendChannelError(channel, 502, kConfirmClassId,
                                static_cast<uint16_t>(ConfirmMethodId::Select),
                                "invalid confirm.select");
    }

    channels_[channel].confirm_mode = true;
    if (!select.no_wait) {
        return sendMethodOnChannel(
            channel, kConfirmClassId,
            static_cast<uint16_t>(ConfirmMethodId::SelectOk),
            encodeConfirmSelectOk());
    }

    return SessionResult{};
}

}  // namespace mq::amqp091
