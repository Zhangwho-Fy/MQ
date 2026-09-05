#include "mq/protocol/amqp091/wire_writer.hpp"

namespace mq::amqp091 {

void WireWriter::writeU8(uint8_t value) {
    bytes_.push_back(static_cast<char>(value));
}

void WireWriter::writeU16(uint16_t value) {
    bytes_.push_back(static_cast<char>((value >> 8) & 0xff));
    bytes_.push_back(static_cast<char>(value & 0xff));
}

void WireWriter::writeU32(uint32_t value) {
    bytes_.push_back(static_cast<char>((value >> 24) & 0xff));
    bytes_.push_back(static_cast<char>((value >> 16) & 0xff));
    bytes_.push_back(static_cast<char>((value >> 8) & 0xff));
    bytes_.push_back(static_cast<char>(value & 0xff));
}

void WireWriter::writeU64(uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        bytes_.push_back(static_cast<char>((value >> shift) & 0xff));
    }
}

void WireWriter::writeBytes(std::string_view bytes) {
    bytes_.append(bytes.data(), bytes.size());
}

}  // namespace mq::amqp091
