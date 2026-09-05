#ifndef MQ_PROTOCOL_AMQP091_CHANNEL_METHODS_HPP
#define MQ_PROTOCOL_AMQP091_CHANNEL_METHODS_HPP

#include <cstdint>
#include <string>
#include <string_view>

namespace mq::amqp091 {

constexpr uint16_t kChannelClassId = 20;

enum class ChannelMethodId : uint16_t {
    Open = 10,
    OpenOk = 11,
    Flow = 20,
    FlowOk = 21,
    Close = 40,
    CloseOk = 41,
};

struct ChannelOpen {
    // channel.open currently has no meaningful arguments in AMQP 0-9-1.
};

struct ChannelFlow {
    bool active = true;
};

struct ChannelClose {
    uint16_t reply_code = 0;
    std::string reply_text;
    uint16_t class_id = 0;
    uint16_t method_id = 0;
};

std::string encodeChannelOpen(const ChannelOpen& open);
bool decodeChannelOpen(std::string_view arguments, std::string& error);

std::string encodeChannelOpenOk();

std::string encodeChannelFlow(const ChannelFlow& flow);
bool decodeChannelFlow(std::string_view arguments, ChannelFlow& flow,
                       std::string& error);

std::string encodeChannelFlowOk(const ChannelFlow& flow);
bool decodeChannelFlowOk(std::string_view arguments, ChannelFlow& flow,
                         std::string& error);

std::string encodeChannelClose(const ChannelClose& close);
bool decodeChannelClose(std::string_view arguments, ChannelClose& close,
                        std::string& error);

std::string encodeChannelCloseOk();

}  // namespace mq::amqp091

#endif  // MQ_PROTOCOL_AMQP091_CHANNEL_METHODS_HPP
