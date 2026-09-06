#include "mq/protocol/amqp091/basic_methods.hpp"

#include "mq/protocol/amqp091/wire_reader.hpp"
#include "mq/protocol/amqp091/wire_writer.hpp"

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

bool writeBits(WireWriter& writer, std::initializer_list<bool> bits) {
    uint8_t octet = 0;
    size_t index = 0;
    for (bool bit : bits) {
        if (index >= 8) return false;
        if (bit) octet |= static_cast<uint8_t>(0x80U >> index);
        ++index;
    }
    writer.writeU8(octet);
    return true;
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

bool skipShortString(WireReader& reader) {
    uint8_t length = 0;
    std::string_view bytes;
    return reader.readU8(length) && reader.readBytes(length, bytes);
}

bool skipTable(WireReader& reader) {
    uint32_t length = 0;
    std::string_view bytes;
    return reader.readU32(length) && reader.readBytes(length, bytes);
}

}  // namespace

std::string encodeBasicPublish(const BasicPublish& publish) {
    WireWriter writer;
    writer.writeU16(publish.ticket);
    writeShortString(writer, publish.exchange);
    writeShortString(writer, publish.routing_key);
    writeBits(writer, {publish.mandatory, publish.immediate});
    return writer.takeBytes();
}

bool decodeBasicPublish(std::string_view arguments, BasicPublish& publish,
                        std::string& error) {
    WireReader reader(arguments);
    if (!reader.readU16(publish.ticket) ||
        !readShortString(reader, publish.exchange) ||
        !readShortString(reader, publish.routing_key)) {
        error = "truncated basic.publish";
        return false;
    }
    return readBits(reader, {&publish.mandatory, &publish.immediate});
}

std::string encodeBasicReturn(const BasicReturn& ret) {
    WireWriter writer;
    writer.writeU16(ret.reply_code);
    writeShortString(writer, ret.reply_text);
    writeShortString(writer, ret.exchange);
    writeShortString(writer, ret.routing_key);
    return writer.takeBytes();
}

bool decodeBasicReturn(std::string_view arguments, BasicReturn& ret,
                       std::string& error) {
    WireReader reader(arguments);
    if (!reader.readU16(ret.reply_code) ||
        !readShortString(reader, ret.reply_text) ||
        !readShortString(reader, ret.exchange) ||
        !readShortString(reader, ret.routing_key)) {
        error = "truncated basic.return";
        return false;
    }
    return true;
}

std::string encodeBasicConsume(const BasicConsume& consume) {
    WireWriter writer;
    writer.writeU16(consume.ticket);
    writeShortString(writer, consume.queue);
    writeShortString(writer, consume.consumer_tag);
    writeBits(writer,
              {consume.no_local, consume.no_ack, consume.exclusive,
               consume.no_wait});
    writer.writeBytes(encodeFieldTable(consume.arguments));
    return writer.takeBytes();
}

bool decodeBasicConsume(std::string_view arguments, BasicConsume& consume,
                        std::string& error) {
    WireReader reader(arguments);
    if (!reader.readU16(consume.ticket) ||
        !readShortString(reader, consume.queue) ||
        !readShortString(reader, consume.consumer_tag)) {
        error = "truncated basic.consume";
        return false;
    }
    if (!readBits(reader,
                  {&consume.no_local, &consume.no_ack, &consume.exclusive,
                   &consume.no_wait})) {
        error = "invalid basic.consume bits";
        return false;
    }
    return skipTable(reader);
}

std::string encodeBasicCancel(const BasicCancel& cancel) {
    WireWriter writer;
    writeShortString(writer, cancel.consumer_tag);
    writeBits(writer, {cancel.no_wait});
    return writer.takeBytes();
}

bool decodeBasicCancel(std::string_view arguments, BasicCancel& cancel,
                       std::string& error) {
    WireReader reader(arguments);
    if (!readShortString(reader, cancel.consumer_tag)) {
        error = "truncated basic.cancel";
        return false;
    }
    return readBits(reader, {&cancel.no_wait});
}

std::string encodeBasicDeliver(const BasicDeliver& deliver) {
    WireWriter writer;
    writeShortString(writer, deliver.consumer_tag);
    writer.writeU64(deliver.delivery_tag);
    writeBits(writer, {deliver.redelivered});
    writeShortString(writer, deliver.exchange);
    writeShortString(writer, deliver.routing_key);
    return writer.takeBytes();
}

bool decodeBasicDeliver(std::string_view arguments, BasicDeliver& deliver,
                        std::string& error) {
    WireReader reader(arguments);
    if (!readShortString(reader, deliver.consumer_tag) ||
        !reader.readU64(deliver.delivery_tag)) {
        error = "truncated basic.deliver";
        return false;
    }
    if (!readBits(reader, {&deliver.redelivered})) {
        error = "invalid basic.deliver bits";
        return false;
    }
    return readShortString(reader, deliver.exchange) &&
           readShortString(reader, deliver.routing_key);
}

std::string encodeBasicConsumeOk(const std::string& consumer_tag) {
    WireWriter writer;
    writeShortString(writer, consumer_tag);
    return writer.takeBytes();
}

std::string encodeBasicCancelOk(const std::string& consumer_tag) {
    WireWriter writer;
    writeShortString(writer, consumer_tag);
    return writer.takeBytes();
}

