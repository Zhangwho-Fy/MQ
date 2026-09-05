#include "mq/protocol/amqp091/connection_methods.hpp"

#include <gtest/gtest.h>

namespace mq::amqp091 {

TEST(ConnectionMethodsTest, MethodHeaderRoundTrip) {
    const std::string payload =
        encodeMethodHeader(10, 10, std::string("\x01\x02\x03", 3));
    MethodHeader header;
    std::string error;
    ASSERT_TRUE(decodeMethodHeader(payload, header, error)) << error;
    EXPECT_EQ(header.class_id, 10U);
    EXPECT_EQ(header.method_id, 10U);
    EXPECT_EQ(header.arguments, std::string("\x01\x02\x03", 3));
}

TEST(ConnectionMethodsTest, StartRoundTrip) {
    ConnectionStart start;
    start.version_major = 0;
    start.version_minor = 9;
    start.server_properties.addString("product", "FyMQ");
    start.mechanisms = "PLAIN AMQPLAIN";
    start.locales = "en_US zh_CN";

    ConnectionStart decoded;
    std::string error;
    const std::string args = encodeConnectionStart(start);
    ASSERT_TRUE(decodeConnectionStart(args, decoded, error)) << error;
    EXPECT_EQ(decoded.version_major, 0U);
    EXPECT_EQ(decoded.version_minor, 9U);
    EXPECT_EQ(decoded.mechanisms, "PLAIN AMQPLAIN");
    EXPECT_EQ(decoded.locales, "en_US zh_CN");
    const std::string* product = decoded.server_properties.findString("product");
    ASSERT_NE(product, nullptr);
    EXPECT_EQ(*product, "FyMQ");
}

TEST(ConnectionMethodsTest, StartOkRoundTrip) {
    ConnectionStartOk start_ok;
    start_ok.client_properties.addString("product", "pika");
    start_ok.mechanism = "PLAIN";
    start_ok.response = std::string("\0guest\0guest", 12);
    start_ok.locale = "en_US";

    ConnectionStartOk decoded;
    std::string error;
    ASSERT_TRUE(decodeConnectionStartOk(encodeConnectionStartOk(start_ok),
                                        decoded, error))
        << error;
    EXPECT_EQ(decoded.mechanism, "PLAIN");
    EXPECT_EQ(decoded.response, start_ok.response);
    EXPECT_EQ(decoded.locale, "en_US");
    const std::string* product = decoded.client_properties.findString("product");
    ASSERT_NE(product, nullptr);
    EXPECT_EQ(*product, "pika");
}

TEST(ConnectionMethodsTest, TuneRoundTrip) {
    ConnectionTune tune;
    tune.channel_max = 2047;
    tune.frame_max = 131072;
    tune.heartbeat = 60;

    ConnectionTune decoded;
    std::string error;
    ASSERT_TRUE(decodeConnectionTune(encodeConnectionTune(tune), decoded, error))
        << error;
    EXPECT_EQ(decoded.channel_max, 2047U);
    EXPECT_EQ(decoded.frame_max, 131072U);
    EXPECT_EQ(decoded.heartbeat, 60U);
}

TEST(ConnectionMethodsTest, OpenAndCloseRoundTrip) {
    ConnectionOpen open;
    open.virtual_host = "/";
    ConnectionOpen decoded_open;
    std::string error;
    ASSERT_TRUE(decodeConnectionOpen(encodeConnectionOpen(open), decoded_open,
                                     error))
        << error;
    EXPECT_EQ(decoded_open.virtual_host, "/");

    ConnectionClose close;
    close.reply_code = 501;
    close.reply_text = "frame error";
    close.class_id = 10;
    close.method_id = 10;
    ConnectionClose decoded_close;
    ASSERT_TRUE(decodeConnectionClose(encodeConnectionClose(close),
                                      decoded_close, error))
        << error;
    EXPECT_EQ(decoded_close.reply_code, 501U);
    EXPECT_EQ(decoded_close.reply_text, "frame error");
    EXPECT_EQ(decoded_close.class_id, 10U);
    EXPECT_EQ(decoded_close.method_id, 10U);
}

}  // namespace mq::amqp091

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
