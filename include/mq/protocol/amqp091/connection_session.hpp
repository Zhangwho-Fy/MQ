#ifndef MQ_PROTOCOL_AMQP091_CONNECTION_SESSION_HPP
#define MQ_PROTOCOL_AMQP091_CONNECTION_SESSION_HPP

#include "connection_methods.hpp"
#include "frame_codec.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace mq::amqp091 {

constexpr std::string_view kAmqp091ProtocolHeader("AMQP\x00\x00\x09\x01", 8);

struct ConnectionConfig {
    std::string virtual_host = "/";
    std::string mechanisms = "PLAIN";
    std::string locales = "en_US";
    std::string username = "guest";
    std::string password = "guest";
    uint16_t channel_max = 2047;
    uint32_t frame_max = 131072;
    uint16_t heartbeat = 60;
};

enum class ConnectionState {
    kAwaitProtocolHeader,
    kWaitStartOk,
    kWaitTuneOk,
    kWaitOpen,
    kReady,
    kClosing,
    kClosed,
};

struct SessionResult {
    bool ok = true;
    std::string error;
    uint16_t reply_code = 0;
    uint16_t failing_class_id = 0;
    uint16_t failing_method_id = 0;
};

class ConnectionSession {
public:
    using SendCallback = std::function<void(const std::string&)>;

    ConnectionSession(const ConnectionConfig& config, SendCallback send);

    SessionResult feed(std::string_view bytes);

    ConnectionState state() const { return state_; }
    uint16_t channelMax() const { return channel_max_; }
    uint32_t frameMax() const { return frame_max_; }
    uint16_t heartbeat() const { return heartbeat_; }

private:
    SessionResult processFrames(std::string_view bytes);
    SessionResult handleMethod(uint16_t channel, std::string_view payload);
    SessionResult handleConnectionMethod(const MethodHeader& header);
    SessionResult sendMethod(uint16_t class_id, uint16_t method_id,
                             const std::string& arguments);
    SessionResult fail(const std::string& message, uint16_t reply_code = 0,
                       uint16_t failing_class_id = 0,
                       uint16_t failing_method_id = 0);
    void sendFrame(uint8_t type, uint16_t channel, std::string_view payload);

    ConnectionConfig config_;
    SendCallback send_;
    FrameDecoder decoder_;
    ConnectionState state_ = ConnectionState::kAwaitProtocolHeader;
    std::string header_buffer_;
    uint16_t channel_max_ = 0;
    uint32_t frame_max_ = 0;
    uint16_t heartbeat_ = 0;
};

}  // namespace mq::amqp091

#endif  // MQ_PROTOCOL_AMQP091_CONNECTION_SESSION_HPP
