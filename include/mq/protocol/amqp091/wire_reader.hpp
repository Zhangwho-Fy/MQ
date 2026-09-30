#ifndef MQ_PROTOCOL_AMQP091_WIRE_READER_HPP
#define MQ_PROTOCOL_AMQP091_WIRE_READER_HPP

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>

namespace mq::amqp091 {

class WireReader {
public:
    explicit WireReader(std::string_view bytes) : bytes_(bytes) {}

    bool readU8(uint8_t& value);
    bool readU16(uint16_t& value);
    bool readU32(uint32_t& value);
    bool readU64(uint64_t& value);
    bool readBytes(size_t count, std::string_view& value);

    size_t position() const { return position_; }
    size_t remaining() const { return bytes_.size() - position_; }
    std::string_view bytes() const { return bytes_; }

private:
    bool canRead(size_t count) const;

    std::string_view bytes_;
    size_t position_ = 0;
};

// Short string: one-octet length followed by the bytes.
inline bool readShortString(WireReader& reader, std::string& value) {
    uint8_t length = 0;
    std::string_view bytes;
    if (!reader.readU8(length) || !reader.readBytes(length, bytes)) return false;
    value.assign(bytes.data(), bytes.size());
    return true;
}

// Long string: four-octet length followed by the bytes.
inline bool readLongString(WireReader& reader, std::string& value) {
    uint32_t length = 0;
    std::string_view bytes;
    if (!reader.readU32(length) || !reader.readBytes(length, bytes)) return false;
    value.assign(bytes.data(), bytes.size());
    return true;
}

// Unpacks up to eight bits from one octet, first field in the least significant
// bit (mirrors writeBits).
inline bool readBits(WireReader& reader, std::initializer_list<bool*> bits) {
    uint8_t octet = 0;
    if (!reader.readU8(octet)) return false;
    size_t index = 0;
    for (bool* bit : bits) {
        if (index >= 8) return false;
        *bit = (octet & (1U << index)) != 0;
        ++index;
    }

    return true;
}

}  // namespace mq::amqp091

#endif  // MQ_PROTOCOL_AMQP091_WIRE_READER_HPP
