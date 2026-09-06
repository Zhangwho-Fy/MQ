#include "mq/protocol/amqp091/basic_methods.hpp"

#include <gtest/gtest.h>

namespace mq::amqp091 {

TEST(BasicMethodsTest, PublishRoundTrip) {
    BasicPublish publish;
    publish.exchange = "logs";
    publish.routing_key = "task";
    publish.mandatory = true;

    BasicPublish decoded;
    std::string error;
    ASSERT_TRUE(decodeBasicPublish(encodeBasicPublish(publish), decoded, error))
        << error;
    EXPECT_EQ(decoded.exchange, "logs");
    EXPECT_EQ(decoded.routing_key, "task");
    EXPECT_TRUE(decoded.mandatory);
    EXPECT_FALSE(decoded.immediate);
}

TEST(BasicMethodsTest, ReturnRoundTrip) {
    BasicReturn ret;
    ret.reply_code = 312;
    ret.reply_text = "NO_ROUTE";
    ret.exchange = "logs";
    ret.routing_key = "missing";

    BasicReturn decoded;
    std::string error;
    ASSERT_TRUE(decodeBasicReturn(encodeBasicReturn(ret), decoded, error))
        << error;
    EXPECT_EQ(decoded.reply_code, 312U);
    EXPECT_EQ(decoded.reply_text, "NO_ROUTE");
    EXPECT_EQ(decoded.exchange, "logs");
    EXPECT_EQ(decoded.routing_key, "missing");
}

TEST(BasicMethodsTest, ConsumeCancelDeliverRoundTrip) {
    BasicConsume consume;
    consume.queue = "q1";
    consume.consumer_tag = "c1";
    consume.no_ack = true;
    consume.no_wait = false;

    BasicConsume decoded_consume;
    std::string error;
    ASSERT_TRUE(
        decodeBasicConsume(encodeBasicConsume(consume), decoded_consume, error))
        << error;
    EXPECT_EQ(decoded_consume.queue, "q1");
    EXPECT_EQ(decoded_consume.consumer_tag, "c1");
    EXPECT_TRUE(decoded_consume.no_ack);
    EXPECT_EQ(encodeBasicConsumeOk("c1"), std::string("\x02""c1", 3));

    BasicCancel cancel;
    cancel.consumer_tag = "c1";
    BasicCancel decoded_cancel;
    ASSERT_TRUE(decodeBasicCancel(encodeBasicCancel(cancel), decoded_cancel,
                                  error))
        << error;
    EXPECT_EQ(decoded_cancel.consumer_tag, "c1");
    EXPECT_EQ(encodeBasicCancelOk("c1"),
              std::string("\x02""c1", 3));

    BasicDeliver deliver;
    deliver.consumer_tag = "c1";
    deliver.delivery_tag = 7;
    deliver.exchange = "logs";
    deliver.routing_key = "task";
    BasicDeliver decoded_deliver;
    ASSERT_TRUE(decodeBasicDeliver(encodeBasicDeliver(deliver),
                                   decoded_deliver, error))
        << error;
    EXPECT_EQ(decoded_deliver.delivery_tag, 7U);
    EXPECT_EQ(decoded_deliver.exchange, "logs");
    EXPECT_EQ(decoded_deliver.routing_key, "task");
}

TEST(BasicMethodsTest, AckAndRejectRoundTrip) {
    BasicAck ack;
    ack.delivery_tag = 12;
    ack.multiple = true;
    BasicAck decoded_ack;
    std::string error;
    ASSERT_TRUE(decodeBasicAck(encodeBasicAck(ack), decoded_ack, error))
        << error;
    EXPECT_EQ(decoded_ack.delivery_tag, 12U);
    EXPECT_TRUE(decoded_ack.multiple);

    BasicReject reject;
    reject.delivery_tag = 12;
    reject.requeue = true;
    BasicReject decoded_reject;
    ASSERT_TRUE(decodeBasicReject(encodeBasicReject(reject), decoded_reject,
                                  error))
        << error;
    EXPECT_EQ(decoded_reject.delivery_tag, 12U);
    EXPECT_TRUE(decoded_reject.requeue);
}

TEST(BasicMethodsTest, ContentHeaderRoundTrip) {
    const uint64_t body_size = 5;
    const std::string payload = encodeContentHeader(body_size);
    ContentHeaderInfo info;
    std::string error;
    ASSERT_TRUE(decodeContentHeader(payload, info, error)) << error;
    EXPECT_EQ(info.class_id, kBasicClassId);
    EXPECT_EQ(info.body_size, body_size);
    EXPECT_FALSE(info.persistent);
}

TEST(BasicMethodsTest, ContentHeaderExtractsPersistentDeliveryMode) {
    // flags: content-type=0, content-encoding=0, headers=0, delivery-mode=1
    const uint16_t delivery_mode_flag = 0x8000U >> 3;
    std::string payload;
    payload.append("\x00\x3c", 2);  // class-id 60
    payload.append("\x00\x00", 2);  // weight
    payload.append("\x00\x00\x00\x00\x00\x00\x00\x03", 8);  // body size 3
    payload.push_back(static_cast<char>((delivery_mode_flag >> 8) & 0xff));
    payload.push_back(static_cast<char>(delivery_mode_flag & 0xff));
    payload.append("\x02", 1);  // delivery mode 2 = persistent

    ContentHeaderInfo info;
    std::string error;
    ASSERT_TRUE(decodeContentHeader(payload, info, error)) << error;
    EXPECT_EQ(info.body_size, 3U);
    EXPECT_TRUE(info.persistent);
}

}  // namespace mq::amqp091

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
