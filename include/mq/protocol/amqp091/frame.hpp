#ifndef MQ_PROTOCOL_AMQP091_FRAME_HPP
#define MQ_PROTOCOL_AMQP091_FRAME_HPP

#include <cstdint>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

namespace mq::amqp091 {

constexpr uint8_t kFrameMethod = 1;
constexpr uint8_t kFrameHeader = 2;
constexpr uint8_t kFrameBody = 3;
constexpr uint8_t kFrameHeartbeat = 8;
constexpr uint8_t kFrameEnd = 0xCE;
constexpr uint32_t kFrameMinSize = 4096;
constexpr size_t kFrameHeaderSize = 7;
constexpr size_t kFrameTrailerSize = 1;

struct Frame {
    uint8_t type = 0;
    uint16_t channel = 0;
    std::string payload;
};

enum class DecodeStatus {
    kOk,
    kNeedMoreData,
    kError,
};

struct DecodeResult {
    DecodeStatus status = DecodeStatus::kOk;
    size_t frames_decoded = 0;
    std::string error;
};

class FrameCodecError : public std::runtime_error {
public:
    explicit FrameCodecError(const std::string& message) : std::runtime_error(message) {}
};

inline bool isKnownFrameType(uint8_t type) {
    return type == kFrameMethod || type == kFrameHeader ||
           type == kFrameBody || type == kFrameHeartbeat;
}

inline size_t frameWireSize(const Frame& frame) {
    return kFrameHeaderSize + frame.payload.size() + kFrameTrailerSize;
}

}  // namespace mq::amqp091

#endif  // MQ_PROTOCOL_AMQP091_FRAME_HPP
