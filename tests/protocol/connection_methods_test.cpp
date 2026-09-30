#include "mq/protocol/amqp091/connection_methods.hpp"

#include <gtest/gtest.h>

namespace mq::amqp091 {

TEST(ConnectionMethodsTest, RoundTripsHandshakeMethods) {
    std::string error;

    const std::string payload =
        encodeMethodHeader(10, 10, std::string("\x01\x02\x03", 3));
    MethodHeader header;
    ASSERT_TRUE(decodeMethodHeader(payload, header, error)) << error;
    EXPECT_EQ(header.class_id, 10U);
    EXPECT_EQ(header.method_id, 10U);
    EXPECT_EQ(header.arguments, std::string("\x01\x02\x03", 3));

    ConnectionStart start;
    start.server_properties.addString("product", "FyMQ");
    start.mechanisms = "PLAIN AMQPLAIN";
    start.locales = "en_US zh_CN";
    ConnectionStart decoded_start;
    ASSERT_TRUE(decodeConnectionStart(encodeConnectionStart(start),
                                      decoded_start, error))
        << error;
    EXPECT_EQ(decoded_start.version_major, 0U);
    EXPECT_EQ(decoded_start.version_minor, 9U);
    EXPECT_EQ(decoded_start.mechanisms, "PLAIN AMQPLAIN");
    EXPECT_EQ(decoded_start.locales, "en_US zh_CN");
    const std::string* product = decoded_start.server_properties.findString("product");
    ASSERT_NE(product, nullptr);
    EXPECT_EQ(*product, "FyMQ");

    ConnectionStartOk start_ok;
    start_ok.client_properties.addString("product", "pika");
    start_ok.mechanism = "PLAIN";
    start_ok.response = std::string("\0guest\0guest", 12);
    start_ok.locale = "en_US";
    ConnectionStartOk decoded_ok;
    ASSERT_TRUE(decodeConnectionStartOk(encodeConnectionStartOk(start_ok),
                                        decoded_ok, error))
        << error;
    EXPECT_EQ(decoded_ok.mechanism, "PLAIN");
    EXPECT_EQ(decoded_ok.response, start_ok.response);
    EXPECT_EQ(decoded_ok.locale, "en_US");
    const std::string* client_product =
        decoded_ok.client_properties.findString("product");
    ASSERT_NE(client_product, nullptr);
    EXPECT_EQ(*client_product, "pika");

    ConnectionTune tune;
    tune.channel_max = 2047;
    tune.frame_max = 131072;
    tune.heartbeat = 60;
    ConnectionTune decoded_tune;
    ASSERT_TRUE(
        decodeConnectionTune(encodeConnectionTune(tune), decoded_tune, error))
        << error;
    EXPECT_EQ(decoded_tune.channel_max, 2047U);
    EXPECT_EQ(decoded_tune.frame_max, 131072U);
    EXPECT_EQ(decoded_tune.heartbeat, 60U);
}

TEST(ConnectionMethodsTest, RoundTripsOpenAndClose) {
    std::string error;

    ConnectionOpen open;
    open.virtual_host = "/";
    ConnectionOpen decoded_open;
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
