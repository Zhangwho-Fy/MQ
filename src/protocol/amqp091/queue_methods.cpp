#include "mq/protocol/amqp091/queue_methods.hpp"

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

std::string encodeQueueDeclare(const QueueDeclare& declare) {
    WireWriter writer;
    writer.writeU16(declare.ticket);
    writeShortString(writer, declare.queue);
    writeBits(writer,
              {declare.passive, declare.durable, declare.exclusive,
               declare.auto_delete, declare.no_wait});
    writer.writeBytes(encodeFieldTable(declare.arguments));
    return writer.takeBytes();
}

bool decodeQueueDeclare(std::string_view arguments, QueueDeclare& declare,
                        std::string& error) {
    WireReader reader(arguments);
    if (!reader.readU16(declare.ticket) ||
        !readShortString(reader, declare.queue)) {
        error = "truncated queue.declare";
        return false;
    }
    if (!readBits(reader,
                  {&declare.passive, &declare.durable, &declare.exclusive,
                   &declare.auto_delete, &declare.no_wait})) {
        error = "invalid queue.declare bits";
        return false;
    }
    return readFieldTable(reader, declare.arguments, error);
}

std::string encodeQueueDeclareOk(const QueueDeclareOk& ok) {
    WireWriter writer;
    writeShortString(writer, ok.queue);
    writer.writeU32(ok.message_count);
    writer.writeU32(ok.consumer_count);
    return writer.takeBytes();
}

bool decodeQueueDeclareOk(std::string_view arguments, QueueDeclareOk& ok,
                          std::string& error) {
    WireReader reader(arguments);
    if (!readShortString(reader, ok.queue) || !reader.readU32(ok.message_count) ||
        !reader.readU32(ok.consumer_count)) {
        error = "truncated queue.declare-ok";
        return false;
    }
    return true;
}

std::string encodeQueueBind(const QueueBind& bind) {
    WireWriter writer;
    writer.writeU16(bind.ticket);
    writeShortString(writer, bind.queue);
    writeShortString(writer, bind.exchange);
    writeShortString(writer, bind.routing_key);
    writeBits(writer, {bind.no_wait});
    writer.writeBytes(encodeFieldTable(bind.arguments));
    return writer.takeBytes();
}

bool decodeQueueBind(std::string_view arguments, QueueBind& bind,
                     std::string& error) {
    WireReader reader(arguments);
    if (!reader.readU16(bind.ticket) ||
        !readShortString(reader, bind.queue) ||
        !readShortString(reader, bind.exchange) ||
        !readShortString(reader, bind.routing_key)) {
        error = "truncated queue.bind";
        return false;
    }
    if (!readBits(reader, {&bind.no_wait})) {
        error = "invalid queue.bind bits";
        return false;
    }
    return readFieldTable(reader, bind.arguments, error);
}

std::string encodeQueueUnbind(const QueueUnbind& unbind) {
    WireWriter writer;
    writer.writeU16(unbind.ticket);
    writeShortString(writer, unbind.queue);
    writeShortString(writer, unbind.exchange);
    writeShortString(writer, unbind.routing_key);
    writer.writeBytes(encodeFieldTable(unbind.arguments));
    return writer.takeBytes();
}

bool decodeQueueUnbind(std::string_view arguments, QueueUnbind& unbind,
                       std::string& error) {
    WireReader reader(arguments);
    if (!reader.readU16(unbind.ticket) ||
        !readShortString(reader, unbind.queue) ||
        !readShortString(reader, unbind.exchange) ||
        !readShortString(reader, unbind.routing_key)) {
        error = "truncated queue.unbind";
        return false;
    }
    return readFieldTable(reader, unbind.arguments, error);
}

std::string encodeQueuePurge(const QueuePurge& purge) {
    WireWriter writer;
    writer.writeU16(purge.ticket);
    writeShortString(writer, purge.queue);
    writeBits(writer, {purge.no_wait});
    return writer.takeBytes();
}

bool decodeQueuePurge(std::string_view arguments, QueuePurge& purge,
                      std::string& error) {
    WireReader reader(arguments);
    if (!reader.readU16(purge.ticket) ||
        !readShortString(reader, purge.queue)) {
        error = "truncated queue.purge";
        return false;
    }
    return readBits(reader, {&purge.no_wait});
}

std::string encodeQueuePurgeOk(const QueuePurgeOk& ok) {
    WireWriter writer;
    writer.writeU32(ok.message_count);
    return writer.takeBytes();
}

bool decodeQueuePurgeOk(std::string_view arguments, QueuePurgeOk& ok,
                        std::string& error) {
    WireReader reader(arguments);
    if (!reader.readU32(ok.message_count)) {
        error = "truncated queue.purge-ok";
        return false;
    }
    return true;
}

std::string encodeQueueDelete(const QueueDelete& delete_queue) {
    WireWriter writer;
    writer.writeU16(delete_queue.ticket);
    writeShortString(writer, delete_queue.queue);
    writeBits(writer,
              {delete_queue.if_unused, delete_queue.if_empty,
               delete_queue.no_wait});
    return writer.takeBytes();
}

bool decodeQueueDelete(std::string_view arguments, QueueDelete& delete_queue,
                       std::string& error) {
    WireReader reader(arguments);
    if (!reader.readU16(delete_queue.ticket) ||
        !readShortString(reader, delete_queue.queue)) {
        error = "truncated queue.delete";
        return false;
    }
    return readBits(reader,
                    {&delete_queue.if_unused, &delete_queue.if_empty,
                     &delete_queue.no_wait});
}

std::string encodeQueueDeleteOk(const QueueDeleteOk& ok) {
    WireWriter writer;
    writer.writeU32(ok.message_count);
    return writer.takeBytes();
}

bool decodeQueueDeleteOk(std::string_view arguments, QueueDeleteOk& ok,
                         std::string& error) {
    WireReader reader(arguments);
    if (!reader.readU32(ok.message_count)) {
        error = "truncated queue.delete-ok";
        return false;
    }
    return true;
}

}  // namespace mq::amqp091
