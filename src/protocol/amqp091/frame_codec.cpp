#include "mq/protocol/amqp091/frame_codec.hpp"

#include "mq/protocol/amqp091/wire_reader.hpp"
#include "mq/protocol/amqp091/wire_writer.hpp"

#include <limits>

namespace mq::amqp091 {

FrameDecoder::FrameDecoder(uint32_t frame_max) : frame_max_(frame_max) {
    if (frame_max_ != 0 && frame_max_ < kFrameMinSize) {
        throw FrameCodecError("frame-max is smaller than AMQP frame-min-size");
    }
}

DecodeResult FrameDecoder::error(const std::string& message) {
    failed_ = true;
    error_message_ = message;
    return DecodeResult{DecodeStatus::kError, 0, error_message_};
}

DecodeResult FrameDecoder::feed(std::string_view bytes, std::vector<Frame>& frames) {
    if (failed_) return DecodeResult{DecodeStatus::kError, 0, error_message_};
    if (!bytes.empty()) buffer_.append(bytes.data(), bytes.size());

    const size_t initial_count = frames.size();
    while (buffer_.size() >= kFrameHeaderSize) {
        WireReader header_reader(buffer_);
        uint8_t type = 0;
        uint16_t channel = 0;
        uint32_t payload_size = 0;
        if (!header_reader.readU8(type) || !header_reader.readU16(channel) ||
            !header_reader.readU32(payload_size)) {
            return error("truncated AMQP frame header");
        }
        if (!isKnownFrameType(type)) {
            return error("unknown AMQP frame type");
        }
        const uint64_t wire_size = static_cast<uint64_t>(kFrameHeaderSize) +
                                   payload_size + kFrameTrailerSize;
        if (frame_max_ != 0 && wire_size > frame_max_) {
            return error("AMQP frame exceeds frame-max");
        }
        if (wire_size > std::numeric_limits<size_t>::max()) {
            return error("AMQP frame size overflows host size_t");
        }
        if (buffer_.size() < static_cast<size_t>(wire_size)) {
            break;
        }

        const size_t total_size = static_cast<size_t>(wire_size);
        const uint8_t frame_end = static_cast<uint8_t>(buffer_[total_size - 1]);
        if (frame_end != kFrameEnd) {
            return error("invalid AMQP frame-end octet");
        }
        if (type == kFrameHeartbeat && (channel != 0 || payload_size != 0)) {
            return error("heartbeat frame must use channel 0 and an empty payload");
        }

        Frame frame;
        frame.type = type;
        frame.channel = channel;
        frame.payload.assign(buffer_.data() + kFrameHeaderSize, payload_size);
        frames.push_back(std::move(frame));
        buffer_.erase(0, total_size);
    }

    const size_t decoded = frames.size() - initial_count;
    if (decoded == 0 && !buffer_.empty()) {
        return DecodeResult{DecodeStatus::kNeedMoreData, 0, {}};
    }
    return DecodeResult{DecodeStatus::kOk, decoded, {}};
}

void FrameDecoder::reset() {
    buffer_.clear();
    failed_ = false;
    error_message_.clear();
}

void FrameDecoder::setFrameMax(uint32_t frame_max) {
    if (frame_max_ != 0 && frame_max < kFrameMinSize) {
        throw FrameCodecError("frame-max is smaller than AMQP frame-min-size");
    }
    frame_max_ = frame_max;
}

std::string FrameEncoder::encode(const Frame& frame, uint32_t frame_max) {
    if (!isKnownFrameType(frame.type)) {
        throw FrameCodecError("unknown AMQP frame type");
    }
    if (frame.type == kFrameHeartbeat && (frame.channel != 0 || !frame.payload.empty())) {
        throw FrameCodecError("heartbeat frame must use channel 0 and an empty payload");
    }
    if (frame.payload.size() > std::numeric_limits<uint32_t>::max()) {
        throw FrameCodecError("AMQP payload is too large");
    }
    const size_t total_size = frameWireSize(frame);
    if (frame_max != 0 && total_size > frame_max) {
        throw FrameCodecError("AMQP frame exceeds frame-max");
    }

    WireWriter writer;
    writer.writeU8(frame.type);
    writer.writeU16(frame.channel);
    writer.writeU32(static_cast<uint32_t>(frame.payload.size()));
    writer.writeBytes(frame.payload);
    writer.writeU8(kFrameEnd);
    return writer.takeBytes();
}

}  // namespace mq::amqp091