std::string encodeBasicAck(const BasicAck& ack) {
    WireWriter writer;
    writer.writeU64(ack.delivery_tag);
    writeBits(writer, {ack.multiple});
    return writer.takeBytes();
}

bool decodeBasicAck(std::string_view arguments, BasicAck& ack,
                    std::string& error) {
    WireReader reader(arguments);
    if (!reader.readU64(ack.delivery_tag)) {
        error = "truncated basic.ack";
        return false;
    }
    return readBits(reader, {&ack.multiple});
}

std::string encodeBasicReject(const BasicReject& reject) {
    WireWriter writer;
    writer.writeU64(reject.delivery_tag);
    writeBits(writer, {reject.requeue});
    return writer.takeBytes();
}

bool decodeBasicReject(std::string_view arguments, BasicReject& reject,
                       std::string& error) {
    WireReader reader(arguments);
    if (!reader.readU64(reject.delivery_tag)) {
        error = "truncated basic.reject";
        return false;
    }
    return readBits(reader, {&reject.requeue});
}

std::string encodeBasicGet(const BasicGet& get) {
    WireWriter writer;
    writer.writeU16(get.ticket);
    writeShortString(writer, get.queue);
    writeBits(writer, {get.no_ack});
    return writer.takeBytes();
}

bool decodeBasicGet(std::string_view arguments, BasicGet& get,
                    std::string& error) {
    WireReader reader(arguments);
    if (!reader.readU16(get.ticket) ||
        !readShortString(reader, get.queue)) {
        error = "truncated basic.get";
        return false;
    }
    return readBits(reader, {&get.no_ack});
}

std::string encodeBasicGetOk(const BasicGetOk& ok) {
    WireWriter writer;
    writer.writeU64(ok.delivery_tag);
    writeBits(writer, {ok.redelivered});
    writeShortString(writer, ok.exchange);
    writeShortString(writer, ok.routing_key);
    writer.writeU32(ok.message_count);
    return writer.takeBytes();
}

bool decodeBasicGetOk(std::string_view arguments, BasicGetOk& ok,
                      std::string& error) {
    WireReader reader(arguments);
    if (!reader.readU64(ok.delivery_tag)) {
        error = "truncated basic.get-ok";
        return false;
    }
    if (!readBits(reader, {&ok.redelivered})) {
        error = "invalid basic.get-ok bits";
        return false;
    }
    return readShortString(reader, ok.exchange) &&
           readShortString(reader, ok.routing_key) &&
           reader.readU32(ok.message_count);
}

std::string encodeContentHeader(uint64_t body_size) {
    WireWriter writer;
    writer.writeU16(kBasicClassId);
    writer.writeU16(0);  // weight
    writer.writeU64(body_size);
    writer.writeU16(0);  // property flags: none
    return writer.takeBytes();
}

bool decodeContentHeader(std::string_view payload, ContentHeaderInfo& info,
                         std::string& error) {
    WireReader reader(payload);
    uint16_t class_id = 0;
    uint16_t weight = 0;
    if (!reader.readU16(class_id) || !reader.readU16(weight) ||
        !reader.readU64(info.body_size)) {
        error = "truncated content header";
        return false;
    }
    info.class_id = class_id;
    if (class_id != kBasicClassId || weight != 0) {
        error = "unsupported content class";
        return false;
    }

    uint16_t flags = 0;
    if (!reader.readU16(flags)) {
        error = "truncated property flags";
        return false;
    }

    // Property order matches AMQP 0-9-1 flag positions, MSB first.
    struct PropertyType {
        char kind;  // 's' shortstr, 'o' octet, 't' timestamp, 'T' table
        bool* persistent_flag;
    };
    bool delivery_mode_2 = false;
    const PropertyType properties[] = {
        {'s', nullptr},  // content-type
        {'s', nullptr},  // content-encoding
        {'T', nullptr},  // headers
        {'o', &delivery_mode_2},  // delivery-mode
        {'o', nullptr},  // priority
        {'s', nullptr},  // correlation-id
        {'s', nullptr},  // reply-to
        {'s', nullptr},  // expiration
        {'s', nullptr},  // message-id
        {'t', nullptr},  // timestamp
        {'s', nullptr},  // type
        {'s', nullptr},  // user-id
        {'s', nullptr},  // app-id
        {'s', nullptr},  // reserved
    };

    for (size_t i = 0; i < sizeof(properties) / sizeof(properties[0]); ++i) {
        const uint16_t mask = static_cast<uint16_t>(0x8000U >> i);
        if ((flags & mask) == 0) continue;
        if (properties[i].kind == 's') {
            if (!skipShortString(reader)) {
                error = "invalid short string property";
                return false;
            }
        } else if (properties[i].kind == 'o') {
            uint8_t value = 0;
            if (!reader.readU8(value)) {
                error = "truncated octet property";
                return false;
            }
            if (properties[i].persistent_flag != nullptr) {
                delivery_mode_2 = value == 2;
            }
        } else if (properties[i].kind == 't') {
            uint64_t value = 0;
            if (!reader.readU64(value)) {
                error = "truncated timestamp property";
                return false;
            }
        } else if (properties[i].kind == 'T') {
            if (!skipTable(reader)) {
                error = "truncated table property";
                return false;
            }
        }
    }
    info.persistent = delivery_mode_2;
    return true;
}

}  // namespace mq::amqp091
