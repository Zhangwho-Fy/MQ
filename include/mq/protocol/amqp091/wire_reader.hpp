#ifndef MQ_PROTOCOL_AMQP091_WIRE_READER_HPP
#define MQ_PROTOCOL_AMQP091_WIRE_READER_HPP

#include <cstdint>
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

private:
    bool canRead(size_t count) const;

    std::string_view bytes_;
    size_t position_ = 0;
};

}  // namespace mq::amqp091

#endif  // MQ_PROTOCOL_AMQP091_WIRE_READER_HPP
