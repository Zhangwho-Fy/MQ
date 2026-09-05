#include "mq/broker/virtual_host.hpp"

#include <gtest/gtest.h>

namespace mq::broker {

TEST(VirtualHostTest, DeclaresAndDeletesExchanges) {
    VirtualHost host;
    ASSERT_TRUE(host.declareExchange(
                    ExchangeSpec{"logs", "fanout", true, false, false})
                    .ok);
    ASSERT_TRUE(host.hasExchange("logs"));

    EXPECT_FALSE(host
                     .declareExchange(ExchangeSpec{
                         "logs", "direct", true, false, false})
                     .ok);

    const BrokerResult deleted = host.deleteExchange("logs", false);
    ASSERT_TRUE(deleted.ok) << deleted.error;
    EXPECT_FALSE(host.hasExchange("logs"));
    EXPECT_EQ(host.exchangeCount(), 0U);
}

TEST(VirtualHostTest, RejectsInvalidExchangeType) {
    VirtualHost host;
    const BrokerResult result =
        host.declareExchange(ExchangeSpec{"bad", "unknown", false, false, false});
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.reply_code, VirtualHost::kPreconditionFailed);
}

TEST(VirtualHostTest, QueueGetsDefaultExchangeBinding) {
    VirtualHost host;
    ASSERT_TRUE(host.declareQueue(QueueSpec{"q1", true, false, false}).ok);
    EXPECT_EQ(host.queueCount(), 1U);
    EXPECT_EQ(host.bindingCount(), 1U);
    EXPECT_EQ(host.bindingCount("", "q1"), 1U);
}

TEST(VirtualHostTest, SupportsMultipleBindingKeysPerQueue) {
    VirtualHost host;
    ASSERT_TRUE(host.declareExchange(
                    ExchangeSpec{"topic_ex", "topic", false, false, false})
                    .ok);
    ASSERT_TRUE(host.declareQueue(QueueSpec{"q1", false, false, false}).ok);

    ASSERT_TRUE(host.bind("topic_ex", "q1", "news.#").ok);
    ASSERT_TRUE(host.bind("topic_ex", "q1", "sport.#").ok);
    EXPECT_EQ(host.bindingCount("topic_ex", "q1"), 2U);

    // Duplicate bindings are allowed and idempotent.
    ASSERT_TRUE(host.bind("topic_ex", "q1", "news.#").ok);
    EXPECT_EQ(host.bindingCount("topic_ex", "q1"), 2U);

    ASSERT_TRUE(host.unbind("topic_ex", "q1", "news.#").ok);
    EXPECT_EQ(host.bindingCount("topic_ex", "q1"), 1U);
    EXPECT_EQ(host.unbind("topic_ex", "q1", "missing").reply_code,
              VirtualHost::kNotFound);
}

TEST(VirtualHostTest, DeleteCleansBindingsAndHonorsIfUnused) {
    VirtualHost host;
    ASSERT_TRUE(host.declareExchange(
                    ExchangeSpec{"ex1", "direct", false, false, false})
                    .ok);
    ASSERT_TRUE(host.declareQueue(QueueSpec{"q1", false, false, false}).ok);
    ASSERT_TRUE(host.bind("ex1", "q1", "key1").ok);

    EXPECT_EQ(host.deleteExchange("ex1", true).reply_code,
              VirtualHost::kPreconditionFailed);
    ASSERT_TRUE(host.deleteQueue("q1", false, true).ok);
    EXPECT_FALSE(host.hasQueue("q1"));

    ASSERT_TRUE(host.declareQueue(QueueSpec{"q1", false, false, false}).ok);
    ASSERT_TRUE(host.bind("ex1", "q1", "key1").ok);
    EXPECT_EQ(host.deleteExchange("ex1", true).reply_code,
              VirtualHost::kPreconditionFailed);
    ASSERT_TRUE(host.unbind("ex1", "q1", "key1").ok);
    ASSERT_TRUE(host.deleteExchange("ex1", true).ok);
}

