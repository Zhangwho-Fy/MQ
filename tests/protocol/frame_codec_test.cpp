#include "mq/protocol/amqp091/frame_codec.hpp"

#include <gtest/gtest.h>

namespace mq::amqp091 {

TEST(FrameCodecTest, EncodesAndDecodesMethodFrame) {
    Frame input{kFrameMethod, 3, std::string("\x00\x0a\x00\x0b" "body", 8)};
    const std::string encoded = FrameEncoder::encode(input, 4096);
    ASSERT_EQ(encoded.size(), 16U);
    EXPECT_EQ(static_cast<unsigned char>(encoded[0]), kFrameMethod);
    EXPECT_EQ(static_cast<unsigned char>(encoded[1]), 0U);
    EXPECT_EQ(static_cast<unsigned char>(encoded[2]), 3U);
    EXPECT_EQ(static_cast<unsigned char>(encoded[6]), 8U);
    EXPECT_EQ(static_cast<unsigned char>(encoded.back()), kFrameEnd);

    FrameDecoder decoder(4096);
    std::vector<Frame> frames;
    DecodeResult result = decoder.feed(encoded, frames);
    ASSERT_EQ(result.status, DecodeStatus::kOk);
    ASSERT_EQ(result.frames_decoded, 1U);
    ASSERT_EQ(frames.size(), 1U);
    EXPECT_EQ(frames[0].type, input.type);
    EXPECT_EQ(frames[0].channel, input.channel);
    EXPECT_EQ(frames[0].payload, input.payload);
}

TEST(FrameCodecTest, HandlesFragmentedAndMultipleFrames) {
    const std::string first = FrameEncoder::encode(Frame{kFrameBody, 1, "abc"});
    const std::string second = FrameEncoder::encode(Frame{kFrameHeartbeat, 0, {}});
    const std::string combined = first + second;
    FrameDecoder decoder;
    std::vector<Frame> frames;

    EXPECT_EQ(decoder.feed(combined.substr(0, 4), frames).status, DecodeStatus::kNeedMoreData);
    EXPECT_EQ(decoder.feed(combined.substr(4, 3), frames).status, DecodeStatus::kNeedMoreData);
    DecodeResult result = decoder.feed(combined.substr(7), frames);
    ASSERT_EQ(result.status, DecodeStatus::kOk);
    ASSERT_EQ(result.frames_decoded, 2U);
    ASSERT_EQ(frames.size(), 2U);
    EXPECT_EQ(frames[0].payload, "abc");
    EXPECT_EQ(frames[1].type, kFrameHeartbeat);
}

TEST(FrameCodecTest, PreservesBinaryPayload) {
    const std::string payload("\x00\xff\x01", 3);
    FrameDecoder decoder;
    std::vector<Frame> frames;
    ASSERT_EQ(decoder.feed(FrameEncoder::encode(Frame{kFrameBody, 2, payload}), frames).status,
              DecodeStatus::kOk);
    ASSERT_EQ(frames.size(), 1U);
    EXPECT_EQ(frames[0].payload, payload);
}

TEST(FrameCodecTest, RejectsMalformedFrames) {
    FrameDecoder decoder;
    std::vector<Frame> frames;
    std::string wrong_end = FrameEncoder::encode(Frame{kFrameBody, 1, "x"});
    wrong_end.back() = static_cast<char>(0);
    EXPECT_EQ(decoder.feed(wrong_end, frames).status, DecodeStatus::kError);
    EXPECT_FALSE(decoder.feed({}, frames).error.empty());

    decoder.reset();
    std::string unknown = FrameEncoder::encode(Frame{kFrameBody, 1, "x"});
    unknown[0] = static_cast<char>(99);
    EXPECT_EQ(decoder.feed(unknown, frames).status, DecodeStatus::kError);
}

TEST(FrameCodecTest, EnforcesFrameMaxAndHeartbeatRules) {
    EXPECT_THROW(FrameEncoder::encode(Frame{kFrameBody, 1, std::string(4090, 'x')}, 4096),
                 FrameCodecError);
    EXPECT_THROW(FrameEncoder::encode(Frame{kFrameHeartbeat, 1, {}}), FrameCodecError);
    EXPECT_THROW(FrameEncoder::encode(Frame{kFrameHeartbeat, 0, "x"}), FrameCodecError);

    FrameDecoder decoder(4096);
    std::vector<Frame> frames;
    std::string heartbeat = FrameEncoder::encode(Frame{kFrameHeartbeat, 0, {}});
    heartbeat[6] = 1;
    heartbeat.insert(heartbeat.begin() + 7, 'x');
    EXPECT_EQ(decoder.feed(heartbeat, frames).status, DecodeStatus::kError);
}

}  // namespace mq::amqp091

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
