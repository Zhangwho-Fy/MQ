#include "mq/broker/broker.hpp"

#include <gtest/gtest.h>

#include <filesystem>

namespace mq::broker {

TEST(BrokerTest, DefaultGuestAuthenticates) {
    Broker broker;
    EXPECT_TRUE(broker.authenticate("guest", "guest"));
    EXPECT_FALSE(broker.authenticate("guest", "wrong"));
    EXPECT_FALSE(broker.authenticate("nobody", "guest"));
    EXPECT_NE(broker.vhost("/"), nullptr);
}

TEST(BrokerTest, AddUserResolvesVhost) {
    Broker broker;
    ASSERT_TRUE(broker.addUser("alice", "secret", {"/alice"}));
    EXPECT_TRUE(broker.authenticate("alice", "secret"));
    EXPECT_FALSE(broker.authenticate("alice", "bad"));
    EXPECT_NE(broker.resolveVhost("alice", "/alice"), nullptr);
    EXPECT_EQ(broker.resolveVhost("alice", "/"), nullptr);
    EXPECT_EQ(broker.resolveVhost("alice", "/other"), nullptr);
}

TEST(BrokerTest, RejectsDuplicateUserWithoutReplacingIt) {
    Broker broker;
    ASSERT_TRUE(broker.addUser("alice", "secret", {"/alice"}));
    EXPECT_FALSE(broker.addUser("alice", "other", {"/other"}));
    EXPECT_TRUE(broker.authenticate("alice", "secret"));
    EXPECT_FALSE(broker.authenticate("alice", "other"));
    EXPECT_EQ(broker.resolveVhost("alice", "/other"), nullptr);
}

TEST(BrokerTest, RejectsOverridingBuiltinGuest) {
    Broker broker;
    EXPECT_FALSE(broker.addUser("guest", "hacked", {"/"}));
    EXPECT_TRUE(broker.authenticate("guest", "guest"));
    EXPECT_FALSE(broker.authenticate("guest", "hacked"));
}

TEST(BrokerTest, DistinctVhostsGetDistinctDirectories) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "mq-broker-vhost-dirs";
    std::filesystem::remove_all(root);
    {
        Broker broker(root.string());
        ASSERT_TRUE(broker.addUser("u1", "pw", {"/a.b"}));
        ASSERT_TRUE(broker.addUser("u2", "pw", {"/a_b"}));
    }

    size_t directories = 0;
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        if (entry.is_directory()) ++directories;
    }

    // One for the default vhost "/", one for "/a.b", one for "/a_b".
    EXPECT_EQ(directories, 3U);
    std::filesystem::remove_all(root);
}

}  // namespace mq::broker

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
