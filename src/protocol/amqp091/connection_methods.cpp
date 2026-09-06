#include "mq/protocol/amqp091/connection_methods.hpp"

#include "mq/protocol/amqp091/wire_reader.hpp"
#include "mq/protocol/amqp091/wire_writer.hpp"

namespace mq::amqp091 {

namespace {

bool writeShortString(WireWriter& writer, std::string_view value) {
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

void writeLongString(WireWriter& writer, std::string_view value) {
    writer.writeU32(static_cast<uint32_t>(value.size()));
    writer.writeBytes(value);
}

bool readLongString(WireReader& reader, std::string& value) {
    uint32_t length = 0;
    std::string_view bytes;
    if (!reader.readU32(length) || !reader.readBytes(length, bytes)) return false;
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

}  // namespace

std::string encodeMethodHeader(uint16_t class_id, uint16_t method_id,
                               std::string_view arguments) {
    WireWriter writer;
    writer.writeU16(class_id);
    writer.writeU16(method_id);
    writer.writeBytes(arguments);
    return writer.takeBytes();
}

bool decodeMethodHeader(std::string_view payload, MethodHeader& header,
                        std::string& error) {
    WireReader reader(payload);
    uint16_t class_id = 0;
    uint16_t method_id = 0;
    if (!reader.readU16(class_id) || !reader.readU16(method_id)) {
        error = "truncated method header";
        return false;
    }
    header.class_id = class_id;
    header.method_id = method_id;
    header.arguments.assign(payload.data() + reader.position(),
                            reader.remaining());
    return true;
}

std::string encodeConnectionStart(const ConnectionStart& start) {
    WireWriter writer;
    writer.writeU8(start.version_major);
    writer.writeU8(start.version_minor);
    const std::string properties = encodeFieldTable(start.server_properties);
    writer.writeBytes(properties);
    writeLongString(writer, start.mechanisms);
    writeLongString(writer, start.locales);
    return writer.takeBytes();
}

bool decodeConnectionStart(std::string_view arguments, ConnectionStart& start,
                           std::string& error) {
    WireReader reader(arguments);
    if (!reader.readU8(start.version_major) ||
        !reader.readU8(start.version_minor)) {
        error = "truncated connection.start version";
        return false;
    }
    if (!readFieldTable(reader, start.server_properties, error)) return false;
    if (!readLongString(reader, start.mechanisms) ||
        !readLongString(reader, start.locales)) {
        error = "truncated connection.start mechanisms/locales";
        return false;
    }
    return true;
}

std::string encodeConnectionStartOk(const ConnectionStartOk& start_ok) {
    WireWriter writer;
    const std::string properties = encodeFieldTable(start_ok.client_properties);
    writer.writeBytes(properties);
    writeShortString(writer, start_ok.mechanism);
    writeLongString(writer, start_ok.response);
    writeShortString(writer, start_ok.locale);
    return writer.takeBytes();
}

bool decodeConnectionStartOk(std::string_view arguments,
                             ConnectionStartOk& start_ok, std::string& error) {
    WireReader reader(arguments);
    if (!readFieldTable(reader, start_ok.client_properties, error)) {
        return false;
    }
    if (!readShortString(reader, start_ok.mechanism) ||
        !readLongString(reader, start_ok.response) ||
        !readShortString(reader, start_ok.locale)) {
        error = "truncated connection.start-ok";
        return false;
    }
    return true;
}

std::string encodeConnectionTune(const ConnectionTune& tune) {
    WireWriter writer;
    writer.writeU16(tune.channel_max);
    writer.writeU32(tune.frame_max);
    writer.writeU16(tune.heartbeat);
    return writer.takeBytes();
}

bool decodeConnectionTune(std::string_view arguments, ConnectionTune& tune,
                          std::string& error) {
    WireReader reader(arguments);
    if (!reader.readU16(tune.channel_max) || !reader.readU32(tune.frame_max) ||
        !reader.readU16(tune.heartbeat)) {
        error = "truncated connection.tune";
        return false;
    }
    return true;
}

std::string encodeConnectionOpen(const ConnectionOpen& open) {
    WireWriter writer;
    writeShortString(writer, open.virtual_host);
    writer.writeU8(0);  // reserved-1 and reserved-2 bits
    return writer.takeBytes();
}

bool decodeConnectionOpen(std::string_view arguments, ConnectionOpen& open,
                          std::string& error) {
    WireReader reader(arguments);
    if (!readShortString(reader, open.virtual_host)) {
        error = "truncated connection.open virtual-host";
        return false;
    }
    return true;
}

std::string encodeConnectionOpenOk() {
    WireWriter writer;
    writeShortString(writer, "");  // reserved/known-hosts shortstr
    return writer.takeBytes();
}

std::string encodeConnectionClose(const ConnectionClose& close) {
    WireWriter writer;
    writer.writeU16(close.reply_code);
    writeShortString(writer, close.reply_text);
    writer.writeU16(close.class_id);
    writer.writeU16(close.method_id);
    return writer.takeBytes();
}

bool decodeConnectionClose(std::string_view arguments, ConnectionClose& close,
                           std::string& error) {
    WireReader reader(arguments);
    if (!reader.readU16(close.reply_code) ||
        !readShortString(reader, close.reply_text) ||
        !reader.readU16(close.class_id) || !reader.readU16(close.method_id)) {
        error = "truncated connection.close";
        return false;
    }
    return true;
}

}  // namespace mq::amqp091
