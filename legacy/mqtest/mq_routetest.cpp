#include "../mqserver/mq_route.hpp"
#include <gtest/gtest.h>

class RouteTest : public testing::Environment {
    public:
        virtual void SetUp() override {

        }

        virtual void TearDown() override {

        }
};

TEST(route_test,legal_routing_key) {
    std::string rkey1 = "news.music.cpp";
    std::string rkey2 = "news..music.pop";
    std::string rkey3 = "news.,music.pop";
    std::string rkey4 = "news.music_123.cpp";
    ASSERT_EQ(Fy_mq::Router::isLegalRoutingKey(rkey1), true);
    ASSERT_EQ(Fy_mq::Router::isLegalRoutingKey(rkey2), true);
    ASSERT_EQ(Fy_mq::Router::isLegalRoutingKey(rkey3), false);
    ASSERT_EQ(Fy_mq::Router::isLegalRoutingKey(rkey4), true);
}

TEST(route_test,legal_binding_key) {
    std::string bkey1 = "news.music.pop";
    std::string bkey2 = "news.#.music.pop";
    std::string bkey3 = "news.#.*.music.pop";//
    std::string bkey4 = "news.*.#.music.pop";//
    std::string bkey5 = "news.#.#.music.pop";//
    std::string bkey6 = "news.*.*.music.pop";
    std::string bkey7 = "news.,music_123.pop";//

    ASSERT_EQ(Fy_mq::Router::isLegalBindingKey(bkey1), true);
    ASSERT_EQ(Fy_mq::Router::isLegalBindingKey(bkey2), true);
    ASSERT_EQ(Fy_mq::Router::isLegalBindingKey(bkey3), false);
    ASSERT_EQ(Fy_mq::Router::isLegalBindingKey(bkey4), false);
    ASSERT_EQ(Fy_mq::Router::isLegalBindingKey(bkey5), false);
    ASSERT_EQ(Fy_mq::Router::isLegalBindingKey(bkey6), true);
    ASSERT_EQ(Fy_mq::Router::isLegalBindingKey(bkey7), false);
}

TEST(route_test,route) {
     std::vector<std::string> bkeys = {
        "aaa",
        "aaa.bbb",
        "aaa.bbb", 
        "aaa.bbb",
        "aaa.#.bbb",
        "aaa.bbb.#",
        "#.bbb.ccc",
        "aaa.bbb.ccc",
        "aaa.*",
        "aaa.*.bbb",
        "*.aaa.bbb", 
        "#",   
        "aaa.#", 
        "aaa.#",  
        "aaa.#.ccc",
        "aaa.#.ccc",
        "aaa.#.ccc",
        "#.ccc",
        "#.ccc",
        "aaa.#.ccc.ccc",
        "aaa.#.bbb.*.bbb"
    };
    std::vector<std::string> rkeys = {
        "aaa",
        "aaa.bbb",    
        "aaa.bbb.ccc",        
        "aaa.ccc",        
        "aaa.bbb.ccc",        
        "aaa.ccc.bbb",        
        "aaa.bbb.ccc.ddd",    
        "aaa.bbb.ccc",       
        "aaa.bbb",         
        "aaa.bbb.ccc",      
        "aaa.bbb",         
        "aaa.bbb.ccc",       
        "aaa.bbb",        
        "aaa.bbb.ccc",     
        "aaa.ccc",        
        "aaa.bbb.ccc",       
        "aaa.aaa.bbb.ccc",  
        "ccc",         
        "aaa.bbb.ccc",    
        "aaa.bbb.ccc.ccc.ccc",
        "aaa.ddd.ccc.bbb.eee.bbb"
    };
    std::vector<bool> result = {
        true,
        true,
        false,
        false,
        false,
        false,
        false,
        true,
        true,
        false,
        false,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true
    };
    for (int i = 0; i < bkeys.size(); i++) {
        ASSERT_EQ(Fy_mq::Router::route(Fy_mq::ExchangeType::TOPIC, rkeys[i], bkeys[i]), result[i]);
        
    }
}

int main(int argc,char *argv[]){
    testing::InitGoogleTest(&argc,argv);
    testing::AddGlobalTestEnvironment(new RouteTest);
    return RUN_ALL_TESTS();
}