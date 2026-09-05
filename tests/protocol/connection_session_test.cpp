#include "mq/protocol/amqp091/connection_session.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace mq::amqp091 {

namespace {

std::string encodeMethodFrame(uint16_t class_id, uint16_t method_id,
                              const std::string& arguments) {
    const std::string method_payload =
        encodeMethodHeader(class_id, method_id, arguments);
    return FrameEncoder::encode(
        Frame{kFrameMethod, 0, method_payload}, 131072);
}

bool decodeCapturedMethod(const std::string& frame_bytes, MethodHeader& header,
                          std::string& error) {
    if (frame_bytes.size() < 8) return false;
    const uint32_t payload_size =
        (static_cast<uint32_t>(static_cast<uint8_t>(frame_bytes[3])) << 24) |
        (static_cast<uint32_t>(static_cast<uint8_t>(frame_bytes[4])) << 16) |
        (static_cast<uint32_t>(static_cast<uint8_t>(frame_bytes[5])) << 8) |
        static_cast<uint32_t>(static_cast<uint8_t>(frame_bytes[6]));
    if (7U + payload_size + 1U != frame_bytes.size()) return false;
    return decodeMethodHeader(
        std::string_view(frame_bytes).substr(7, payload_size), header, error);
}

ConnectionStartOk makeStartOk() {
    ConnectionStartOk start_ok;
    start_ok.client_properties.addString("product", "pika");
    start_ok.mechanism = "PLAIN";
    start_ok.response = std::string("\0guest\0guest", 12);
    start_ok.locale = "en_US";
    return start_ok;
}

}  // namespace

TEST(ConnectionSessionTest, CompletesHandshakeAndClose) {
    std::vector<std::string> sent;
    ConnectionConfig config;
    ConnectionSession session(config, [&](const std::string& bytes) {
        sent.push_back(bytes);
    });

    SessionResult result = session.feed(kAmqp091ProtocolHeader);
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(sent.size(), 1U);
    MethodHeader start_header;
    std::string error;
    ASSERT_TRUE(decodeCapturedMethod(sent[0], start_header, error)) << error;
    EXPECT_EQ(start_header.class_id, kConnectionClassId);
    EXPECT_EQ(start_header.method_id,
              static_cast<uint16_t>(ConnectionMethodId::Start));

    result = session.feed(encodeMethodFrame(
        kConnectionClassId,
        static_cast<uint16_t>(ConnectionMethodId::StartOk),
        encodeConnectionStartOk(makeStartOk())));
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(sent.size(), 2U);
    MethodHeader tune_header;
    ASSERT_TRUE(decodeCapturedMethod(sent[1], tune_header, error)) << error;
    EXPECT_EQ(tune_header.method_id,
              static_cast<uint16_t>(ConnectionMethodId::Tune));

    ConnectionTune tune_ok;
    tune_ok.channel_max = 1024;
    tune_ok.frame_max = 65536;
    tune_ok.heartbeat = 30;
    ConnectionOpen open;
    open.virtual_host = "/";
    result = session.feed(
        encodeMethodFrame(
            kConnectionClassId,
            static_cast<uint16_t>(ConnectionMethodId::TuneOk),
            encodeConnectionTune(tune_ok)) +
        encodeMethodFrame(
            kConnectionClassId,
            static_cast<uint16_t>(ConnectionMethodId::Open),
            encodeConnectionOpen(open)));
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(sent.size(), 3U);
    MethodHeader open_ok_header;
    ASSERT_TRUE(decodeCapturedMethod(sent[2], open_ok_header, error)) << error;
    EXPECT_EQ(open_ok_header.method_id,
              static_cast<uint16_t>(ConnectionMethodId::OpenOk));
    EXPECT_EQ(session.state(), ConnectionState::kReady);
    EXPECT_EQ(session.frameMax(), 65536U);
    EXPECT_EQ(session.channelMax(), 1024U);
    EXPECT_EQ(session.heartbeat(), 30U);

    ConnectionClose close;
    close.reply_code = 200;
    close.reply_text = "bye";
    result = session.feed(encodeMethodFrame(
        kConnectionClassId, static_cast<uint16_t>(ConnectionMethodId::Close),
        encodeConnectionClose(close)));
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(sent.size(), 4U);
    EXPECT_EQ(session.state(), ConnectionState::kClosed);
}

TEST(ConnectionSessionTest, RejectsInvalidHeader) {
    ConnectionSession session(ConnectionConfig{}, nullptr);
    const SessionResult result =
        session.feed(std::string_view("NOTAMQP!", 8));
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.empty());
    EXPECT_EQ(result.reply_code, 501U);
}

TEST(ConnectionSessionTest, RejectsWrongPassword) {
    std::vector<std::string> sent;
    ConnectionSession session(ConnectionConfig{}, [&](const std::string& bytes) {
        sent.push_back(bytes);
    });
    ASSERT_TRUE(session.feed(kAmqp091ProtocolHeader).ok);

    ConnectionStartOk start_ok = makeStartOk();
    start_ok.response = std::string("\0guest\0wrong", 12);
    const SessionResult result = session.feed(encodeMethodFrame(
        kConnectionClassId,
        static_cast<uint16_t>(ConnectionMethodId::StartOk),
        encodeConnectionStartOk(start_ok)));
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, "authentication failed");
    EXPECT_EQ(result.reply_code, 403U);
    ASSERT_EQ(sent.size(), 2U);
    MethodHeader close_header;
    std::string error;
    ASSERT_TRUE(decodeCapturedMethod(sent[1], close_header, error)) << error;
    EXPECT_EQ(close_header.method_id,
              static_cast<uint16_t>(ConnectionMethodId::Close));
    ConnectionClose close;
    ASSERT_TRUE(decodeConnectionClose(close_header.arguments, close, error))
        << error;
    EXPECT_EQ(close.reply_code, 403U);
}

}  // namespace mq::amqp091

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