TEST(VirtualHostTest, MissingEntitiesReturnNotFound) {
    VirtualHost host;
    EXPECT_EQ(host.deleteExchange("nope", false).reply_code,
              VirtualHost::kNotFound);
    EXPECT_EQ(host.deleteQueue("nope", false, false).reply_code,
              VirtualHost::kNotFound);
    EXPECT_FALSE(host.bind("nope", "q1", "k").ok);
}

TEST(VirtualHostTest, RoutesDirectMessages) {
    VirtualHost host;
    ASSERT_TRUE(host.declareExchange(
                    ExchangeSpec{"ex", "direct", false, false, false})
                    .ok);
    ASSERT_TRUE(host.declareQueue(QueueSpec{"q1", false, false, false}).ok);
    ASSERT_TRUE(host.declareQueue(QueueSpec{"q2", false, false, false}).ok);
    ASSERT_TRUE(host.bind("ex", "q1", "a").ok);
    ASSERT_TRUE(host.bind("ex", "q2", "b").ok);

    size_t delivered = 0;
    ASSERT_TRUE(host
                    .publish("ex", "a", Message{"hello", false}, &delivered)
                    .ok);
    EXPECT_EQ(delivered, 1U);
    EXPECT_EQ(host.messageCount("q1"), 1U);
    EXPECT_EQ(host.messageCount("q2"), 0U);
}

TEST(VirtualHostTest, RoutesFanoutToAllBoundQueues) {
    VirtualHost host;
    ASSERT_TRUE(host.declareExchange(
                    ExchangeSpec{"ex", "fanout", false, false, false})
                    .ok);
    ASSERT_TRUE(host.declareQueue(QueueSpec{"q1", false, false, false}).ok);
    ASSERT_TRUE(host.declareQueue(QueueSpec{"q2", false, false, false}).ok);
    ASSERT_TRUE(host.bind("ex", "q1", "ignored").ok);
    ASSERT_TRUE(host.bind("ex", "q2", "ignored").ok);

    size_t delivered = 0;
    ASSERT_TRUE(host.publish("ex", "", Message{"hello", false}, &delivered).ok);
    EXPECT_EQ(delivered, 2U);
    EXPECT_EQ(host.messageCount("q1"), 1U);
    EXPECT_EQ(host.messageCount("q2"), 1U);
}

TEST(VirtualHostTest, RoutesTopicBindings) {
    VirtualHost host;
    ASSERT_TRUE(host.declareExchange(
                    ExchangeSpec{"ex", "topic", false, false, false})
                    .ok);
    ASSERT_TRUE(host.declareQueue(QueueSpec{"q1", false, false, false}).ok);
    ASSERT_TRUE(host.declareQueue(QueueSpec{"q2", false, false, false}).ok);
    ASSERT_TRUE(host.bind("ex", "q1", "news.#").ok);
    ASSERT_TRUE(host.bind("ex", "q2", "sport.*").ok);

    size_t delivered = 0;
    ASSERT_TRUE(
        host.publish("ex", "news.music", Message{"hello", false}, &delivered)
            .ok);
    EXPECT_EQ(delivered, 1U);
    EXPECT_EQ(host.messageCount("q1"), 1U);
    EXPECT_EQ(host.messageCount("q2"), 0U);
}

TEST(VirtualHostTest, PublishesThroughDefaultExchange) {
    VirtualHost host;
    ASSERT_TRUE(host.declareQueue(QueueSpec{"q1", false, false, false}).ok);
    size_t delivered = 0;
    ASSERT_TRUE(host.publish("", "q1", Message{"hello", false}, &delivered).ok);
    EXPECT_EQ(delivered, 1U);
    EXPECT_EQ(host.messageCount("q1"), 1U);
}

TEST(VirtualHostTest, PurgeRemovesMessagesAndReportsCount) {
    VirtualHost host;
    ASSERT_TRUE(host.declareQueue(QueueSpec{"q1", false, false, false}).ok);
    ASSERT_TRUE(host.publish("", "q1", Message{"a", false}).ok);
    ASSERT_TRUE(host.publish("", "q1", Message{"b", false}).ok);
    EXPECT_EQ(host.messageCount("q1"), 2U);

    const BrokerResult result = host.purgeQueue("q1");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.count, 2U);
    EXPECT_EQ(host.messageCount("q1"), 0U);
}

}  // namespace mq::broker

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
