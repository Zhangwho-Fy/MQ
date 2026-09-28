#ifndef MQ_PROTOCOL_AMQP091_WIRE_WRITER_HPP
#define MQ_PROTOCOL_AMQP091_WIRE_WRITER_HPP

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace mq::amqp091 {

class WireWriter {
public:
    void writeU8(uint8_t value);
    void writeU16(uint16_t value);
    void writeU32(uint32_t value);
    void writeU64(uint64_t value);
    void writeBytes(std::string_view bytes);

    const std::string& bytes() const { return bytes_; }
    std::string takeBytes() { return std::move(bytes_); }

private:
    std::string bytes_;
};

// Short string: one-octet length followed by the bytes.
inline bool writeShortString(WireWriter& writer, std::string_view value) {
    if (value.size() > std::numeric_limits<uint8_t>::max()) return false;
    writer.writeU8(static_cast<uint8_t>(value.size()));
    writer.writeBytes(value);
    return true;
}

// Long string: four-octet length followed by the bytes.
inline bool writeLongString(WireWriter& writer, std::string_view value) {
    if (value.size() > std::numeric_limits<uint32_t>::max()) return false;
    writer.writeU32(static_cast<uint32_t>(value.size()));
    writer.writeBytes(value);
    return true;
}

// Packs up to eight bits into one octet, most significant bit first.
inline bool writeBits(WireWriter& writer, std::initializer_list<bool> bits) {
    uint8_t octet = 0;
    size_t index = 0;
    for (const bool bit : bits) {
        if (index >= 8) return false;
        if (bit) octet |= static_cast<uint8_t>(0x80U >> index);
        ++index;
    }

    writer.writeU8(octet);
    return true;
}

}  // namespace mq::amqp091

#endif  // MQ_PROTOCOL_AMQP091_WIRE_WRITER_HPP
