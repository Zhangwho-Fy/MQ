#include "mq/protocol/amqp091/channel_methods.hpp"

#include <gtest/gtest.h>

namespace mq::amqp091 {

TEST(ChannelMethodsTest, OpenRoundTrip) {
    ChannelOpen open;
    std::string error;
    EXPECT_TRUE(decodeChannelOpen(encodeChannelOpen(open), error)) << error;
    EXPECT_EQ(encodeChannelOpenOk(), std::string());
}

TEST(ChannelMethodsTest, FlowUsesHighBit) {
    ChannelFlow flow;
    flow.active = true;
    const std::string encoded = encodeChannelFlow(flow);
    ASSERT_EQ(encoded.size(), 1U);
    EXPECT_EQ(static_cast<unsigned char>(encoded[0]), 0x80U);

    ChannelFlow decoded;
    std::string error;
    ASSERT_TRUE(decodeChannelFlow(encoded, decoded, error)) << error;
    EXPECT_TRUE(decoded.active);

    flow.active = false;
    ASSERT_TRUE(decodeChannelFlow(encodeChannelFlow(flow), decoded, error))
        << error;
    EXPECT_FALSE(decoded.active);
}

TEST(ChannelMethodsTest, FlowOkRoundTrip) {
    ChannelFlow flow;
    flow.active = true;
    ChannelFlow decoded;
    std::string error;
    ASSERT_TRUE(decodeChannelFlowOk(encodeChannelFlowOk(flow), decoded, error))
        << error;
    EXPECT_TRUE(decoded.active);
}

TEST(ChannelMethodsTest, CloseRoundTrip) {
    ChannelClose close;
    close.reply_code = 504;
    close.reply_text = "channel not open";
    close.class_id = 20;
    close.method_id = 10;

    ChannelClose decoded;
    std::string error;
    ASSERT_TRUE(decodeChannelClose(encodeChannelClose(close), decoded, error))
        << error;
    EXPECT_EQ(decoded.reply_code, 504U);
    EXPECT_EQ(decoded.reply_text, "channel not open");
    EXPECT_EQ(decoded.class_id, 20U);
    EXPECT_EQ(decoded.method_id, 10U);
    EXPECT_EQ(encodeChannelCloseOk(), std::string());
}

}  // namespace mq::amqp091

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
