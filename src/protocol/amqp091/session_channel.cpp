#include "mq/protocol/amqp091/connection_session.hpp"


namespace mq::amqp091 {

SessionResult ConnectionSession::handleChannelMethod(
    uint16_t channel, const MethodHeader& header) {
    const auto method = static_cast<ChannelMethodId>(header.method_id);

    // channel.open is the only method that may arrive before the channel exists.
    if (method == ChannelMethodId::Open) {
        return handleChannelOpen(channel, header.arguments);
    }

    const auto it = channels_.find(channel);
    if (it == channels_.end()) {
        return sendChannelError(channel, 504, kChannelClassId,
                                static_cast<uint16_t>(method),
                                "channel is not open");
    }

    // While a channel is closing, only close-ok still means anything.
    if (it->second.lifecycle == ChannelLifecycle::kClosing) {
        if (method == ChannelMethodId::CloseOk) {
            closeChannel(channel);
        }

        return SessionResult{};
    }

    switch (method) {
        case ChannelMethodId::Flow:
            return handleChannelFlow(channel, header.arguments);
        case ChannelMethodId::Close:
            return handleChannelClose(channel, header.arguments);
        case ChannelMethodId::CloseOk:
            return sendChannelError(channel, 503, kChannelClassId,
                                    static_cast<uint16_t>(method),
                                    "unexpected channel.close-ok");
        // Server-to-client only, and open is handled above.
        case ChannelMethodId::Open:
        case ChannelMethodId::OpenOk:
        case ChannelMethodId::FlowOk:
            break;
    }

    return sendChannelError(channel, 540, kChannelClassId,
                            static_cast<uint16_t>(method),
                            "channel method not implemented");
}

SessionResult ConnectionSession::handleChannelOpen(uint16_t channel,
                                                   std::string_view arguments) {
    std::string error;
    if (!decodeChannelOpen(arguments, error)) {
        return sendChannelError(channel, 502, kChannelClassId,
                                static_cast<uint16_t>(ChannelMethodId::Open),
                                "invalid channel.open");
    }

    if (channels_.find(channel) != channels_.end()) {
        return sendChannelError(channel, 504, kChannelClassId,
                                static_cast<uint16_t>(ChannelMethodId::Open),
                                "channel already open");
    }

    if (channel_max_ != 0 && channel > channel_max_) {
        return sendChannelError(channel, 504, kChannelClassId,
                                static_cast<uint16_t>(ChannelMethodId::Open),
                                "channel number exceeds channel-max");
    }

    channels_[channel] = ChannelState{};
    sendFrame(kFrameMethod, channel,
              encodeMethodHeader(kChannelClassId,
                                 static_cast<uint16_t>(ChannelMethodId::OpenOk),
                                 encodeChannelOpenOk()));
    return SessionResult{};
}

SessionResult ConnectionSession::handleChannelFlow(uint16_t channel,
                                                   std::string_view arguments) {
    ChannelFlow flow;
    std::string error;
    if (!decodeChannelFlow(arguments, flow, error)) {
        return sendChannelError(channel, 502, kChannelClassId,
                                static_cast<uint16_t>(ChannelMethodId::Flow),
                                "invalid channel.flow");
    }

    channels_[channel].flow_active = flow.active;
    sendFrame(kFrameMethod, channel,
              encodeMethodHeader(kChannelClassId,
                                 static_cast<uint16_t>(ChannelMethodId::FlowOk),
                                 encodeChannelFlowOk(flow)));
    return SessionResult{};
}

SessionResult ConnectionSession::handleChannelClose(uint16_t channel,
                                                    std::string_view arguments) {
    ChannelClose close;
    std::string error;
    if (!decodeChannelClose(arguments, close, error)) {
        return sendChannelError(channel, 502, kChannelClassId,
                                static_cast<uint16_t>(ChannelMethodId::Close),
                                "invalid channel.close");
    }

    sendFrame(kFrameMethod, channel,
              encodeMethodHeader(kChannelClassId,
                                 static_cast<uint16_t>(ChannelMethodId::CloseOk),
                                 encodeChannelCloseOk()));
    closeChannel(channel);
    return SessionResult{};
}

void ConnectionSession::closeChannel(uint16_t channel) {
    for (auto it = broker_consumers_.begin(); it != broker_consumers_.end();) {
        if (it->second.channel != channel) {
            ++it;
            continue;
        }

        const std::string broker_tag = it->first;
        const std::string queue = it->second.queue;
        client_consumer_lookup_.erase({channel, it->second.client_tag});
        it = broker_consumers_.erase(it);
        if (virtual_host_) {
            // Unregister first: redelivery of the requeued messages must not
            // go back to the consumer that is going away.
            virtual_host_->unregisterConsumer(queue, broker_tag, this);
            virtual_host_->requeueConsumerUnacked(broker_tag, this);
        }
    }

    channels_.erase(channel);
    pending_content_.erase(channel);
}

}  // namespace mq::amqp091
