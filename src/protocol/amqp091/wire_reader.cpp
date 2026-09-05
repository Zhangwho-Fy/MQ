#include "mq/protocol/amqp091/wire_reader.hpp"

#include <cstring>

namespace mq::amqp091 {

bool WireReader::canRead(size_t count) const {
    return count <= bytes_.size() - position_;
}

bool WireReader::readU8(uint8_t& value) {
    if (!canRead(1)) return false;
    value = static_cast<uint8_t>(bytes_[position_++]);
    return true;
}

bool WireReader::readU16(uint16_t& value) {
    if (!canRead(2)) return false;
    value = (static_cast<uint16_t>(static_cast<uint8_t>(bytes_[position_])) << 8) |
            static_cast<uint16_t>(static_cast<uint8_t>(bytes_[position_ + 1]));
    position_ += 2;
    return true;
}

bool WireReader::readU32(uint32_t& value) {
    if (!canRead(4)) return false;
    value = (static_cast<uint32_t>(static_cast<uint8_t>(bytes_[position_])) << 24) |
            (static_cast<uint32_t>(static_cast<uint8_t>(bytes_[position_ + 1])) << 16) |
            (static_cast<uint32_t>(static_cast<uint8_t>(bytes_[position_ + 2])) << 8) |
            static_cast<uint32_t>(static_cast<uint8_t>(bytes_[position_ + 3]));
    position_ += 4;
    return true;
}

bool WireReader::readU64(uint64_t& value) {
    if (!canRead(8)) return false;
    value = 0;
    for (size_t i = 0; i < 8; ++i) {
        value = (value << 8) | static_cast<uint8_t>(bytes_[position_ + i]);
    }
    position_ += 8;
    return true;
}

bool WireReader::readBytes(size_t count, std::string_view& value) {
    if (!canRead(count)) return false;
    value = bytes_.substr(position_, count);
    position_ += count;
    return true;
}

}  // namespace mq::amqp091
