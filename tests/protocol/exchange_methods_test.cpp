#include "mq/protocol/amqp091/exchange_methods.hpp"

#include <gtest/gtest.h>

namespace mq::amqp091 {

TEST(ExchangeMethodsTest, DeclareRoundTrip) {
    ExchangeDeclare declare;
    declare.ticket = 0;
    declare.exchange = "logs";
    declare.type = "fanout";
    declare.durable = true;
    declare.auto_delete = true;
    declare.no_wait = false;
    declare.arguments.addString("x-key", "value");

    ExchangeDeclare decoded;
    std::string error;
    ASSERT_TRUE(decodeExchangeDeclare(encodeExchangeDeclare(declare), decoded,
                                      error))
        << error;
    EXPECT_EQ(decoded.ticket, 0U);
    EXPECT_EQ(decoded.exchange, "logs");
    EXPECT_EQ(decoded.type, "fanout");
    EXPECT_TRUE(decoded.durable);
    EXPECT_TRUE(decoded.auto_delete);
    EXPECT_FALSE(decoded.no_wait);
    const std::string* value = decoded.arguments.findString("x-key");
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(*value, "value");
}

TEST(ExchangeMethodsTest, DeleteRoundTrip) {
    ExchangeDelete delete_exchange;
    delete_exchange.exchange = "logs";
    delete_exchange.if_unused = true;
    delete_exchange.no_wait = true;

    ExchangeDelete decoded;
    std::string error;
    ASSERT_TRUE(decodeExchangeDelete(encodeExchangeDelete(delete_exchange),
                                     decoded, error))
        << error;
    EXPECT_EQ(decoded.exchange, "logs");
    EXPECT_TRUE(decoded.if_unused);
    EXPECT_TRUE(decoded.no_wait);
}

TEST(ExchangeMethodsTest, RejectsTruncatedDeclare) {
    ExchangeDeclare decoded;
    std::string error;
    EXPECT_FALSE(decodeExchangeDeclare(std::string_view("\x00\x00", 2),
                                       decoded, error));
}

}  // namespace mq::amqp091

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
