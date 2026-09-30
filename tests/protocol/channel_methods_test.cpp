#include "mq/protocol/amqp091/channel_methods.hpp"

#include <gtest/gtest.h>

namespace mq::amqp091 {

// channel.open has no arguments in 0-9-1; the reserved shortstr is tolerated.
TEST(ChannelMethodsTest, RoundTripsOpenFlowAndClose) {
    std::string error;
    EXPECT_TRUE(decodeChannelOpen(encodeChannelOpen(ChannelOpen{}), error))
        << error;
    EXPECT_EQ(encodeChannelOpenOk(), std::string("\x00\x00\x00\x00", 4));

    ChannelFlow flow;
    flow.active = true;
    ChannelFlow decoded_flow;
    ASSERT_TRUE(decodeChannelFlow(encodeChannelFlow(flow), decoded_flow, error))
        << error;
    EXPECT_TRUE(decoded_flow.active);

    flow.active = false;
    ASSERT_TRUE(decodeChannelFlow(encodeChannelFlow(flow), decoded_flow, error))
        << error;
    EXPECT_FALSE(decoded_flow.active);

    ChannelClose close;
    close.reply_code = 406;
    close.reply_text = "precondition failed";
    close.class_id = 50;
    close.method_id = 10;
    ChannelClose decoded_close;
    ASSERT_TRUE(
        decodeChannelClose(encodeChannelClose(close), decoded_close, error))
        << error;
    EXPECT_EQ(decoded_close.reply_code, 406U);
    EXPECT_EQ(decoded_close.reply_text, "precondition failed");
    EXPECT_EQ(decoded_close.method_id, 10U);
}

// Argument bit fields occupy the least significant bit first (pika, amqp091-go and
// other clients rely on it); content header property flags use the opposite order.
TEST(ChannelMethodsTest, FlowUsesLeastSignificantBit) {
    ChannelFlow flow;
    flow.active = true;
    const std::string encoded = encodeChannelFlow(flow);
    ASSERT_EQ(encoded.size(), 1U);
    EXPECT_EQ(static_cast<unsigned char>(encoded[0]), 0x01U);
}

}  // namespace mq::amqp091

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
