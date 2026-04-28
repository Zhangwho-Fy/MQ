#include "../mqserver/mq_exchange.hpp"
#include <gtest/gtest.h>

Fy_mq::ExchangeManager::ptr emp;

class ExchangeTest : public testing::Environment{
public:
    virtual void SetUp() override{
        emp = std::make_shared<Fy_mq::ExchangeManager>("./data/meta.db");
    }
    virtual void TearDown() override{
        emp->clear();
        DLOG("最后的清理!!\n");
    }
};

TEST(exchange_test, insert_test) {
    google::protobuf::Map<std::string, std::string> map;

    map["k1"] = "v1";
    map["k2"] = "v2";

    emp->declareExchange("exchange1", Fy_mq::ExchangeType::DIRECT, true, false, map);
    emp->declareExchange("exchange2", Fy_mq::ExchangeType::DIRECT, true, false, map);
    emp->declareExchange("exchange3", Fy_mq::ExchangeType::DIRECT, true, false, map);
    emp->declareExchange("exchange4", Fy_mq::ExchangeType::DIRECT, true, false, map);
    ASSERT_EQ(emp->size(), 4);
}

TEST(exchange_test, remove_test) {
    emp->deleteExchange("exchange3");
    Fy_mq::Exchange::ptr exp = emp->selectExchange("exchange3");
    ASSERT_EQ(exp.get(), nullptr);
    ASSERT_EQ(emp->exists("exchange3"), false);
}

TEST(exchange_test, select_test) {
    ASSERT_EQ(emp->exists("exchange1"), true);
    ASSERT_EQ(emp->exists("exchange2"), true);
    ASSERT_EQ(emp->exists("exchange3"), false);
    ASSERT_EQ(emp->exists("exchange4"), true);
    Fy_mq::Exchange::ptr exp = emp->selectExchange("exchange2");
    ASSERT_NE(exp.get(), nullptr);
    ASSERT_EQ(exp->name, "exchange2");
    ASSERT_EQ(exp->durable, true);
    ASSERT_EQ(exp->auto_delete, false);
    ASSERT_EQ(exp->type, Fy_mq::ExchangeType::DIRECT);
    
    ASSERT_EQ(exp->args["k1"], "v1");
    ASSERT_EQ(exp->args["k2"], "v2");
}

int main(int argc,char*argv[]){
    testing::InitGoogleTest(&argc,argv);
    testing::AddGlobalTestEnvironment(new ExchangeTest);
    return RUN_ALL_TESTS();
}