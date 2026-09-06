#ifndef MQ_PROTOCOL_AMQP091_CONFIRM_METHODS_HPP
#define MQ_PROTOCOL_AMQP091_CONFIRM_METHODS_HPP

#include <cstdint>
#include <string>
#include <string_view>

namespace mq::amqp091 {

// RabbitMQ publisher-confirm extension class.
constexpr uint16_t kConfirmClassId = 85;

enum class ConfirmMethodId : uint16_t {
    Select = 10,
    SelectOk = 11,
};

struct ConfirmSelect {
    bool no_wait = false;
};

std::string encodeConfirmSelect(const ConfirmSelect& select);
bool decodeConfirmSelect(std::string_view arguments, ConfirmSelect& select,
                         std::string& error);

std::string encodeConfirmSelectOk();

}  // namespace mq::amqp091

#endif  // MQ_PROTOCOL_AMQP091_CONFIRM_METHODS_HPP
