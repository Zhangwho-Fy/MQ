#include "mq/protocol/amqp091/wire_reader.hpp"
#include "mq/protocol/amqp091/wire_writer.hpp"

#include <gtest/gtest.h>

namespace mq::amqp091 {

TEST(WireTypesTest, UsesNetworkByteOrder) {
    WireWriter writer;
    writer.writeU8(0x7f);
    writer.writeU16(0x1234);
    writer.writeU32(0x12345678);
    writer.writeU64(0x0123456789abcdefULL);
    writer.writeBytes(std::string_view("a\0b", 3));

    const std::string& bytes = writer.bytes();
    EXPECT_EQ(bytes.size(), 1U + 2U + 4U + 8U + 3U);
    EXPECT_EQ(static_cast<unsigned char>(bytes[0]), 0x7f);
    EXPECT_EQ(static_cast<unsigned char>(bytes[1]), 0x12);
    EXPECT_EQ(static_cast<unsigned char>(bytes[2]), 0x34);
    EXPECT_EQ(static_cast<unsigned char>(bytes[3]), 0x12);
    EXPECT_EQ(static_cast<unsigned char>(bytes[7]), 0x01);

    WireReader reader(bytes);
    uint8_t u8 = 0;
    uint16_t u16 = 0;
    uint32_t u32 = 0;
    uint64_t u64 = 0;
    std::string_view binary;
    ASSERT_TRUE(reader.readU8(u8));
    ASSERT_TRUE(reader.readU16(u16));
    ASSERT_TRUE(reader.readU32(u32));
    ASSERT_TRUE(reader.readU64(u64));
    ASSERT_TRUE(reader.readBytes(3, binary));
    EXPECT_EQ(u8, 0x7f);
    EXPECT_EQ(u16, 0x1234);
    EXPECT_EQ(u32, 0x12345678U);
    EXPECT_EQ(u64, 0x0123456789abcdefULL);
    EXPECT_EQ(binary, std::string_view("a\0b", 3));
    EXPECT_EQ(reader.remaining(), 0U);
}

TEST(WireTypesTest, RejectsTruncatedValues) {
    WireReader reader(std::string_view("\x01\x02", 2));
    uint32_t value = 0;
    EXPECT_FALSE(reader.readU32(value));
    EXPECT_EQ(reader.position(), 0U);
}

}  // namespace mq::amqp091

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
