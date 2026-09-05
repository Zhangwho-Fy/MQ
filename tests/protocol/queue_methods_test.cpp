#include "mq/protocol/amqp091/queue_methods.hpp"

#include <gtest/gtest.h>

namespace mq::amqp091 {

TEST(QueueMethodsTest, DeclareRoundTrip) {
    QueueDeclare declare;
    declare.ticket = 0;
    declare.queue = "task_queue";
    declare.durable = true;
    declare.exclusive = false;
    declare.auto_delete = false;
    declare.arguments.addString("x-dead-letter-exchange", "dlx");

    QueueDeclare decoded;
    std::string error;
    ASSERT_TRUE(decodeQueueDeclare(encodeQueueDeclare(declare), decoded, error))
        << error;
    EXPECT_EQ(decoded.queue, "task_queue");
    EXPECT_TRUE(decoded.durable);
    EXPECT_FALSE(decoded.exclusive);
    EXPECT_FALSE(decoded.auto_delete);
    const std::string* dlx = decoded.arguments.findString("x-dead-letter-exchange");
    ASSERT_NE(dlx, nullptr);
    EXPECT_EQ(*dlx, "dlx");
}

TEST(QueueMethodsTest, DeclareOkRoundTrip) {
    QueueDeclareOk ok;
    ok.queue = "task_queue";
    ok.message_count = 5;
    ok.consumer_count = 2;

    QueueDeclareOk decoded;
    std::string error;
    ASSERT_TRUE(decodeQueueDeclareOk(encodeQueueDeclareOk(ok), decoded, error))
        << error;
    EXPECT_EQ(decoded.queue, "task_queue");
    EXPECT_EQ(decoded.message_count, 5U);
    EXPECT_EQ(decoded.consumer_count, 2U);
}

TEST(QueueMethodsTest, BindAndUnbindRoundTrip) {
    QueueBind bind;
    bind.queue = "q1";
    bind.exchange = "ex1";
    bind.routing_key = "news.*";

    QueueBind decoded_bind;
    std::string error;
    ASSERT_TRUE(decodeQueueBind(encodeQueueBind(bind), decoded_bind, error))
        << error;
    EXPECT_EQ(decoded_bind.queue, "q1");
    EXPECT_EQ(decoded_bind.exchange, "ex1");
    EXPECT_EQ(decoded_bind.routing_key, "news.*");

    QueueUnbind unbind;
    unbind.queue = "q1";
    unbind.exchange = "ex1";
    unbind.routing_key = "news.*";
    QueueUnbind decoded_unbind;
    ASSERT_TRUE(
        decodeQueueUnbind(encodeQueueUnbind(unbind), decoded_unbind, error))
        << error;
    EXPECT_EQ(decoded_unbind.routing_key, "news.*");
}

TEST(QueueMethodsTest, PurgeAndDeleteRoundTrip) {
    QueuePurge purge;
    purge.queue = "q1";
    purge.no_wait = false;
    QueuePurge decoded_purge;
    std::string error;
    ASSERT_TRUE(decodeQueuePurge(encodeQueuePurge(purge), decoded_purge, error))
        << error;
    EXPECT_EQ(decoded_purge.queue, "q1");

    QueuePurgeOk purge_ok;
    purge_ok.message_count = 42;
    QueuePurgeOk decoded_purge_ok;
    ASSERT_TRUE(decodeQueuePurgeOk(encodeQueuePurgeOk(purge_ok),
                                   decoded_purge_ok, error))
        << error;
    EXPECT_EQ(decoded_purge_ok.message_count, 42U);

    QueueDelete delete_queue;
    delete_queue.queue = "q1";
    delete_queue.if_unused = true;
    delete_queue.if_empty = true;
    QueueDelete decoded_delete;
    ASSERT_TRUE(decodeQueueDelete(encodeQueueDelete(delete_queue),
                                  decoded_delete, error))
        << error;
    EXPECT_TRUE(decoded_delete.if_unused);
    EXPECT_TRUE(decoded_delete.if_empty);

    QueueDeleteOk delete_ok;
    delete_ok.message_count = 7;
    QueueDeleteOk decoded_delete_ok;
    ASSERT_TRUE(decodeQueueDeleteOk(encodeQueueDeleteOk(delete_ok),
                                    decoded_delete_ok, error))
        << error;
    EXPECT_EQ(decoded_delete_ok.message_count, 7U);
}

}  // namespace mq::amqp091

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
