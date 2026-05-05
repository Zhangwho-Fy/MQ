#include "mq_connection.hpp"

int main(){
    //1.实例化异步工作线程池
    Fy_mq::AsyncWorker::ptr awp = std::make_shared<Fy_mq::AsyncWorker>();
    //2.实例化连接对象
    Fy_mq::Connection::ptr conn = std::make_shared<Fy_mq::Connection>("127.0.0.1",8085,awp);
    //3.通过连接创建信道
    Fy_mq::Channel::ptr channel = conn->openChannel();
    //4.通过信道提供的服务完成所需
    // a. 声明一个交换机exchange1，交换机类型为主题
    google::protobuf::Map<std::string,std::string> tmp_map;
    channel->declareExchange("exchange1",Fy_mq::ExchangeType::TOPIC,true,false,tmp_map);
    // b. 声明一个队列queue1
    channel->declareQueue("queue1",true,false,false,tmp_map);
    // c.声明一个队列queue2
    channel->declareQueue("queue2",true,false,false,tmp_map);
    // d.绑定queue1-exchange1，且binding_key设置为queue1
    channel->QueueBind("exchange1","queue1","queue1");
    // e.绑定queue2-exchange1，且binding_key设置为news.music.#
    channel->QueueBind("exchange1","queue2","news.music.#");
    //5.向交换机发布消息
    for(int i = 0;i < 10;++i){
        Fy_mq::BasicProperties bp;
        bp.set_id(Fy_mq::UUIDHelper::uuid());
        bp.set_deliver_mode(Fy_mq::DeliverMode::DURABLE);
        bp.set_routing_key("news.music.pop");
        channel->basicPublish("exchange1", &bp, "Hello World-" + std::to_string(i));
    }
    Fy_mq::BasicProperties bp;
    bp.set_id(Fy_mq::UUIDHelper::uuid());
    bp.set_deliver_mode(Fy_mq::DeliverMode::DURABLE);
    bp.set_routing_key("news.music.sport");
    channel->basicPublish("exchange1", &bp, "Hello Fy");

    bp.set_routing_key("news.sport");
    channel->basicPublish("exchange1", &bp, "Hello sport?");

    //6.关闭信道
    conn->closeChannel(channel);
    
    return 0;
}