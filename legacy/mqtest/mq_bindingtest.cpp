#include "../mqserver/mq_binding.hpp"
#include <gtest/gtest.h>

Fy_mq::BindingManager::ptr bmp;

class BindingTest : public testing::Environment{
public:
    virtual void SetUp() override {
        bmp = std::make_shared<Fy_mq::BindingManager>("./data/meta.db");
    }
    virtual void TearDown() override {
        bmp->clear();
    }
};

// 1. 批量添加绑定 → 总数6
TEST(binding_test, step1_bind_6_items) {
    bmp->bind("exchange1", "queue1", "news.music.#", true);
    bmp->bind("exchange1", "queue2", "news.sport.#", true);
    bmp->bind("exchange1", "queue3", "news.gossip.#", true);
    bmp->bind("exchange2", "queue1", "news.music.pop", true);
    bmp->bind("exchange2", "queue2", "news.sport.football", true);
    bmp->bind("exchange2", "queue3", "news.gossip.#", true);
    ASSERT_EQ(bmp->size(), 6);
}

// 2. 重复绑定 → 数量不变
TEST(binding_test, step2_rebind_same) {
    bmp->bind("exchange1", "queue1", "news.music.#", true);
    ASSERT_EQ(bmp->size(), 6);
}

// 3. 存在性判断
TEST(binding_test, step3_exists_check) {
    ASSERT_TRUE(bmp->exists("exchange1", "queue1"));
    ASSERT_TRUE(bmp->exists("exchange1", "queue2"));
    ASSERT_TRUE(bmp->exists("exchange1", "queue3"));
    ASSERT_TRUE(bmp->exists("exchange2", "queue1"));
    ASSERT_TRUE(bmp->exists("exchange2", "queue2"));
    ASSERT_TRUE(bmp->exists("exchange2", "queue3"));

    ASSERT_FALSE(bmp->exists("no_ex", "queue1"));
    ASSERT_FALSE(bmp->exists("exchange1", "no_queue"));
}

// 4. 获取单个绑定
TEST(binding_test, step4_get_binding) {
    auto bp = bmp->getBinding("exchange1", "queue1");
    ASSERT_NE(bp, nullptr);
    ASSERT_EQ(bp->exchange_name, "exchange1");
    ASSERT_EQ(bp->msgqueue_name, "queue1");
    ASSERT_EQ(bp->binding_key, "news.music.#");
}

// 5. 获取交换机下所有绑定
TEST(binding_test, step5_get_exchange_bindings) {
    auto bindings = bmp->getExchangeBindings("exchange1");
    ASSERT_EQ(bindings.size(), 3);

    auto bindings2 = bmp->getExchangeBindings("exchange2");
    ASSERT_EQ(bindings2.size(), 3);
}

// 6. 解绑一个
TEST(binding_test, step6_unbind_one) {
    bmp->unBind("exchange1", "queue1");
    ASSERT_EQ(bmp->size(), 5);
    ASSERT_FALSE(bmp->exists("exchange1", "queue1"));
}

// 7. 删除交换机下所有绑定
TEST(binding_test, step7_remove_exchange_bindings) {
    bmp->removeExchangeBindings("exchange2");
    ASSERT_EQ(bmp->size(), 2);
}

// 8. 删除队列所有绑定
TEST(binding_test, step8_remove_queue_bindings) {
    bmp->removeMsgqueueBindings("queue3");
    ASSERT_EQ(bmp->size(), 1);
}

// 9. 清空所有
TEST(binding_test, step9_clear_all) {
    bmp->clear();
    ASSERT_EQ(bmp->size(), 0);
}

int main(int argc,char *argv[])
{
    testing::InitGoogleTest(&argc, argv);
    testing::AddGlobalTestEnvironment(new BindingTest);
    return RUN_ALL_TESTS();
}