#include "../mqserver/mq_queue.hpp"
#include <gtest/gtest.h>
#include <vector>

Fy_mq::MsgQueueManager::ptr mqmp;

class QueueTest : public testing::Environment
{
public:
    virtual void SetUp() override
    {
        static std::once_flag flag;
        std::call_once(flag, [&]() {
            mqmp = std::make_shared<Fy_mq::MsgQueueManager>("./data/meta.db");
        });
    }

    virtual void TearDown() override
    {
        mqmp->clear();
    }
};

// 1. 初始化：创建4个队列 → size=4
TEST(queue_test, step1_declare_4_queues) {
    google::protobuf::Map<std::string, std::string> args;
    args["k1"] = "v1";

    mqmp->declareQueue("queue1", true, false, false, args);
    mqmp->declareQueue("queue2", true, false, false, args);
    mqmp->declareQueue("queue3", true, false, false, args);
    mqmp->declareQueue("queue4", true, false, false, args);

    ASSERT_EQ(mqmp->size(), 4);
}

// 2. 重复声明其中一个 → 总数不变
TEST(queue_test, step2_duplicate_declare) {
    google::protobuf::Map<std::string, std::string> args;
    bool ret = mqmp->declareQueue("queue1", true, false, false, args);

    ASSERT_TRUE(ret);
    ASSERT_EQ(mqmp->size(), 4);
}

// 3. 测试存在 + 查询
TEST(queue_test, step3_exists_and_select) {
    ASSERT_TRUE(mqmp->exists("queue1"));
    ASSERT_TRUE(mqmp->exists("queue2"));
    ASSERT_FALSE(mqmp->exists("queue_not_exist"));

    auto q = mqmp->selectQueue("queue3");
    ASSERT_NE(q, nullptr);
    ASSERT_EQ(q->name, "queue3");
}

// 4. 测试删除2个 → 剩下 2 个
TEST(queue_test, step4_delete_2_queues) {
    mqmp->deleteQueue("queue1");
    mqmp->deleteQueue("queue2");

    ASSERT_EQ(mqmp->size(), 2);
    ASSERT_FALSE(mqmp->exists("queue1"));
    ASSERT_FALSE(mqmp->exists("queue2"));
}

// 5. 测试获取全部队列 → 数量=2
TEST(queue_test, step5_all_queues) {
    auto queues = mqmp->allQueues();
    ASSERT_EQ(queues.size(), 2);
}

// 7. 测试非持久化不恢复
TEST(queue_test, step7_non_durable_no_recovery) {
    mqmp->clear();

    google::protobuf::Map<std::string, std::string> args;
    mqmp->declareQueue("temp_queue", false, false, false, args);

    mqmp.reset();
    mqmp = std::make_shared<Fy_mq::MsgQueueManager>("./data/meta.db");

    ASSERT_FALSE(mqmp->exists("temp_queue"));
}

// 8. 最后清空所有
TEST(queue_test, step8_clear_all) {
    mqmp->clear();
    ASSERT_EQ(mqmp->size(), 0);
}

int main(int argc,char *argv[]){
    testing::InitGoogleTest(&argc,argv);
    testing::AddGlobalTestEnvironment(new QueueTest);
    return RUN_ALL_TESTS();
}