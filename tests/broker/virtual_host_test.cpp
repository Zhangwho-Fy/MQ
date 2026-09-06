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

TEST(VirtualHostTest, ConsumerReceivesQueuedMessages) {
    VirtualHost host;
    ASSERT_TRUE(host.declareQueue(QueueSpec{"q1", false, false, false}).ok);
    ASSERT_TRUE(host.publish("", "q1", Message{"queued", false}).ok);
    EXPECT_EQ(host.messageCount("q1"), 1U);

    std::vector<std::string> received;
    const BrokerResult result = host.registerConsumer(
        "q1", "c1", this,
        [&](const std::string&, const std::string&, const Message& message) {
            received.push_back(message.body);
        });
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(received.size(), 1U);
    EXPECT_EQ(received[0], "queued");
    EXPECT_EQ(host.messageCount("q1"), 0U);
    EXPECT_EQ(host.consumerCount("q1"), 1U);
}

TEST(VirtualHostTest, PublishDeliversToExistingConsumer) {
    VirtualHost host;
    ASSERT_TRUE(host.declareQueue(QueueSpec{"q1", false, false, false}).ok);

    std::vector<std::string> received;
    ASSERT_TRUE(host
                    .registerConsumer(
                        "q1", "c1", this,
                        [&](const std::string&, const std::string&,
                            const Message& message) {
                            received.push_back(message.body);
                        })
                    .ok);

    ASSERT_TRUE(host.publish("", "q1", Message{"live", false}).ok);
    EXPECT_EQ(received.size(), 1U);
    EXPECT_EQ(received[0], "live");
    EXPECT_EQ(host.messageCount("q1"), 0U);
}

TEST(VirtualHostTest, AckRemovesUnackedMessage) {
    VirtualHost host;
    ASSERT_TRUE(host.declareQueue(QueueSpec{"q1", false, false, false}).ok);
    uint64_t delivered_id = 0;
    ASSERT_TRUE(host
                    .registerConsumer(
                        "q1", "c1", this,
                        [&](const std::string&, const std::string&,
                            const Message& message) {
                            delivered_id = message.id;
                        })
                    .ok);
    ASSERT_TRUE(host.publish("", "q1", Message{"a", false}).ok);
    EXPECT_EQ(host.unackedCount(), 1U);
    ASSERT_NE(delivered_id, 0U);

    ASSERT_TRUE(host.ackMessage(delivered_id).ok);
    EXPECT_EQ(host.unackedCount(), 0U);
    EXPECT_EQ(host.ackMessage(delivered_id).reply_code,
              VirtualHost::kPreconditionFailed);
}

TEST(VirtualHostTest, RejectRequeuesAndMarksRedelivered) {
    VirtualHost host;
    ASSERT_TRUE(host.declareQueue(QueueSpec{"q1", false, false, false}).ok);
    std::vector<Message> received;
    ASSERT_TRUE(host
                    .registerConsumer(
                        "q1", "c1", this,
                        [&](const std::string&, const std::string&,
                            const Message& message) {
                            received.push_back(message);
                        })
                    .ok);
    ASSERT_TRUE(host.publish("", "q1", Message{"a", false}).ok);
    ASSERT_EQ(received.size(), 1U);
    EXPECT_EQ(host.unackedCount(), 1U);

    const uint64_t id = received[0].id;
    ASSERT_TRUE(host.rejectMessage(id, true).ok);
    ASSERT_EQ(received.size(), 2U);
    EXPECT_TRUE(received[1].redelivered);
    EXPECT_EQ(received[1].id, id);
    EXPECT_EQ(host.unackedCount(), 1U);
}

}  // namespace mq::broker

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
