#include "mq/protocol/amqp091/exchange_methods.hpp"

#include "mq/protocol/amqp091/wire_reader.hpp"
#include "mq/protocol/amqp091/wire_writer.hpp"

#include <initializer_list>

namespace mq::amqp091 {

namespace {

bool writeShortString(WireWriter& writer, const std::string& value) {
    if (value.size() > 255) return false;
    writer.writeU8(static_cast<uint8_t>(value.size()));
    writer.writeBytes(value);
    return true;
}

bool readShortString(WireReader& reader, std::string& value) {
    uint8_t length = 0;
    std::string_view bytes;
    if (!reader.readU8(length) || !reader.readBytes(length, bytes)) return false;
    value.assign(bytes.data(), bytes.size());
    return true;
}

bool readFieldTable(WireReader& reader, FieldTable& table, std::string& error) {
    uint32_t length = 0;
    std::string_view body;
    if (!reader.readU32(length) || !reader.readBytes(length, body)) {
        error = "truncated field table";
        return false;
    }
    return decodeFieldTableBody(body, table, error);
}

bool readBits(WireReader& reader, std::initializer_list<bool*> bits) {
    uint8_t octet = 0;
    if (!reader.readU8(octet)) return false;
    size_t index = 0;
    for (bool* bit : bits) {
        if (index >= 8) return false;
        *bit = (octet & (0x80U >> index)) != 0;
        ++index;
    }
    return true;
}

void writeBits(WireWriter& writer, std::initializer_list<bool> bits) {
    uint8_t octet = 0;
    size_t index = 0;
    for (bool bit : bits) {
        if (index >= 8) return;
        if (bit) octet |= static_cast<uint8_t>(0x80U >> index);
        ++index;
    }
    writer.writeU8(octet);
}

}  // namespace

std::string encodeExchangeDeclare(const ExchangeDeclare& declare) {
    WireWriter writer;
    writer.writeU16(declare.ticket);
    writeShortString(writer, declare.exchange);
    writeShortString(writer, declare.type);
    writeBits(writer,
              {declare.passive, declare.durable, declare.auto_delete,
               declare.internal, declare.no_wait});
    writer.writeBytes(encodeFieldTable(declare.arguments));
    return writer.takeBytes();
}

bool decodeExchangeDeclare(std::string_view arguments, ExchangeDeclare& declare,
                           std::string& error) {
    WireReader reader(arguments);
    if (!reader.readU16(declare.ticket) ||
        !readShortString(reader, declare.exchange) ||
        !readShortString(reader, declare.type)) {
        error = "truncated exchange.declare";
        return false;
    }
    if (!readBits(reader,
                  {&declare.passive, &declare.durable, &declare.auto_delete,
                   &declare.internal, &declare.no_wait})) {
        error = "invalid exchange.declare bits";
        return false;
    }
    return readFieldTable(reader, declare.arguments, error);
}

std::string encodeExchangeDelete(const ExchangeDelete& delete_exchange) {
    WireWriter writer;
    writer.writeU16(delete_exchange.ticket);
    writeShortString(writer, delete_exchange.exchange);
    writeBits(writer, {delete_exchange.if_unused, delete_exchange.no_wait});
    return writer.takeBytes();
}

bool decodeExchangeDelete(std::string_view arguments,
                          ExchangeDelete& delete_exchange, std::string& error) {
    WireReader reader(arguments);
    if (!reader.readU16(delete_exchange.ticket) ||
        !readShortString(reader, delete_exchange.exchange)) {
        error = "truncated exchange.delete";
        return false;
    }
    return readBits(reader,
                    {&delete_exchange.if_unused, &delete_exchange.no_wait});
}

}  // namespace mq::amqp091
