#ifndef MQ_PROTOCOL_AMQP091_FRAME_CODEC_HPP
#define MQ_PROTOCOL_AMQP091_FRAME_CODEC_HPP

#include "frame.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mq::amqp091 {

class FrameDecoder {
public:
    explicit FrameDecoder(uint32_t frame_max = 131072);

    DecodeResult feed(std::string_view bytes, std::vector<Frame>& frames);
    void reset();
    uint32_t frameMax() const { return frame_max_; }

private:
    DecodeResult error(const std::string& message);

    uint32_t frame_max_;
    std::string buffer_;
    bool failed_ = false;
    std::string error_message_;
};

class FrameEncoder {
public:
    // frame_max == 0 means no configured maximum.
    static std::string encode(const Frame& frame, uint32_t frame_max = 0);
};

}  // namespace mq::amqp091

#endif  // MQ_PROTOCOL_AMQP091_FRAME_CODEC_HPP
