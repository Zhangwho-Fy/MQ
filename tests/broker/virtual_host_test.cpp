#include "mq/broker/virtual_host.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <thread>

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

TEST(VirtualHostTest, RejectWithoutRequeueDeadLetters) {
    VirtualHost host;
    ASSERT_TRUE(host.declareExchange(
                    ExchangeSpec{"dlx", "direct", false, false, false})
                    .ok);
    ASSERT_TRUE(host.declareQueue(QueueSpec{"dlq", false, false, false}).ok);
    ASSERT_TRUE(host.bind("dlx", "dlq", "dead.rk").ok);

    QueueSpec source_spec;
    source_spec.name = "q1";
    source_spec.dead_letter_exchange = "dlx";
    source_spec.dead_letter_routing_key = "dead.rk";
    ASSERT_TRUE(host.declareQueue(source_spec).ok);
    EXPECT_EQ(host.deadLetterExchange("q1"), "dlx");

    std::vector<uint64_t> delivered_ids;
    ASSERT_TRUE(host
                    .registerConsumer(
                        "q1", "c1", this,
                        [&](const std::string&, const std::string&,
                            const Message& message) {
                            delivered_ids.push_back(message.id);
                        })
                    .ok);
    ASSERT_TRUE(host.publish("", "q1", Message{"bad", false}).ok);
    ASSERT_EQ(delivered_ids.size(), 1U);
    EXPECT_EQ(host.unackedCount(), 1U);

    ASSERT_TRUE(host.rejectMessage(delivered_ids[0], false).ok);
    EXPECT_EQ(host.unackedCount(), 0U);
    EXPECT_EQ(host.messageCount("dlq"), 1U);
}

TEST(VirtualHostTest, TtlExpiredMessagesDeadLetterOnPurge) {
    VirtualHost host;
    ASSERT_TRUE(host.declareExchange(
                    ExchangeSpec{"dlx", "direct", false, false, false})
                    .ok);
    ASSERT_TRUE(host.declareQueue(QueueSpec{"dlq", false, false, false}).ok);
    ASSERT_TRUE(host.bind("dlx", "dlq", "expired").ok);

    QueueSpec source;
    source.name = "q1";
    source.dead_letter_exchange = "dlx";
    source.dead_letter_routing_key = "expired";
    source.message_ttl_ms = 20;
    ASSERT_TRUE(host.declareQueue(source).ok);
    EXPECT_EQ(host.messageTtl("q1"), 20);

    ASSERT_TRUE(host.publish("", "q1", Message{"old", false}).ok);
    EXPECT_EQ(host.messageCount("q1"), 1U);
    std::this_thread::sleep_for(std::chrono::milliseconds(40));

    const BrokerResult purged = host.purgeQueue("q1");
    ASSERT_TRUE(purged.ok) << purged.error;
    EXPECT_EQ(purged.count, 0U);
    EXPECT_EQ(host.messageCount("dlq"), 1U);
}

TEST(VirtualHostTest, PerMessageTtlExpiresMessage) {
    VirtualHost host;
    ASSERT_TRUE(host.declareQueue(QueueSpec{"q1", false, false, false}).ok);
    Message message;
    message.body = "short-lived";
    message.ttl_ms = 20;
    ASSERT_TRUE(host.publish("", "q1", message).ok);
    EXPECT_EQ(host.messageCount("q1"), 1U);

    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    const BrokerResult purged = host.purgeQueue("q1");
    ASSERT_TRUE(purged.ok) << purged.error;
    EXPECT_EQ(purged.count, 0U);
}

TEST(VirtualHostTest, GetPullsMessageAndTracksUnacked) {
    VirtualHost host;
    ASSERT_TRUE(host.declareQueue(QueueSpec{"q1", false, false, false}).ok);
    ASSERT_TRUE(host.publish("", "q1", Message{"one", false}).ok);
    ASSERT_TRUE(host.publish("", "q1", Message{"two", false}).ok);

    Message message;
    bool has = false;
    uint32_t remaining = 0;
    const BrokerResult result =
        host.getMessage("q1", false, this, &message, &has, &remaining);
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_TRUE(has);
    EXPECT_EQ(message.body, "one");
    EXPECT_EQ(remaining, 1U);
    EXPECT_EQ(host.unackedCount(), 1U);

    ASSERT_TRUE(host.ackMessage(message.id).ok);
    EXPECT_EQ(host.unackedCount(), 0U);
    EXPECT_EQ(host.messageCount("q1"), 1U);
}

TEST(VirtualHostTest, GetEmptyQueueReturnsNoMessage) {
    VirtualHost host;
    ASSERT_TRUE(host.declareQueue(QueueSpec{"q1", false, false, false}).ok);
    Message message;
    bool has = true;
    uint32_t remaining = 0;
    const BrokerResult result =
        host.getMessage("q1", true, this, &message, &has, &remaining);
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_FALSE(has);
    EXPECT_EQ(host.unackedCount(), 0U);
}

