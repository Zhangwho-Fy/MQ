#ifndef MQ_PROTOCOL_AMQP091_CONNECTION_SESSION_HPP
#define MQ_PROTOCOL_AMQP091_CONNECTION_SESSION_HPP

#include "connection_methods.hpp"
#include "channel_methods.hpp"
#include "basic_methods.hpp"
#include "confirm_methods.hpp"
#include "exchange_methods.hpp"
#include "frame_codec.hpp"
#include "queue_methods.hpp"
#include "mq/broker/virtual_host.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
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

    ConnectionSession(const ConnectionConfig& config, SendCallback send,
                      std::shared_ptr<broker::VirtualHost> virtual_host =
                          nullptr);
    ~ConnectionSession();

    SessionResult feed(std::string_view bytes);

    ConnectionState state() const { return state_; }
    uint16_t channelMax() const { return channel_max_; }
    uint32_t frameMax() const { return frame_max_; }
    uint16_t heartbeat() const { return heartbeat_; }
    bool isChannelOpen(uint16_t channel) const;
    size_t openChannelCount() const;

private:
    enum class ChannelLifecycle {
        kOpen,
        kClosing,
    };

    struct ChannelState {
        ChannelLifecycle lifecycle = ChannelLifecycle::kOpen;
        bool flow_active = true;
    };

    struct PendingContent {
        BasicPublish publish;
        ContentHeaderInfo header;
        std::string header_payload;
        std::string body;
        bool header_received = false;
    };

    struct SessionConsumer {
        uint16_t channel = 0;
        std::string queue;
        std::string client_tag;
        std::string broker_tag;
    };

    SessionResult processFrames(std::string_view bytes);
    SessionResult handleMethod(uint16_t channel, std::string_view payload);
    SessionResult handleBasicMethod(uint16_t channel,
                                    const MethodHeader& header);
    SessionResult handleConfirmMethod(uint16_t channel,
                                      const MethodHeader& header);
    SessionResult handleContentFrame(uint16_t channel, const Frame& frame);
    SessionResult finishPendingContent(uint16_t channel,
                                       PendingContent& pending);
    void deliverToConsumer(const std::string& consumer_tag,
                           const std::string& queue,
                           const broker::Message& message);
    void sendContent(uint16_t channel, const std::string& header_payload,
                     const std::string& body);
    SessionResult handleConnectionMethod(const MethodHeader& header);
    SessionResult handleChannelMethod(uint16_t channel,
                                      const MethodHeader& header);
    SessionResult handleExchangeMethod(uint16_t channel,
                                       const MethodHeader& header);
    SessionResult handleQueueMethod(uint16_t channel,
                                    const MethodHeader& header);
    SessionResult sendMethodOnChannel(uint16_t channel, uint16_t class_id,
                                      uint16_t method_id,
                                      const std::string& arguments);
    SessionResult sendChannelError(uint16_t channel, uint16_t reply_code,
                                   uint16_t failing_class_id,
                                   uint16_t failing_method_id,
                                   const std::string& text);
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
    std::map<uint16_t, ChannelState> channels_;
    std::map<uint16_t, PendingContent> pending_content_;
    std::map<std::string, SessionConsumer> broker_consumers_;
    std::map<std::pair<uint16_t, std::string>, std::string>
        client_consumer_lookup_;
    std::map<uint16_t, uint64_t> delivery_seq_;
    std::map<uint16_t, uint16_t> channel_prefetch_;
    std::map<uint16_t, std::map<uint64_t, uint64_t>>
        delivery_tag_to_message_;
    std::set<uint16_t> confirm_channels_;
    std::map<uint16_t, uint64_t> publish_seq_;
    uint64_t generated_consumer_seq_ = 0;
    std::shared_ptr<broker::VirtualHost> virtual_host_;
    uint64_t generated_queue_seq_ = 0;
};

}  // namespace mq::amqp091

#endif  // MQ_PROTOCOL_AMQP091_CONNECTION_SESSION_HPP
