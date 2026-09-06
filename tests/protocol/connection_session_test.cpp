#include "mq/protocol/amqp091/connection_session.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>
#include <vector>

namespace mq::amqp091 {

namespace {

std::string encodeMethodFrame(uint16_t class_id, uint16_t method_id,
                              const std::string& arguments,
                              uint16_t channel = 0) {
    const std::string method_payload =
        encodeMethodHeader(class_id, method_id, arguments);
    return FrameEncoder::encode(
        Frame{kFrameMethod, channel, method_payload}, 131072);
}

std::string encodeRawFrame(uint8_t type, uint16_t channel,
                           const std::string& payload) {
    return FrameEncoder::encode(Frame{type, channel, payload}, 131072);
}

std::string encodeExpirationContentHeader(uint64_t body_size,
                                          const std::string& expiration) {
    std::string payload;
    payload.append("\x00\x3c\x00\x00", 4);  // class-id 60, weight 0
    for (int shift = 56; shift >= 0; shift -= 8) {
        payload.push_back(static_cast<char>(
            (body_size >> shift) & 0xff));
    }
    const uint16_t expiration_flag = 0x8000U >> 7;
    payload.push_back(static_cast<char>((expiration_flag >> 8) & 0xff));
    payload.push_back(static_cast<char>(expiration_flag & 0xff));
    payload.push_back(static_cast<char>(expiration.size()));
    payload.append(expiration);
    return payload;
}

bool decodeCapturedMethod(const std::string& frame_bytes, MethodHeader& header,
                          std::string& error,
                          uint16_t* channel_out = nullptr) {
    if (frame_bytes.size() < 8) return false;
    if (channel_out != nullptr) {
        *channel_out =
            (static_cast<uint16_t>(static_cast<uint8_t>(frame_bytes[1])) << 8) |
            static_cast<uint16_t>(static_cast<uint8_t>(frame_bytes[2]));
    }
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

void establishReady(ConnectionSession& session,
                    std::vector<std::string>& sent) {
    SessionResult result = session.feed(kAmqp091ProtocolHeader);
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(sent.size(), 1U);

    result = session.feed(encodeMethodFrame(
        kConnectionClassId,
        static_cast<uint16_t>(ConnectionMethodId::StartOk),
        encodeConnectionStartOk(makeStartOk())));
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(sent.size(), 2U);

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
    ASSERT_EQ(session.state(), ConnectionState::kReady);
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

TEST(ConnectionSessionTest, OpensAndClosesChannel) {
    std::vector<std::string> sent;
    ConnectionSession session(ConnectionConfig{}, [&](const std::string& bytes) {
        sent.push_back(bytes);
    });
    establishReady(session, sent);

    const uint16_t channel = 1;
    SessionResult result = session.feed(encodeMethodFrame(
        kChannelClassId, static_cast<uint16_t>(ChannelMethodId::Open),
        encodeChannelOpen(ChannelOpen{}), channel));
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(sent.size(), 4U);
    ASSERT_TRUE(session.isChannelOpen(channel));
    EXPECT_EQ(session.openChannelCount(), 1U);

    MethodHeader header;
    uint16_t response_channel = 0;
    std::string error;
    ASSERT_TRUE(decodeCapturedMethod(sent[3], header, error,
                                     &response_channel))
        << error;
    EXPECT_EQ(response_channel, channel);
    EXPECT_EQ(header.class_id, kChannelClassId);
    EXPECT_EQ(header.method_id,
              static_cast<uint16_t>(ChannelMethodId::OpenOk));

    ChannelClose close;
    close.reply_code = 200;
    close.reply_text = "done";
    result = session.feed(encodeMethodFrame(
        kChannelClassId, static_cast<uint16_t>(ChannelMethodId::Close),
        encodeChannelClose(close), channel));
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(sent.size(), 5U);
    EXPECT_FALSE(session.isChannelOpen(channel));
    EXPECT_EQ(session.openChannelCount(), 0U);
    EXPECT_EQ(session.state(), ConnectionState::kReady);
}

TEST(ConnectionSessionTest, ChannelErrorDoesNotCloseConnection) {
    std::vector<std::string> sent;
    ConnectionSession session(ConnectionConfig{}, [&](const std::string& bytes) {
        sent.push_back(bytes);
    });
    establishReady(session, sent);

    const uint16_t channel = 1;
    ASSERT_TRUE(session
                    .feed(encodeMethodFrame(
                        kChannelClassId,
                        static_cast<uint16_t>(ChannelMethodId::Open),
                        encodeChannelOpen(ChannelOpen{}), channel))
                    .ok);
    ASSERT_EQ(sent.size(), 4U);

    // basic.publish is not implemented yet: it must close only the channel.
    const uint16_t basic_class = 60;
    const uint16_t publish_method = 40;
    const SessionResult result = session.feed(encodeMethodFrame(
        basic_class, publish_method, std::string(), channel));
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(sent.size(), 5U);
    EXPECT_EQ(session.state(), ConnectionState::kReady);
    EXPECT_FALSE(session.isChannelOpen(channel));

    MethodHeader header;
    std::string error;
    ASSERT_TRUE(decodeCapturedMethod(sent[4], header, error)) << error;
    EXPECT_EQ(header.class_id, kChannelClassId);
    EXPECT_EQ(header.method_id,
              static_cast<uint16_t>(ChannelMethodId::Close));
    ChannelClose channel_close;
    ASSERT_TRUE(decodeChannelClose(header.arguments, channel_close, error))
        << error;
    EXPECT_EQ(channel_close.reply_code, 540U);

    // Complete the channel close handshake and verify the connection survives.
    ASSERT_TRUE(session
                    .feed(encodeMethodFrame(
                        kChannelClassId,
                        static_cast<uint16_t>(ChannelMethodId::CloseOk), "",
                        channel))
                    .ok);
    EXPECT_EQ(session.openChannelCount(), 0U);
    EXPECT_EQ(session.state(), ConnectionState::kReady);
}

TEST(ConnectionSessionTest, RejectsMethodOnUnopenedChannel) {
    std::vector<std::string> sent;
    ConnectionSession session(ConnectionConfig{}, [&](const std::string& bytes) {
        sent.push_back(bytes);
    });
    establishReady(session, sent);

    const uint16_t channel = 2;
    const SessionResult result = session.feed(encodeMethodFrame(
        kChannelClassId, static_cast<uint16_t>(ChannelMethodId::Flow),
        encodeChannelFlow(ChannelFlow{false}), channel));
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(sent.size(), 4U);
    EXPECT_EQ(session.state(), ConnectionState::kReady);

    MethodHeader header;
    std::string error;
    ASSERT_TRUE(decodeCapturedMethod(sent[3], header, error)) << error;
    ChannelClose close;
    ASSERT_TRUE(decodeChannelClose(header.arguments, close, error)) << error;
    EXPECT_EQ(close.reply_code, 504U);
}

TEST(ConnectionSessionTest, RejectsDuplicateChannelOpen) {
    std::vector<std::string> sent;
    ConnectionSession session(ConnectionConfig{}, [&](const std::string& bytes) {
        sent.push_back(bytes);
    });
    establishReady(session, sent);

    const uint16_t channel = 1;
    ASSERT_TRUE(session
                    .feed(encodeMethodFrame(
                        kChannelClassId,
                        static_cast<uint16_t>(ChannelMethodId::Open),
                        encodeChannelOpen(ChannelOpen{}), channel))
                    .ok);
    const SessionResult result = session.feed(encodeMethodFrame(
        kChannelClassId, static_cast<uint16_t>(ChannelMethodId::Open),
        encodeChannelOpen(ChannelOpen{}), channel));
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(sent.size(), 5U);

    MethodHeader header;
    std::string error;
    ASSERT_TRUE(decodeCapturedMethod(sent[4], header, error)) << error;
    ChannelClose close;
    ASSERT_TRUE(decodeChannelClose(header.arguments, close, error)) << error;
    EXPECT_EQ(close.reply_code, 504U);
    EXPECT_EQ(session.state(), ConnectionState::kReady);
}

TEST(ConnectionSessionTest, DeclaresExchangeQueueAndBind) {
    auto host = std::make_shared<broker::VirtualHost>();
    std::vector<std::string> sent;
    ConnectionSession session(ConnectionConfig{}, [&](const std::string& bytes) {
        sent.push_back(bytes);
    }, host);
    establishReady(session, sent);

    const uint16_t channel = 1;
    ASSERT_TRUE(session
                    .feed(encodeMethodFrame(
                        kChannelClassId,
                        static_cast<uint16_t>(ChannelMethodId::Open),
                        encodeChannelOpen(ChannelOpen{}), channel))
                    .ok);
    ASSERT_EQ(sent.size(), 4U);

    ExchangeDeclare exchange;
    exchange.exchange = "logs";
    exchange.type = "fanout";
    exchange.durable = true;
    ASSERT_TRUE(session
                    .feed(encodeMethodFrame(
                        kExchangeClassId,
                        static_cast<uint16_t>(ExchangeMethodId::Declare),
                        encodeExchangeDeclare(exchange), channel))
                    .ok);
    ASSERT_EQ(sent.size(), 5U);
    ASSERT_TRUE(host->hasExchange("logs"));

    QueueDeclare queue;
    queue.queue = "task_queue";
    queue.durable = true;
    queue.arguments.addString("x-dead-letter-exchange", "dlx");
    queue.arguments.addString("x-dead-letter-routing-key", "dead.rk");
    queue.arguments.addInt32("x-message-ttl", 60000);
    ASSERT_TRUE(session
                    .feed(encodeMethodFrame(
                        kQueueClassId,
                        static_cast<uint16_t>(QueueMethodId::Declare),
                        encodeQueueDeclare(queue), channel))
                    .ok);
    ASSERT_EQ(sent.size(), 6U);
    EXPECT_EQ(host->deadLetterExchange("task_queue"), "dlx");
    EXPECT_EQ(host->messageTtl("task_queue"), 60000);

    QueueBind bind;
    bind.queue = "task_queue";
    bind.exchange = "logs";
    bind.routing_key = "task";
    ASSERT_TRUE(session
                    .feed(encodeMethodFrame(
                        kQueueClassId,
                        static_cast<uint16_t>(QueueMethodId::Bind),
                        encodeQueueBind(bind), channel))
                    .ok);
    ASSERT_EQ(sent.size(), 7U);
    EXPECT_EQ(host->bindingCount("logs", "task_queue"), 1U);

    MethodHeader header;
    uint16_t response_channel = 0;
    std::string error;
    ASSERT_TRUE(decodeCapturedMethod(sent[4], header, error,
                                     &response_channel))
        << error;
    EXPECT_EQ(response_channel, channel);
    EXPECT_EQ(header.class_id, kExchangeClassId);
    EXPECT_EQ(header.method_id,
              static_cast<uint16_t>(ExchangeMethodId::DeclareOk));

    ASSERT_TRUE(decodeCapturedMethod(sent[5], header, error,
                                     &response_channel))
        << error;
    EXPECT_EQ(header.class_id, kQueueClassId);
    EXPECT_EQ(header.method_id,
              static_cast<uint16_t>(QueueMethodId::DeclareOk));
    QueueDeclareOk declare_ok;
    ASSERT_TRUE(decodeQueueDeclareOk(header.arguments, declare_ok, error))
        << error;
    EXPECT_EQ(declare_ok.queue, "task_queue");

    ASSERT_TRUE(decodeCapturedMethod(sent[6], header, error,
                                     &response_channel))
        << error;
    EXPECT_EQ(header.class_id, kQueueClassId);
    EXPECT_EQ(header.method_id,
              static_cast<uint16_t>(QueueMethodId::BindOk));
}

TEST(ConnectionSessionTest, PassiveDeclareUsesChannelErrorNotConnectionClose) {
    auto host = std::make_shared<broker::VirtualHost>();
    std::vector<std::string> sent;
    ConnectionSession session(ConnectionConfig{}, [&](const std::string& bytes) {
        sent.push_back(bytes);
    }, host);
    establishReady(session, sent);

    const uint16_t channel = 1;
    ASSERT_TRUE(session
                    .feed(encodeMethodFrame(
                        kChannelClassId,
                        static_cast<uint16_t>(ChannelMethodId::Open),
                        encodeChannelOpen(ChannelOpen{}), channel))
                    .ok);

    QueueDeclare queue;
    queue.queue = "missing_queue";
    queue.passive = true;
    const SessionResult result = session.feed(encodeMethodFrame(
        kQueueClassId, static_cast<uint16_t>(QueueMethodId::Declare),
        encodeQueueDeclare(queue), channel));
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(session.state(), ConnectionState::kReady);
    EXPECT_FALSE(host->hasQueue("missing_queue"));

    MethodHeader header;
    std::string error;
    ASSERT_TRUE(decodeCapturedMethod(sent.back(), header, error)) << error;
    EXPECT_EQ(header.class_id, kChannelClassId);
    ChannelClose close;
    ASSERT_TRUE(decodeChannelClose(header.arguments, close, error)) << error;
    EXPECT_EQ(close.reply_code, 404U);
}

TEST(ConnectionSessionTest, PublishesMessageIntoQueue) {
    auto host = std::make_shared<broker::VirtualHost>();
    std::vector<std::string> sent;
    ConnectionSession session(ConnectionConfig{}, [&](const std::string& bytes) {
        sent.push_back(bytes);
    }, host);
    establishReady(session, sent);

    ASSERT_TRUE(host->declareQueue(
                    broker::QueueSpec{"q1", false, false, false})
                    .ok);
    const uint16_t channel = 1;
    ASSERT_TRUE(session
                    .feed(encodeMethodFrame(
                        kChannelClassId,
                        static_cast<uint16_t>(ChannelMethodId::Open),
                        encodeChannelOpen(ChannelOpen{}), channel))
                    .ok);

    BasicPublish publish;
    publish.exchange = "";       // default exchange
    publish.routing_key = "q1";  // queue name
    const std::string body = "hello";
    const SessionResult result = session.feed(
        encodeMethodFrame(
            kBasicClassId,
            static_cast<uint16_t>(BasicMethodId::Publish),
            encodeBasicPublish(publish), channel) +
        encodeRawFrame(kFrameHeader, channel, encodeContentHeader(body.size())) +
        encodeRawFrame(kFrameBody, channel, body));
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(host->messageCount("q1"), 1U);
    EXPECT_EQ(session.state(), ConnectionState::kReady);
}

TEST(ConnectionSessionTest, MandatoryUnroutableReturnsMessage) {
    auto host = std::make_shared<broker::VirtualHost>();
    std::vector<std::string> sent;
    ConnectionSession session(ConnectionConfig{}, [&](const std::string& bytes) {
        sent.push_back(bytes);
    }, host);
    establishReady(session, sent);

    ASSERT_TRUE(host
                    ->declareExchange(
                        broker::ExchangeSpec{"ex", "direct", false, false, false})
                    .ok);
    const uint16_t channel = 1;
    ASSERT_TRUE(session
                    .feed(encodeMethodFrame(
                        kChannelClassId,
                        static_cast<uint16_t>(ChannelMethodId::Open),
                        encodeChannelOpen(ChannelOpen{}), channel))
                    .ok);
    const size_t before = sent.size();

    BasicPublish publish;
    publish.exchange = "ex";
    publish.routing_key = "missing";
    publish.mandatory = true;
    const std::string body = "lost";
    const SessionResult result = session.feed(
        encodeMethodFrame(
            kBasicClassId,
            static_cast<uint16_t>(BasicMethodId::Publish),
            encodeBasicPublish(publish), channel) +
        encodeRawFrame(kFrameHeader, channel, encodeContentHeader(body.size())) +
        encodeRawFrame(kFrameBody, channel, body));
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(sent.size(), before + 3U);

    MethodHeader header;
    std::string error;
    ASSERT_TRUE(decodeCapturedMethod(sent[before], header, error)) << error;
    EXPECT_EQ(header.class_id, kBasicClassId);
    EXPECT_EQ(header.method_id,
              static_cast<uint16_t>(BasicMethodId::Return));
    EXPECT_EQ(session.state(), ConnectionState::kReady);
}

TEST(ConnectionSessionTest, ConsumeReceivesDeliveredMessage) {
    auto host = std::make_shared<broker::VirtualHost>();
    std::vector<std::string> sent;
    ConnectionSession session(ConnectionConfig{}, [&](const std::string& bytes) {
        sent.push_back(bytes);
    }, host);
    establishReady(session, sent);
    ASSERT_TRUE(host->declareQueue(
                    broker::QueueSpec{"q1", false, false, false})
                    .ok);

    const uint16_t channel = 1;
    ASSERT_TRUE(session
                    .feed(encodeMethodFrame(
                        kChannelClassId,
                        static_cast<uint16_t>(ChannelMethodId::Open),
                        encodeChannelOpen(ChannelOpen{}), channel))
                    .ok);
    const size_t before_consume = sent.size();

    BasicConsume consume;
    consume.queue = "q1";
    consume.no_ack = true;
    const SessionResult consume_result = session.feed(encodeMethodFrame(
        kBasicClassId, static_cast<uint16_t>(BasicMethodId::Consume),
        encodeBasicConsume(consume), channel));
    ASSERT_TRUE(consume_result.ok) << consume_result.error;
    ASSERT_EQ(sent.size(), before_consume + 1U);
    MethodHeader header;
    std::string error;
    ASSERT_TRUE(decodeCapturedMethod(sent[before_consume], header, error))
        << error;
    EXPECT_EQ(header.method_id,
              static_cast<uint16_t>(BasicMethodId::ConsumeOk));

    const size_t before_publish = sent.size();
    BasicPublish publish;
    publish.exchange = "";
    publish.routing_key = "q1";
    const std::string body = "hello";
    const SessionResult publish_result = session.feed(
        encodeMethodFrame(
            kBasicClassId,
            static_cast<uint16_t>(BasicMethodId::Publish),
            encodeBasicPublish(publish), channel) +
        encodeRawFrame(kFrameHeader, channel, encodeContentHeader(body.size())) +
        encodeRawFrame(kFrameBody, channel, body));
    ASSERT_TRUE(publish_result.ok) << publish_result.error;
    EXPECT_EQ(host->messageCount("q1"), 0U);
    ASSERT_EQ(sent.size(), before_publish + 3U);

    ASSERT_TRUE(decodeCapturedMethod(sent[before_publish], header, error))
        << error;
    EXPECT_EQ(header.class_id, kBasicClassId);
    EXPECT_EQ(header.method_id,
              static_cast<uint16_t>(BasicMethodId::Deliver));
    BasicDeliver deliver;
    ASSERT_TRUE(decodeBasicDeliver(header.arguments, deliver, error)) << error;
    EXPECT_EQ(deliver.delivery_tag, 1U);
    EXPECT_EQ(deliver.exchange, "");
    EXPECT_EQ(deliver.routing_key, "q1");
    EXPECT_EQ(static_cast<unsigned char>(sent[before_publish + 1][0]),
              kFrameHeader);
    EXPECT_EQ(static_cast<unsigned char>(sent[before_publish + 2][0]),
              kFrameBody);
}

TEST(ConnectionSessionTest, AckAndRejectControlDelivery) {
    auto host = std::make_shared<broker::VirtualHost>();
    std::vector<std::string> sent;
    ConnectionSession session(ConnectionConfig{}, [&](const std::string& bytes) {
        sent.push_back(bytes);
    }, host);
    establishReady(session, sent);
    ASSERT_TRUE(host->declareQueue(
                    broker::QueueSpec{"q1", false, false, false})
                    .ok);

    const uint16_t channel = 1;
    ASSERT_TRUE(session
                    .feed(encodeMethodFrame(
                        kChannelClassId,
                        static_cast<uint16_t>(ChannelMethodId::Open),
                        encodeChannelOpen(ChannelOpen{}), channel))
                    .ok);
    BasicConsume consume;
    consume.queue = "q1";
    consume.no_ack = false;
    ASSERT_TRUE(session
                    .feed(encodeMethodFrame(
                        kBasicClassId,
                        static_cast<uint16_t>(BasicMethodId::Consume),
                        encodeBasicConsume(consume), channel))
                    .ok);

    auto publish_one = [&](const std::string& body) {
        BasicPublish publish;
        publish.exchange = "";
        publish.routing_key = "q1";
        return session.feed(
            encodeMethodFrame(
                kBasicClassId,
                static_cast<uint16_t>(BasicMethodId::Publish),
                encodeBasicPublish(publish), channel) +
            encodeRawFrame(kFrameHeader, channel,
                           encodeContentHeader(body.size())) +
            encodeRawFrame(kFrameBody, channel, body));
    };

    // First message: deliver then ack.
    ASSERT_TRUE(publish_one("first").ok);
    EXPECT_EQ(host->unackedCount(), 1U);
    MethodHeader header;
    std::string error;
    ASSERT_TRUE(decodeCapturedMethod(sent[sent.size() - 3], header, error))
        << error;
    BasicDeliver first_deliver;
    ASSERT_TRUE(decodeBasicDeliver(header.arguments, first_deliver, error))
        << error;
    EXPECT_FALSE(first_deliver.redelivered);

    BasicAck ack;
    ack.delivery_tag = first_deliver.delivery_tag;
    ASSERT_TRUE(session
                    .feed(encodeMethodFrame(
                        kBasicClassId,
                        static_cast<uint16_t>(BasicMethodId::Ack),
                        encodeBasicAck(ack), channel))
                    .ok);
    EXPECT_EQ(host->unackedCount(), 0U);

    // Second message: reject with requeue => immediate redelivery.
    ASSERT_TRUE(publish_one("second").ok);
    EXPECT_EQ(host->unackedCount(), 1U);
    ASSERT_TRUE(decodeCapturedMethod(sent[sent.size() - 3], header, error))
        << error;
    BasicDeliver second_deliver;
    ASSERT_TRUE(decodeBasicDeliver(header.arguments, second_deliver, error))
        << error;

    BasicReject reject;
    reject.delivery_tag = second_deliver.delivery_tag;
    reject.requeue = true;
    ASSERT_TRUE(session
                    .feed(encodeMethodFrame(
                        kBasicClassId,
                        static_cast<uint16_t>(BasicMethodId::Reject),
                        encodeBasicReject(reject), channel))
                    .ok);
    ASSERT_TRUE(decodeCapturedMethod(sent[sent.size() - 3], header, error))
        << error;
    BasicDeliver redelivered;
    ASSERT_TRUE(decodeBasicDeliver(header.arguments, redelivered, error))
        << error;
    EXPECT_TRUE(redelivered.redelivered);
    EXPECT_EQ(host->unackedCount(), 1U);

    // Clean up: ack the redelivered message.
    BasicAck final_ack;
    final_ack.delivery_tag = redelivered.delivery_tag;
    ASSERT_TRUE(session
                    .feed(encodeMethodFrame(
                        kBasicClassId,
                        static_cast<uint16_t>(BasicMethodId::Ack),
                        encodeBasicAck(final_ack), channel))
                    .ok);
    EXPECT_EQ(host->unackedCount(), 0U);
}

TEST(ConnectionSessionTest, GetReturnsMessageAndEmpty) {
    auto host = std::make_shared<broker::VirtualHost>();
    std::vector<std::string> sent;
    ConnectionSession session(ConnectionConfig{}, [&](const std::string& bytes) {
        sent.push_back(bytes);
    }, host);
    establishReady(session, sent);
    ASSERT_TRUE(host->declareQueue(
                    broker::QueueSpec{"q1", false, false, false})
                    .ok);
    ASSERT_TRUE(host->publish("", "q1", broker::Message{"one", false}).ok);
    ASSERT_TRUE(host->publish("", "q1", broker::Message{"two", false}).ok);

    const uint16_t channel = 1;
    ASSERT_TRUE(session
                    .feed(encodeMethodFrame(
                        kChannelClassId,
                        static_cast<uint16_t>(ChannelMethodId::Open),
                        encodeChannelOpen(ChannelOpen{}), channel))
                    .ok);

    BasicGet get;
    get.queue = "q1";
    const size_t before_first = sent.size();
    ASSERT_TRUE(session
                    .feed(encodeMethodFrame(
                        kBasicClassId,
                        static_cast<uint16_t>(BasicMethodId::Get),
                        encodeBasicGet(get), channel))
                    .ok);
    ASSERT_EQ(sent.size(), before_first + 3U);
    MethodHeader header;
    std::string error;
    ASSERT_TRUE(decodeCapturedMethod(sent[before_first], header, error))
        << error;
    EXPECT_EQ(header.method_id,
              static_cast<uint16_t>(BasicMethodId::GetOk));
    BasicGetOk ok;
    ASSERT_TRUE(decodeBasicGetOk(header.arguments, ok, error)) << error;
    EXPECT_EQ(ok.message_count, 1U);
    EXPECT_EQ(host->unackedCount(), 1U);

    BasicAck ack;
    ack.delivery_tag = ok.delivery_tag;
    ASSERT_TRUE(session
                    .feed(encodeMethodFrame(
                        kBasicClassId,
                        static_cast<uint16_t>(BasicMethodId::Ack),
                        encodeBasicAck(ack), channel))
                    .ok);
    EXPECT_EQ(host->unackedCount(), 0U);

    const size_t before_second = sent.size();
    ASSERT_TRUE(session
                    .feed(encodeMethodFrame(
                        kBasicClassId,
                        static_cast<uint16_t>(BasicMethodId::Get),
                        encodeBasicGet(get), channel))
                    .ok);
    ASSERT_TRUE(decodeCapturedMethod(sent[before_second], header, error))
        << error;
    EXPECT_EQ(header.method_id,
              static_cast<uint16_t>(BasicMethodId::GetOk));

    const size_t before_empty = sent.size();
    ASSERT_TRUE(session
                    .feed(encodeMethodFrame(
                        kBasicClassId,
                        static_cast<uint16_t>(BasicMethodId::Get),
                        encodeBasicGet(get), channel))
                    .ok);
    ASSERT_TRUE(decodeCapturedMethod(sent[before_empty], header, error))
        << error;
    EXPECT_EQ(header.method_id,
              static_cast<uint16_t>(BasicMethodId::GetEmpty));
}

TEST(ConnectionSessionTest, PerMessageExpirationExpiresOnPurge) {
    auto host = std::make_shared<broker::VirtualHost>();
    std::vector<std::string> sent;
    ConnectionSession session(ConnectionConfig{}, [&](const std::string& bytes) {
        sent.push_back(bytes);
    }, host);
    establishReady(session, sent);
    ASSERT_TRUE(host->declareQueue(
                    broker::QueueSpec{"q1", false, false, false})
                    .ok);

    const uint16_t channel = 1;
    ASSERT_TRUE(session
                    .feed(encodeMethodFrame(
                        kChannelClassId,
                        static_cast<uint16_t>(ChannelMethodId::Open),
                        encodeChannelOpen(ChannelOpen{}), channel))
                    .ok);

    BasicPublish publish;
    publish.exchange = "";
    publish.routing_key = "q1";
    const std::string body = "short";
    const SessionResult result = session.feed(
        encodeMethodFrame(
            kBasicClassId,
            static_cast<uint16_t>(BasicMethodId::Publish),
            encodeBasicPublish(publish), channel) +
        encodeRawFrame(kFrameHeader, channel,
                       encodeExpirationContentHeader(body.size(), "20")) +
        encodeRawFrame(kFrameBody, channel, body));
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(host->messageCount("q1"), 1U);

    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    const broker::BrokerResult purged = host->purgeQueue("q1");
    ASSERT_TRUE(purged.ok) << purged.error;
    EXPECT_EQ(purged.count, 0U);
}

TEST(ConnectionSessionTest, QosPrefetchLimitsAndRefillsDelivery) {
    auto host = std::make_shared<broker::VirtualHost>();
    std::vector<std::string> sent;
    ConnectionSession session(ConnectionConfig{}, [&](const std::string& bytes) {
        sent.push_back(bytes);
    }, host);
    establishReady(session, sent);
    ASSERT_TRUE(host->declareQueue(
                    broker::QueueSpec{"q1", false, false, false})
                    .ok);

    const uint16_t channel = 1;
    ASSERT_TRUE(session
                    .feed(encodeMethodFrame(
                        kChannelClassId,
                        static_cast<uint16_t>(ChannelMethodId::Open),
                        encodeChannelOpen(ChannelOpen{}), channel))
                    .ok);
    BasicQos qos;
    qos.prefetch_count = 1;
    ASSERT_TRUE(session
                    .feed(encodeMethodFrame(
                        kBasicClassId,
                        static_cast<uint16_t>(BasicMethodId::Qos),
                        encodeBasicQos(qos), channel))
                    .ok);
    BasicConsume consume;
    consume.queue = "q1";
    consume.no_ack = false;
    ASSERT_TRUE(session
                    .feed(encodeMethodFrame(
                        kBasicClassId,
                        static_cast<uint16_t>(BasicMethodId::Consume),
                        encodeBasicConsume(consume), channel))
                    .ok);

    auto publish = [&](const std::string& body) {
        BasicPublish publish;
        publish.exchange = "";
        publish.routing_key = "q1";
        return session.feed(
            encodeMethodFrame(
                kBasicClassId,
                static_cast<uint16_t>(BasicMethodId::Publish),
                encodeBasicPublish(publish), channel) +
            encodeRawFrame(kFrameHeader, channel,
                           encodeContentHeader(body.size())) +
            encodeRawFrame(kFrameBody, channel, body));
    };

    ASSERT_TRUE(publish("one").ok);
    ASSERT_TRUE(publish("two").ok);
    EXPECT_EQ(host->unackedCount(), 1U);
    EXPECT_EQ(host->messageCount("q1"), 1U);

    MethodHeader header;
    std::string error;
    ASSERT_TRUE(decodeCapturedMethod(sent[sent.size() - 3], header, error))
        << error;
    BasicDeliver first;
    ASSERT_TRUE(decodeBasicDeliver(header.arguments, first, error)) << error;

    BasicAck ack;
    ack.delivery_tag = first.delivery_tag;
    ASSERT_TRUE(session
                    .feed(encodeMethodFrame(
                        kBasicClassId,
                        static_cast<uint16_t>(BasicMethodId::Ack),
                        encodeBasicAck(ack), channel))
                    .ok);
    EXPECT_EQ(host->unackedCount(), 1U);
    EXPECT_EQ(host->messageCount("q1"), 0U);
    ASSERT_TRUE(decodeCapturedMethod(sent[sent.size() - 3], header, error))
        << error;
    BasicDeliver second;
    ASSERT_TRUE(decodeBasicDeliver(header.arguments, second, error)) << error;
    EXPECT_NE(second.delivery_tag, first.delivery_tag);
}

TEST(ConnectionSessionTest, ConsumerTagIsScopedPerChannel) {
    auto host = std::make_shared<broker::VirtualHost>();
    std::vector<std::string> sent;
    ConnectionSession session(ConnectionConfig{}, [&](const std::string& bytes) {
        sent.push_back(bytes);
    }, host);
    establishReady(session, sent);
    ASSERT_TRUE(host->declareQueue(
                    broker::QueueSpec{"q1", false, false, false})
                    .ok);

    const auto consume_on = [&](uint16_t channel) {
        BasicConsume consume;
        consume.queue = "q1";
        consume.consumer_tag = "same";
        return session.feed(encodeMethodFrame(
            kBasicClassId,
            static_cast<uint16_t>(BasicMethodId::Consume),
            encodeBasicConsume(consume), channel));
    };
    ASSERT_TRUE(session
                    .feed(encodeMethodFrame(
                        kChannelClassId,
                        static_cast<uint16_t>(ChannelMethodId::Open),
                        encodeChannelOpen(ChannelOpen{}), 1))
                    .ok);
    ASSERT_TRUE(session
                    .feed(encodeMethodFrame(
                        kChannelClassId,
                        static_cast<uint16_t>(ChannelMethodId::Open),
                        encodeChannelOpen(ChannelOpen{}), 2))
                    .ok);
    ASSERT_TRUE(consume_on(1).ok);
    ASSERT_TRUE(consume_on(2).ok);

    BasicCancel cancel;
    cancel.consumer_tag = "same";
    ASSERT_TRUE(session
                    .feed(encodeMethodFrame(
                        kBasicClassId,
                        static_cast<uint16_t>(BasicMethodId::Cancel),
                        encodeBasicCancel(cancel), 1))
                    .ok);

    // Channel 2 still has its consumer with the same tag.
    BasicCancel cancel_two;
    cancel_two.consumer_tag = "same";
    ASSERT_TRUE(session
                    .feed(encodeMethodFrame(
                        kBasicClassId,
                        static_cast<uint16_t>(BasicMethodId::Cancel),
                        encodeBasicCancel(cancel_two), 2))
                    .ok);
}

TEST(ConnectionSessionTest, ConfirmSelectAcksPublish) {
    auto host = std::make_shared<broker::VirtualHost>();
    std::vector<std::string> sent;
    ConnectionSession session(ConnectionConfig{}, [&](const std::string& bytes) {
        sent.push_back(bytes);
    }, host);
    establishReady(session, sent);
    ASSERT_TRUE(host->declareQueue(
                    broker::QueueSpec{"q1", false, false, false})
                    .ok);

    const uint16_t channel = 1;
    ASSERT_TRUE(session
                    .feed(encodeMethodFrame(
                        kChannelClassId,
                        static_cast<uint16_t>(ChannelMethodId::Open),
                        encodeChannelOpen(ChannelOpen{}), channel))
                    .ok);
    ConfirmSelect select;
    ASSERT_TRUE(session
                    .feed(encodeMethodFrame(
                        kConfirmClassId,
                        static_cast<uint16_t>(ConfirmMethodId::Select),
                        encodeConfirmSelect(select), channel))
                    .ok);
    ASSERT_EQ(host->messageCount("q1"), 0U);

    BasicPublish publish;
    publish.exchange = "";
    publish.routing_key = "q1";
    const std::string body = "confirmed";
    const size_t before = sent.size();
    ASSERT_TRUE(session
                    .feed(encodeMethodFrame(
                        kBasicClassId,
                        static_cast<uint16_t>(BasicMethodId::Publish),
                        encodeBasicPublish(publish), channel) +
                        encodeRawFrame(
                            kFrameHeader, channel,
                            encodeContentHeader(body.size())) +
                        encodeRawFrame(kFrameBody, channel, body))
                    .ok);
    EXPECT_EQ(host->messageCount("q1"), 1U);
    ASSERT_GT(sent.size(), before);

    MethodHeader header;
    std::string error;
    ASSERT_TRUE(decodeCapturedMethod(sent.back(), header, error)) << error;
    EXPECT_EQ(header.class_id, kBasicClassId);
    EXPECT_EQ(header.method_id, static_cast<uint16_t>(BasicMethodId::Ack));
    BasicAck confirm_ack;
    ASSERT_TRUE(decodeBasicAck(header.arguments, confirm_ack, error)) << error;
    EXPECT_EQ(confirm_ack.delivery_tag, 1U);
}

TEST(ConnectionSessionTest, DeliverPreservesContentHeader) {
    auto host = std::make_shared<broker::VirtualHost>();
    std::vector<std::string> sent;
    ConnectionSession session(ConnectionConfig{}, [&](const std::string& bytes) {
        sent.push_back(bytes);
    }, host);
    establishReady(session, sent);
    ASSERT_TRUE(host->declareQueue(
                    broker::QueueSpec{"q1", false, false, false})
                    .ok);

    const uint16_t channel = 1;
    ASSERT_TRUE(session
                    .feed(encodeMethodFrame(
                        kChannelClassId,
                        static_cast<uint16_t>(ChannelMethodId::Open),
                        encodeChannelOpen(ChannelOpen{}), channel))
                    .ok);
    BasicConsume consume;
    consume.queue = "q1";
    consume.no_ack = true;
    ASSERT_TRUE(session
                    .feed(encodeMethodFrame(
                        kBasicClassId,
                        static_cast<uint16_t>(BasicMethodId::Consume),
                        encodeBasicConsume(consume), channel))
                    .ok);

    BasicPublish publish;
    publish.exchange = "";
    publish.routing_key = "q1";
    const std::string body = "abc";
    const std::string header =
        encodeExpirationContentHeader(body.size(), "100");
    const size_t before = sent.size();
    ASSERT_TRUE(session
                    .feed(encodeMethodFrame(
                              kBasicClassId,
                              static_cast<uint16_t>(BasicMethodId::Publish),
                              encodeBasicPublish(publish), channel) +
                          encodeRawFrame(kFrameHeader, channel, header) +
                          encodeRawFrame(kFrameBody, channel, body))
                    .ok);
    ASSERT_GE(sent.size(), before + 3U);
    const std::string& header_frame = sent[sent.size() - 2];
    EXPECT_EQ(header_frame.substr(7, header.size()), header);
}

TEST(ConnectionSessionTest, RecoverRequeuesUnackedMessages) {
    auto host = std::make_shared<broker::VirtualHost>();
    std::vector<std::string> sent;
    ConnectionSession session(ConnectionConfig{}, [&](const std::string& bytes) {
        sent.push_back(bytes);
    }, host);
    establishReady(session, sent);
    ASSERT_TRUE(host->declareQueue(
                    broker::QueueSpec{"q1", false, false, false})
                    .ok);

    const uint16_t channel = 1;
    ASSERT_TRUE(session
                    .feed(encodeMethodFrame(
                        kChannelClassId,
                        static_cast<uint16_t>(ChannelMethodId::Open),
                        encodeChannelOpen(ChannelOpen{}), channel))
                    .ok);

    BasicPublish publish;
    publish.exchange = "";
    publish.routing_key = "q1";
    const std::string body = "retry";
    ASSERT_TRUE(session
                    .feed(encodeMethodFrame(
                              kBasicClassId,
                              static_cast<uint16_t>(BasicMethodId::Publish),
                              encodeBasicPublish(publish), channel) +
                          encodeRawFrame(
                              kFrameHeader, channel,
                              encodeContentHeader(body.size())) +
                          encodeRawFrame(kFrameBody, channel, body))
                    .ok);

    BasicGet get;
    get.queue = "q1";
    ASSERT_TRUE(session
                    .feed(encodeMethodFrame(
                        kBasicClassId,
                        static_cast<uint16_t>(BasicMethodId::Get),
                        encodeBasicGet(get), channel))
                    .ok);
    EXPECT_EQ(host->unackedCount(), 1U);

    BasicRecover recover;
    recover.requeue = true;
    const size_t before_recover = sent.size();
    ASSERT_TRUE(session
                    .feed(encodeMethodFrame(
                        kBasicClassId,
                        static_cast<uint16_t>(BasicMethodId::Recover),
                        encodeBasicRecover(recover), channel))
                    .ok);
    EXPECT_EQ(host->messageCount("q1"), 1U);
    EXPECT_EQ(host->unackedCount(), 0U);
    MethodHeader header;
    std::string error;
    ASSERT_TRUE(decodeCapturedMethod(sent[before_recover], header, error))
        << error;
    EXPECT_EQ(header.method_id,
              static_cast<uint16_t>(BasicMethodId::RecoverOk));
}

}  // namespace mq::amqp091

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
