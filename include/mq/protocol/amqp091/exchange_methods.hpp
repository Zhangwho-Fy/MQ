#ifndef MQ_PROTOCOL_AMQP091_EXCHANGE_METHODS_HPP
#define MQ_PROTOCOL_AMQP091_EXCHANGE_METHODS_HPP

#include "fields.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace mq::amqp091 {

constexpr uint16_t kExchangeClassId = 40;

enum class ExchangeMethodId : uint16_t {
    Declare = 10,
    DeclareOk = 11,
    Delete = 20,
    DeleteOk = 21,
};

struct ExchangeDeclare {
    uint16_t ticket = 0;
    std::string exchange;
    std::string type;
    bool passive = false;
    bool durable = false;
    // auto-delete/internal reuse the reserved bit fields in the XML spec and
    // are the RabbitMQ 0-9-1 interpretations of those bits.
    bool auto_delete = false;
    bool internal = false;
    bool no_wait = false;
    FieldTable arguments;
};

struct ExchangeDelete {
    uint16_t ticket = 0;
    std::string exchange;
    bool if_unused = false;
    bool no_wait = false;
};

std::string encodeExchangeDeclare(const ExchangeDeclare& declare);
bool decodeExchangeDeclare(std::string_view arguments, ExchangeDeclare& declare,
                           std::string& error);

std::string encodeExchangeDelete(const ExchangeDelete& delete_exchange);
bool decodeExchangeDelete(std::string_view arguments,
                          ExchangeDelete& delete_exchange, std::string& error);

}  // namespace mq::amqp091

#endif  // MQ_PROTOCOL_AMQP091_EXCHANGE_METHODS_HPP