TEST(VirtualHostTest, PrefetchRefillsAfterAck) {
    VirtualHost host;
    ASSERT_TRUE(host.declareQueue(QueueSpec{"q1", false, false, false}).ok);
    std::vector<Message> received;
    ASSERT_TRUE(host
                    .registerConsumer(
                        "q1", "c1", this,
                        [&](const std::string&, const std::string&,
                            const Message& message) {
                            received.push_back(message);
                        },
                        false, 2)
                    .ok);
    ASSERT_TRUE(host.publish("", "q1", Message{"a", false}).ok);
    ASSERT_TRUE(host.publish("", "q1", Message{"b", false}).ok);
    ASSERT_TRUE(host.publish("", "q1", Message{"c", false}).ok);
    ASSERT_EQ(received.size(), 2U);

    // We know the acked ids only through VirtualHost's unacked table.
    // Expose it indirectly: ack both currently delivered messages via ids
    // captured by the callback.
    ASSERT_TRUE(host.ackMessage(received[0].id).ok);
    ASSERT_EQ(received.size(), 3U);
    EXPECT_EQ(received[2].body, "c");
    EXPECT_EQ(host.messageCount("q1"), 0U);
    EXPECT_EQ(host.unackedCount(), 2U);
}

TEST(VirtualHostTest, ExclusiveConsumerBlocksOthers) {
    VirtualHost host;
    ASSERT_TRUE(host.declareQueue(QueueSpec{"q1", false, false, false}).ok);
    int owner_a = 0;
    int owner_b = 0;
    ASSERT_TRUE(host
                    .registerConsumer(
                        "q1", "c1", &owner_a,
                        [](const std::string&, const std::string&,
                           const Message&) {},
                        false, 0, false, true)
                    .ok);
    const BrokerResult second = host.registerConsumer(
        "q1", "c2", &owner_b,
        [](const std::string&, const std::string&, const Message&) {},
        false, 0, false, false);
    EXPECT_FALSE(second.ok);
    EXPECT_EQ(second.reply_code, VirtualHost::kAccessRefused);
}

TEST(VirtualHostTest, NoLocalSkipsOwnMessages) {
    VirtualHost host;
    ASSERT_TRUE(host.declareQueue(QueueSpec{"q1", false, false, false}).ok);
    int owner = 0;
    size_t delivered = 0;
    ASSERT_TRUE(host
                    .registerConsumer(
                        "q1", "c1", &owner,
                        [&](const std::string&, const std::string&,
                            const Message&) { ++delivered; },
                        false, 0, true, false)
                    .ok);

    Message message;
    message.body = "own";
    message.publisher_owner = &owner;
    ASSERT_TRUE(host.publish("", "q1", message).ok);
    EXPECT_EQ(delivered, 0U);
    EXPECT_EQ(host.messageCount("q1"), 1U);
}

TEST(VirtualHostTest, AutoDeleteQueueAfterLastConsumer) {
    VirtualHost host;
    QueueSpec spec;
    spec.name = "q1";
    spec.auto_delete = true;
    ASSERT_TRUE(host.declareQueue(spec).ok);
    int owner = 0;
    ASSERT_TRUE(host
                    .registerConsumer(
                        "q1", "c1", &owner,
                        [](const std::string&, const std::string&,
                           const Message&) {},
                        true)
                    .ok);
    EXPECT_TRUE(host.hasQueue("q1"));
    host.unregisterConsumer("q1", "c1", &owner);
    EXPECT_FALSE(host.hasQueue("q1"));
}

TEST(VirtualHostTest, DeleteQueueIfUnusedFailsWithConsumers) {
    VirtualHost host;
    ASSERT_TRUE(host.declareQueue(QueueSpec{"q1", false, false, false}).ok);
    int owner = 0;
    ASSERT_TRUE(host
                    .registerConsumer(
                        "q1", "c1", &owner,
                        [](const std::string&, const std::string&,
                           const Message&) {},
                        true)
                    .ok);
    const BrokerResult result = host.deleteQueue("q1", true, false);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.reply_code, VirtualHost::kPreconditionFailed);
}

TEST(VirtualHostTest, DisconnectRemovesExclusiveQueue) {
    VirtualHost host;
    int owner = 0;
    QueueSpec spec;
    spec.name = "q1";
    spec.exclusive = true;
    ASSERT_TRUE(host.declareQueue(spec, &owner).ok);
    ASSERT_TRUE(host.publish("", "q1", Message{"a", false}).ok);
    host.disconnectOwner(&owner);
    EXPECT_FALSE(host.hasQueue("q1"));
}

TEST(VirtualHostTest, PersistsMetadataAcrossRestart) {
    char template_dir[] = "/tmp/mq-meta-XXXXXX";
    char* created = mkdtemp(template_dir);
    ASSERT_NE(created, nullptr);
    const std::string dir = created;

    {
        VirtualHost host(dir);
        ExchangeSpec exchange;
        exchange.name = "ex";
        exchange.type = "direct";
        exchange.durable = true;
        ASSERT_TRUE(host.declareExchange(exchange).ok);

        QueueSpec queue;
        queue.name = "q1";
        queue.durable = true;
        queue.dead_letter_exchange = "dlx";
        ASSERT_TRUE(host.declareQueue(queue).ok);
        ASSERT_TRUE(host.bind("ex", "q1", "key").ok);
    }

    {
        VirtualHost host(dir);
        ASSERT_TRUE(host.hasExchange("ex"));
        ASSERT_TRUE(host.hasQueue("q1"));
        EXPECT_EQ(host.bindingCount("ex", "q1"), 1U);
        EXPECT_EQ(host.bindingCount("", "q1"), 1U);  // default binding
        EXPECT_EQ(host.deadLetterExchange("q1"), "dlx");
    }

    std::remove((dir + "/meta.db").c_str());
    std::remove(created);
}

}  // namespace mq::broker

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
