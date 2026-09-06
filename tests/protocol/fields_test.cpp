#include "mq/protocol/amqp091/fields.hpp"

#include <gtest/gtest.h>

namespace mq::amqp091 {

TEST(FieldsTest, EncodesAndDecodesStringsAndBools) {
    FieldTable table;
    table.addBool("publisher_confirms", true);
    table.addString("product", "FyMQ");
    table.addString("version", "0.1.0");

    const std::string encoded = encodeFieldTable(table);
    FieldTable decoded;
    std::string error;
    ASSERT_TRUE(decodeFieldTable(encoded, decoded, error)) << error;

    bool confirms = false;
    ASSERT_TRUE(decoded.findBool("publisher_confirms", confirms));
    EXPECT_TRUE(confirms);
    const std::string* product = decoded.findString("product");
    ASSERT_NE(product, nullptr);
    EXPECT_EQ(*product, "FyMQ");
    const std::string* version = decoded.findString("version");
    ASSERT_NE(version, nullptr);
    EXPECT_EQ(*version, "0.1.0");
}

TEST(FieldsTest, EncodesAndDecodesNestedTable) {
    FieldTable capabilities;
    capabilities.addBool("publisher_confirms", true);
    capabilities.addBool("consumer_cancel_notify", false);
    const std::string capabilities_bytes = encodeFieldTable(capabilities);

    FieldTable table;
    table.addString("product", "FyMQ");
    table.addTable("capabilities", capabilities_bytes);

    const std::string encoded = encodeFieldTable(table);
    FieldTable decoded;
    std::string error;
    ASSERT_TRUE(decodeFieldTable(encoded, decoded, error)) << error;
    ASSERT_EQ(decoded.entries().size(), 2U);
    EXPECT_EQ(decoded.entries()[0].type, 'S');
    EXPECT_EQ(decoded.entries()[1].type, 'F');
    EXPECT_EQ(decoded.entries()[1].value, capabilities_bytes);
}

TEST(FieldsTest, ReadsInt32Arguments) {
    FieldTable table;
    table.addInt32("x-message-ttl", 60000);

    const std::string encoded = encodeFieldTable(table);
    FieldTable decoded;
    std::string error;
    ASSERT_TRUE(decodeFieldTable(encoded, decoded, error)) << error;
    int64_t ttl = 0;
    ASSERT_TRUE(decoded.findInt64("x-message-ttl", ttl));
    EXPECT_EQ(ttl, 60000);
}

TEST(FieldsTest, RejectsTruncatedTable) {
    FieldTable decoded;
    std::string error;
    EXPECT_FALSE(decodeFieldTable(std::string_view("\x00\x00\x00\x10x", 5),
                                  decoded, error));
    EXPECT_FALSE(error.empty());
}

}  // namespace mq::amqp091

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
