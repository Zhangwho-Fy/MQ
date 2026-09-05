#include "mq/protocol/amqp091/fields.hpp"

#include "mq/protocol/amqp091/wire_reader.hpp"
#include "mq/protocol/amqp091/wire_writer.hpp"

#include <cstring>

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

bool writeLongString(WireWriter& writer, const std::string& value) {
    if (value.size() > 0xFFFFFFFFULL) return false;
    writer.writeU32(static_cast<uint32_t>(value.size()));
    writer.writeBytes(value);
    return true;
}

bool writeFieldValueBytes(WireWriter& writer, const FieldTableEntry& entry) {
    if (!writeShortString(writer, entry.name)) return false;
    writer.writeU8(static_cast<uint8_t>(entry.type));
    writer.writeBytes(entry.value);
    return true;
}

bool skipFieldValue(WireReader& reader, char type) {
    std::string_view ignored;
    switch (type) {
        case 't':
        case 'b':
        case 'B':
            return reader.readBytes(1, ignored);
        case 's':
        case 'u':
            return reader.readBytes(2, ignored);
        case 'I':
        case 'i':
        case 'f':
            return reader.readBytes(4, ignored);
        case 'l':
        case 'd':
        case 'T':
            return reader.readBytes(8, ignored);
        case 'D':
            return reader.readBytes(5, ignored);
        case 'S': {
            uint32_t length = 0;
            std::string_view bytes;
            return reader.readU32(length) && reader.readBytes(length, bytes);
        }
        case 'F':
        case 'A': {
            uint32_t length = 0;
            std::string_view bytes;
            return reader.readU32(length) && reader.readBytes(length, bytes);
        }
        case 'V':
            return true;
        default:
            return false;
    }
}

bool readFieldValue(WireReader& reader, char type, FieldTableEntry& entry) {
    const size_t start = reader.position();
    if (!skipFieldValue(reader, type)) return false;
    entry.type = type;
    entry.value.assign(reader.bytes().data() + start, reader.position() - start);
    return true;
}

}  // namespace

void FieldTable::addBool(const std::string& name, bool value) {
    WireWriter writer;
    writer.writeU8(value ? 1 : 0);
    entries_.push_back(FieldTableEntry{name, 't', writer.takeBytes()});
}

void FieldTable::addString(const std::string& name, const std::string& value) {
    WireWriter writer;
    writeLongString(writer, value);
    entries_.push_back(FieldTableEntry{name, 'S', writer.takeBytes()});
}

void FieldTable::addTable(const std::string& name,
                          const std::string& encoded_table) {
    entries_.push_back(FieldTableEntry{name, 'F', encoded_table});
}

void FieldTable::addEntry(FieldTableEntry entry) {
    entries_.push_back(std::move(entry));
}

const std::string* FieldTable::findString(const std::string& name) const {
    for (const auto& entry : entries_) {
        if (entry.name == name && entry.type == 'S') {
            WireReader reader(entry.value);
            uint32_t length = 0;
            std::string_view bytes;
            if (reader.readU32(length) && reader.readBytes(length, bytes)) {
                static thread_local std::string storage;
                storage.assign(bytes.data(), bytes.size());
                return &storage;
            }
        }
    }
    return nullptr;
}

bool FieldTable::findBool(const std::string& name, bool& value) const {
    for (const auto& entry : entries_) {
        if (entry.name == name && entry.type == 't' && entry.value.size() == 1) {
            value = entry.value[0] != 0;
            return true;
        }
    }
    return false;
}

std::string encodeFieldTable(const FieldTable& table) {
    WireWriter body;
    for (const auto& entry : table.entries()) {
        writeFieldValueBytes(body, entry);
    }
    WireWriter writer;
    writer.writeU32(static_cast<uint32_t>(body.bytes().size()));
    writer.writeBytes(body.bytes());
    return writer.takeBytes();
}

bool decodeFieldTableBody(std::string_view body, FieldTable& table,
                          std::string& error) {
    WireReader reader(body);
    while (reader.remaining() > 0) {
        FieldTableEntry entry;
        if (!readShortString(reader, entry.name)) {
            error = "invalid field table key";
            return false;
        }
        uint8_t type = 0;
        if (!reader.readU8(type)) {
            error = "truncated field value type";
            return false;
        }
        if (!readFieldValue(reader, static_cast<char>(type), entry)) {
            error = "invalid or unsupported field value type";
            return false;
        }
        table.addEntry(std::move(entry));
    }
    return true;
}

bool decodeFieldTable(std::string_view bytes, FieldTable& table,
                      std::string& error) {
    WireReader reader(bytes);
    uint32_t table_length = 0;
    if (!reader.readU32(table_length)) {
        error = "truncated field table length";
        return false;
    }
    std::string_view body;
    if (!reader.readBytes(table_length, body)) {
        error = "field table length mismatch";
        return false;
    }
    return decodeFieldTableBody(body, table, error);
}

}  // namespace mq::amqp091
