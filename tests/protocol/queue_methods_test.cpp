#include "mq/protocol/amqp091/queue_methods.hpp"

#include <gtest/gtest.h>

namespace mq::amqp091 {

TEST(QueueMethodsTest, RoundTripsDeclareAndBind) {
    std::string error;

    QueueDeclare declare;
    declare.queue = "q1";
    declare.durable = true;
    declare.exclusive = true;
    declare.arguments.addString("x-dead-letter-exchange", "dlx");
    QueueDeclare decoded_declare;
    ASSERT_TRUE(decodeQueueDeclare(encodeQueueDeclare(declare), decoded_declare,
                                   error))
        << error;
    EXPECT_EQ(decoded_declare.queue, "q1");
    EXPECT_TRUE(decoded_declare.durable);
    EXPECT_TRUE(decoded_declare.exclusive);
    ASSERT_NE(decoded_declare.arguments.findString("x-dead-letter-exchange"),
              nullptr);
    EXPECT_EQ(*decoded_declare.arguments.findString("x-dead-letter-exchange"),
              "dlx");

    QueueDeclareOk declare_ok;
    declare_ok.queue = "q1";
    declare_ok.message_count = 3;
    declare_ok.consumer_count = 2;
    QueueDeclareOk decoded_ok;
    ASSERT_TRUE(decodeQueueDeclareOk(encodeQueueDeclareOk(declare_ok),
                                     decoded_ok, error))
        << error;
    EXPECT_EQ(decoded_ok.queue, "q1");
    EXPECT_EQ(decoded_ok.message_count, 3U);
    EXPECT_EQ(decoded_ok.consumer_count, 2U);

    QueueBind bind;
    bind.queue = "q1";
    bind.exchange = "logs";
    bind.routing_key = "task";
    QueueBind decoded_bind;
    ASSERT_TRUE(decodeQueueBind(encodeQueueBind(bind), decoded_bind, error))
        << error;
    EXPECT_EQ(decoded_bind.exchange, "logs");
    EXPECT_EQ(decoded_bind.routing_key, "task");

    QueueUnbind unbind;
    unbind.queue = "q1";
    unbind.exchange = "logs";
    unbind.routing_key = "task";
    QueueUnbind decoded_unbind;
    ASSERT_TRUE(
        decodeQueueUnbind(encodeQueueUnbind(unbind), decoded_unbind, error))
        << error;
    EXPECT_EQ(decoded_unbind.queue, "q1");
    EXPECT_EQ(decoded_unbind.routing_key, "task");
}

TEST(QueueMethodsTest, RoundTripsPurgeAndDelete) {
    std::string error;

    QueuePurge purge;
    purge.queue = "q1";
    purge.no_wait = true;
    QueuePurge decoded_purge;
    ASSERT_TRUE(decodeQueuePurge(encodeQueuePurge(purge), decoded_purge, error))
        << error;
    EXPECT_TRUE(decoded_purge.no_wait);

    QueuePurgeOk purge_ok;
    purge_ok.message_count = 5;
    QueuePurgeOk decoded_purge_ok;
    ASSERT_TRUE(
        decodeQueuePurgeOk(encodeQueuePurgeOk(purge_ok), decoded_purge_ok,
                           error))
        << error;
    EXPECT_EQ(decoded_purge_ok.message_count, 5U);

    QueueDelete remove;
    remove.queue = "q1";
    remove.if_unused = true;
    QueueDelete decoded_delete;
    ASSERT_TRUE(
        decodeQueueDelete(encodeQueueDelete(remove), decoded_delete, error))
        << error;
    EXPECT_TRUE(decoded_delete.if_unused);
    EXPECT_FALSE(decoded_delete.if_empty);

    QueueDeleteOk delete_ok;
    delete_ok.message_count = 7;
    QueueDeleteOk decoded_delete_ok;
    ASSERT_TRUE(
        decodeQueueDeleteOk(encodeQueueDeleteOk(delete_ok), decoded_delete_ok,
                            error))
        << error;
    EXPECT_EQ(decoded_delete_ok.message_count, 7U);
}

// durable is the second bit field of queue.declare, so it alone must be 0x02.
TEST(QueueMethodsTest, DeclarePacksBitsLeastSignificantFirst) {
    QueueDeclare declare;
    declare.queue = "q1";
    declare.durable = true;

    const std::string encoded = encodeQueueDeclare(declare);
    const std::size_t bits_offset = 2 + 1 + 2;  // ticket + shortstr(queue)
    ASSERT_GT(encoded.size(), bits_offset);
    EXPECT_EQ(static_cast<unsigned char>(encoded[bits_offset]), 0x02U);

    QueueDeclare decoded;
    std::string error;
    ASSERT_TRUE(decodeQueueDeclare(encoded, decoded, error)) << error;
    EXPECT_TRUE(decoded.durable);
    EXPECT_FALSE(decoded.passive);
}

}  // namespace mq::amqp091

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
