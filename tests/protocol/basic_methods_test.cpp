#include "mq/protocol/amqp091/basic_methods.hpp"

#include <gtest/gtest.h>

namespace mq::amqp091 {

// Argument codecs are exercised by round-tripping populated structs. Byte-level
// expectations live in the bit-order tests (and in the other *_methods tests).
TEST(BasicMethodsTest, RoundTripsPublishAndGetMethods) {
    std::string error;

    BasicPublish publish;
    publish.exchange = "logs";
    publish.routing_key = "task";
    publish.mandatory = true;
    BasicPublish decoded_publish;
    ASSERT_TRUE(decodeBasicPublish(encodeBasicPublish(publish), decoded_publish,
                                   error))
        << error;
    EXPECT_EQ(decoded_publish.exchange, "logs");
    EXPECT_EQ(decoded_publish.routing_key, "task");
    EXPECT_TRUE(decoded_publish.mandatory);
    EXPECT_FALSE(decoded_publish.immediate);

    BasicReturn ret;
    ret.reply_code = 312;
    ret.reply_text = "NO_ROUTE";
    ret.exchange = "logs";
    ret.routing_key = "missing";
    BasicReturn decoded_return;
    ASSERT_TRUE(decodeBasicReturn(encodeBasicReturn(ret), decoded_return, error))
        << error;
    EXPECT_EQ(decoded_return.reply_code, 312U);
    EXPECT_EQ(decoded_return.reply_text, "NO_ROUTE");
    EXPECT_EQ(decoded_return.routing_key, "missing");

    BasicQos qos;
    qos.prefetch_size = 1024;
    qos.prefetch_count = 10;
    qos.global = true;
    BasicQos decoded_qos;
    ASSERT_TRUE(decodeBasicQos(encodeBasicQos(qos), decoded_qos, error))
        << error;
    EXPECT_EQ(decoded_qos.prefetch_size, 1024U);
    EXPECT_EQ(decoded_qos.prefetch_count, 10U);
    EXPECT_TRUE(decoded_qos.global);

    BasicRecover recover;
    recover.requeue = true;
    BasicRecover decoded_recover;
    ASSERT_TRUE(decodeBasicRecover(encodeBasicRecover(recover), decoded_recover,
                                   error))
        << error;
    EXPECT_TRUE(decoded_recover.requeue);

    BasicGet get;
    get.queue = "q1";
    get.no_ack = true;
    BasicGet decoded_get;
    ASSERT_TRUE(decodeBasicGet(encodeBasicGet(get), decoded_get, error))
        << error;
    EXPECT_EQ(decoded_get.queue, "q1");
    EXPECT_TRUE(decoded_get.no_ack);
    EXPECT_EQ(decoded_get.ticket, 0U);

    BasicGetOk ok;
    ok.delivery_tag = 5;
    ok.redelivered = true;
    ok.exchange = "logs";
    ok.routing_key = "task";
    ok.message_count = 9;
    BasicGetOk decoded_ok;
    ASSERT_TRUE(decodeBasicGetOk(encodeBasicGetOk(ok), decoded_ok, error))
        << error;
    EXPECT_EQ(decoded_ok.delivery_tag, 5U);
    EXPECT_TRUE(decoded_ok.redelivered);
    EXPECT_EQ(decoded_ok.exchange, "logs");
    EXPECT_EQ(decoded_ok.message_count, 9U);
}

TEST(BasicMethodsTest, RoundTripsConsumeAndAckMethods) {
    std::string error;

    BasicConsume consume;
    consume.queue = "q1";
    consume.consumer_tag = "c1";
    consume.no_ack = true;
    BasicConsume decoded_consume;
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
    EXPECT_EQ(encodeBasicCancelOk("c1"), std::string("\x02""c1", 3));

    BasicDeliver deliver;
    deliver.consumer_tag = "c1";
    deliver.delivery_tag = 7;
    deliver.exchange = "logs";
    deliver.routing_key = "task";
    BasicDeliver decoded_deliver;
    ASSERT_TRUE(decodeBasicDeliver(encodeBasicDeliver(deliver), decoded_deliver,
                                   error))
        << error;
    EXPECT_EQ(decoded_deliver.delivery_tag, 7U);
    EXPECT_FALSE(decoded_deliver.redelivered);
    EXPECT_EQ(decoded_deliver.routing_key, "task");

    BasicAck ack;
    ack.delivery_tag = 12;
    ack.multiple = true;
    BasicAck decoded_ack;
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
    EXPECT_TRUE(decoded_reject.requeue);

    BasicNack nack;
    nack.delivery_tag = 4;
    nack.multiple = true;
    BasicNack decoded_nack;
    ASSERT_TRUE(decodeBasicNack(encodeBasicNack(nack), decoded_nack, error))
        << error;
    EXPECT_TRUE(decoded_nack.multiple);
    EXPECT_FALSE(decoded_nack.requeue);
}

TEST(BasicMethodsTest, RoundTripsContentHeaderAndExtractsTwoProperties) {
    ContentHeaderInfo info;
    std::string error;
    ASSERT_TRUE(decodeContentHeader(encodeContentHeader(5), info, error))
        << error;
    EXPECT_EQ(info.class_id, kBasicClassId);
    EXPECT_EQ(info.body_size, 5U);
    EXPECT_FALSE(info.persistent);

    // Property flags are MSB-first: the 4th property (delivery-mode) is 0x1000.
    std::string persistent;
    persistent.append("\x00\x3c\x00\x00", 4);
    persistent.append("\x00\x00\x00\x00\x00\x00\x00\x03", 8);
    persistent.append("\x10\x00", 2);
    persistent.append("\x02", 1);
    ASSERT_TRUE(decodeContentHeader(persistent, info, error)) << error;
    EXPECT_EQ(info.body_size, 3U);
    EXPECT_TRUE(info.persistent);

    // expiration is the 8th property -> flag 0x0100.
    std::string with_expiration;
    with_expiration.append("\x00\x3c\x00\x00", 4);
    with_expiration.append("\x00\x00\x00\x00\x00\x00\x00\x03", 8);
    with_expiration.append("\x01\x00", 2);
    with_expiration.append("\x02""30", 3);
    ASSERT_TRUE(decodeContentHeader(with_expiration, info, error)) << error;
    EXPECT_EQ(info.expiration, "30");
}

}  // namespace mq::amqp091

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
