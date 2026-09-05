#ifndef MQ_PROTOCOL_AMQP091_WIRE_WRITER_HPP
#define MQ_PROTOCOL_AMQP091_WIRE_WRITER_HPP

#include <cstdint>
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

}  // namespace mq::amqp091

#endif  // MQ_PROTOCOL_AMQP091_WIRE_WRITER_HPP
