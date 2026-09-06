#include "mq/protocol/amqp091/confirm_methods.hpp"

#include <gtest/gtest.h>

namespace mq::amqp091 {

TEST(ConfirmMethodsTest, SelectRoundTrip) {
    ConfirmSelect select;
    select.no_wait = true;
    ConfirmSelect decoded;
    std::string error;
    ASSERT_TRUE(decodeConfirmSelect(encodeConfirmSelect(select), decoded,
                                    error))
        << error;
    EXPECT_TRUE(decoded.no_wait);
    EXPECT_EQ(encodeConfirmSelectOk(), std::string());
}

}  // namespace mq::amqp091

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
