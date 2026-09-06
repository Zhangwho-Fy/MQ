#ifndef MQ_PROTOCOL_AMQP091_BASIC_METHODS_HPP
#define MQ_PROTOCOL_AMQP091_BASIC_METHODS_HPP

#include "fields.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace mq::amqp091 {

constexpr uint16_t kBasicClassId = 60;

enum class BasicMethodId : uint16_t {
    Qos = 10,
    QosOk = 11,
    Consume = 20,
    ConsumeOk = 21,
    Cancel = 30,
    CancelOk = 31,
    Publish = 40,
    Return = 50,
    Deliver = 60,
    Get = 70,
    GetOk = 71,
    GetEmpty = 72,
    Ack = 80,
    Reject = 90,
    RecoverAsync = 100,
    Recover = 110,
    RecoverOk = 111,
};

struct BasicPublish {
    uint16_t ticket = 0;
    std::string exchange;
    std::string routing_key;
    bool mandatory = false;
    bool immediate = false;
};

struct BasicReturn {
    uint16_t reply_code = 0;
    std::string reply_text;
    std::string exchange;
    std::string routing_key;
};

struct BasicConsume {
    uint16_t ticket = 0;
    std::string queue;
    std::string consumer_tag;
    bool no_local = false;
    bool no_ack = false;
    bool exclusive = false;
    bool no_wait = false;
    FieldTable arguments;
};

struct BasicCancel {
    std::string consumer_tag;
    bool no_wait = false;
};

struct BasicDeliver {
    std::string consumer_tag;
    uint64_t delivery_tag = 0;
    bool redelivered = false;
    std::string exchange;
    std::string routing_key;
};

struct BasicAck {
    uint64_t delivery_tag = 0;
    bool multiple = false;
};

struct BasicReject {
    uint64_t delivery_tag = 0;
    bool requeue = false;
};

struct BasicGet {
    uint16_t ticket = 0;
    std::string queue;
    bool no_ack = false;
};

struct BasicGetOk {
    uint64_t delivery_tag = 0;
    bool redelivered = false;
    std::string exchange;
    std::string routing_key;
    uint32_t message_count = 0;
};

std::string encodeBasicPublish(const BasicPublish& publish);
bool decodeBasicPublish(std::string_view arguments, BasicPublish& publish,
                        std::string& error);

std::string encodeBasicReturn(const BasicReturn& ret);
bool decodeBasicReturn(std::string_view arguments, BasicReturn& ret,
                       std::string& error);

std::string encodeBasicConsume(const BasicConsume& consume);
bool decodeBasicConsume(std::string_view arguments, BasicConsume& consume,
                        std::string& error);

std::string encodeBasicCancel(const BasicCancel& cancel);
bool decodeBasicCancel(std::string_view arguments, BasicCancel& cancel,
                       std::string& error);

std::string encodeBasicDeliver(const BasicDeliver& deliver);
bool decodeBasicDeliver(std::string_view arguments, BasicDeliver& deliver,
                        std::string& error);

std::string encodeBasicConsumeOk(const std::string& consumer_tag);
std::string encodeBasicCancelOk(const std::string& consumer_tag);

std::string encodeBasicAck(const BasicAck& ack);
bool decodeBasicAck(std::string_view arguments, BasicAck& ack,
                    std::string& error);

std::string encodeBasicReject(const BasicReject& reject);
bool decodeBasicReject(std::string_view arguments, BasicReject& reject,
                       std::string& error);

std::string encodeBasicGet(const BasicGet& get);
bool decodeBasicGet(std::string_view arguments, BasicGet& get,
                    std::string& error);

std::string encodeBasicGetOk(const BasicGetOk& ok);
bool decodeBasicGetOk(std::string_view arguments, BasicGetOk& ok,
                      std::string& error);

struct ContentHeaderInfo {
    uint16_t class_id = 0;
    uint64_t body_size = 0;
    bool persistent = false;
};

// Builds a content header payload for the Basic class without properties.
std::string encodeContentHeader(uint64_t body_size);

// Parses a content header frame payload: class-id, weight, body-size and
// property flags. Properties are skipped, but delivery-mode is extracted.
bool decodeContentHeader(std::string_view payload, ContentHeaderInfo& info,
                         std::string& error);

}  // namespace mq::amqp091

#endif  // MQ_PROTOCOL_AMQP091_BASIC_METHODS_HPP
