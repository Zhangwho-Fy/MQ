#ifndef MQ_PROTOCOL_AMQP091_QUEUE_METHODS_HPP
#define MQ_PROTOCOL_AMQP091_QUEUE_METHODS_HPP

#include "fields.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace mq::amqp091 {

constexpr uint16_t kQueueClassId = 50;

enum class QueueMethodId : uint16_t {
    Declare = 10,
    DeclareOk = 11,
    Bind = 20,
    BindOk = 21,
    Purge = 30,
    PurgeOk = 31,
    Delete = 40,
    DeleteOk = 41,
    Unbind = 50,
    UnbindOk = 51,
};

struct QueueDeclare {
    uint16_t ticket = 0;
    std::string queue;
    bool passive = false;
    bool durable = false;
    bool exclusive = false;
    bool auto_delete = false;
    bool no_wait = false;
    FieldTable arguments;
};

struct QueueDeclareOk {
    std::string queue;
    uint32_t message_count = 0;
    uint32_t consumer_count = 0;
};

struct QueueBind {
    uint16_t ticket = 0;
    std::string queue;
    std::string exchange;
    std::string routing_key;
    bool no_wait = false;
    FieldTable arguments;
};

struct QueueUnbind {
    uint16_t ticket = 0;
    std::string queue;
    std::string exchange;
    std::string routing_key;
    FieldTable arguments;
};

struct QueuePurge {
    uint16_t ticket = 0;
    std::string queue;
    bool no_wait = false;
};

struct QueuePurgeOk {
    uint32_t message_count = 0;
};

struct QueueDelete {
    uint16_t ticket = 0;
    std::string queue;
    bool if_unused = false;
    bool if_empty = false;
    bool no_wait = false;
};

struct QueueDeleteOk {
    uint32_t message_count = 0;
};

std::string encodeQueueDeclare(const QueueDeclare& declare);
bool decodeQueueDeclare(std::string_view arguments, QueueDeclare& declare,
                        std::string& error);

std::string encodeQueueDeclareOk(const QueueDeclareOk& ok);
bool decodeQueueDeclareOk(std::string_view arguments, QueueDeclareOk& ok,
                          std::string& error);

std::string encodeQueueBind(const QueueBind& bind);
bool decodeQueueBind(std::string_view arguments, QueueBind& bind,
                     std::string& error);

std::string encodeQueueUnbind(const QueueUnbind& unbind);
bool decodeQueueUnbind(std::string_view arguments, QueueUnbind& unbind,
                       std::string& error);

std::string encodeQueuePurge(const QueuePurge& purge);
bool decodeQueuePurge(std::string_view arguments, QueuePurge& purge,
                      std::string& error);

std::string encodeQueuePurgeOk(const QueuePurgeOk& ok);
bool decodeQueuePurgeOk(std::string_view arguments, QueuePurgeOk& ok,
                        std::string& error);

std::string encodeQueueDelete(const QueueDelete& delete_queue);
bool decodeQueueDelete(std::string_view arguments, QueueDelete& delete_queue,
                       std::string& error);

std::string encodeQueueDeleteOk(const QueueDeleteOk& ok);
bool decodeQueueDeleteOk(std::string_view arguments, QueueDeleteOk& ok,
                         std::string& error);

}  // namespace mq::amqp091

#endif  // MQ_PROTOCOL_AMQP091_QUEUE_METHODS_HPP
