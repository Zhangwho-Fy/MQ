#include "mq/protocol/amqp091/exchange_methods.hpp"

#include <gtest/gtest.h>

namespace mq::amqp091 {

TEST(ExchangeMethodsTest, RoundTripsDeclareAndDelete) {
    std::string error;

    ExchangeDeclare declare;
    declare.exchange = "logs";
    declare.type = "topic";
    declare.durable = true;
    declare.auto_delete = true;
    ExchangeDeclare decoded_declare;
    ASSERT_TRUE(decodeExchangeDeclare(encodeExchangeDeclare(declare),
                                      decoded_declare, error))
        << error;
    EXPECT_EQ(decoded_declare.exchange, "logs");
    EXPECT_EQ(decoded_declare.type, "topic");
    EXPECT_TRUE(decoded_declare.durable);
    EXPECT_TRUE(decoded_declare.auto_delete);
    EXPECT_FALSE(decoded_declare.passive);

    ExchangeDelete remove;
    remove.exchange = "logs";
    remove.if_unused = true;
    ExchangeDelete decoded_delete;
    ASSERT_TRUE(
        decodeExchangeDelete(encodeExchangeDelete(remove), decoded_delete,
                             error))
        << error;
    EXPECT_EQ(decoded_delete.exchange, "logs");
    EXPECT_TRUE(decoded_delete.if_unused);
}

TEST(ExchangeMethodsTest, RejectsTruncatedDeclare) {
    ExchangeDeclare decoded;
    std::string error;
    EXPECT_FALSE(decodeExchangeDeclare(std::string_view("\x00\x00", 2),
                                       decoded, error));
}

// passive is the first bit field (0x01), durable the second (0x02).
TEST(ExchangeMethodsTest, DeclarePacksBitsLeastSignificantFirst) {
    ExchangeDeclare declare;
    declare.exchange = "ex";
    declare.type = "direct";
    declare.passive = true;

    const std::size_t bits_offset = 2 + (1 + 2) + (1 + 6);  // ticket + 2 shortstr
    const std::string encoded = encodeExchangeDeclare(declare);
    ASSERT_GT(encoded.size(), bits_offset);
    EXPECT_EQ(static_cast<unsigned char>(encoded[bits_offset]), 0x01U);

    declare.passive = false;
    declare.durable = true;
    const std::string encoded2 = encodeExchangeDeclare(declare);
    ASSERT_GT(encoded2.size(), bits_offset);
    EXPECT_EQ(static_cast<unsigned char>(encoded2[bits_offset]), 0x02U);
}

}  // namespace mq::amqp091

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
