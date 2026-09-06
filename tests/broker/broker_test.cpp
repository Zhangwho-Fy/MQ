#include "mq/broker/broker.hpp"

#include <gtest/gtest.h>

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

}  // namespace mq::broker

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
