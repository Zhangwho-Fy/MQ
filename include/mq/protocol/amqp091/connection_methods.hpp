#ifndef MQ_PROTOCOL_AMQP091_CONNECTION_METHODS_HPP
#define MQ_PROTOCOL_AMQP091_CONNECTION_METHODS_HPP

#include "fields.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace mq::amqp091 {

constexpr uint16_t kConnectionClassId = 10;

enum class ConnectionMethodId : uint16_t {
    Start = 10,
    StartOk = 11,
    Secure = 20,
    SecureOk = 21,
    Tune = 30,
    TuneOk = 31,
    Open = 40,
    OpenOk = 41,
    Close = 50,
    CloseOk = 51,
};

struct MethodHeader {
    uint16_t class_id = 0;
    uint16_t method_id = 0;
    std::string arguments;
};

std::string encodeMethodHeader(uint16_t class_id, uint16_t method_id,
                               std::string_view arguments);
bool decodeMethodHeader(std::string_view payload, MethodHeader& header,
                        std::string& error);

struct ConnectionStart {
    uint8_t version_major = 0;
    uint8_t version_minor = 9;
    FieldTable server_properties;
    std::string mechanisms;
    std::string locales;
};

struct ConnectionStartOk {
    FieldTable client_properties;
    std::string mechanism;
    std::string response;
    std::string locale;
};

struct ConnectionTune {
    uint16_t channel_max = 0;
    uint32_t frame_max = 0;
    uint16_t heartbeat = 0;
};

struct ConnectionOpen {
    std::string virtual_host;
};

struct ConnectionClose {
    uint16_t reply_code = 0;
    std::string reply_text;
    uint16_t class_id = 0;
    uint16_t method_id = 0;
};

std::string encodeConnectionStart(const ConnectionStart& start);
bool decodeConnectionStart(std::string_view arguments, ConnectionStart& start,
                           std::string& error);

std::string encodeConnectionStartOk(const ConnectionStartOk& start_ok);
bool decodeConnectionStartOk(std::string_view arguments,
                             ConnectionStartOk& start_ok, std::string& error);

std::string encodeConnectionTune(const ConnectionTune& tune);
bool decodeConnectionTune(std::string_view arguments, ConnectionTune& tune,
                          std::string& error);

std::string encodeConnectionOpen(const ConnectionOpen& open);
bool decodeConnectionOpen(std::string_view arguments, ConnectionOpen& open,
                          std::string& error);

std::string encodeConnectionOpenOk();

std::string encodeConnectionClose(const ConnectionClose& close);
bool decodeConnectionClose(std::string_view arguments, ConnectionClose& close,
                           std::string& error);

}  // namespace mq::amqp091

#endif  // MQ_PROTOCOL_AMQP091_CONNECTION_METHODS_HPP
