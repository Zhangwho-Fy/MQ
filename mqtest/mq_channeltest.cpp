#include "../mqserver/mq_channel.hpp"

int main(){
    Fy_mq::ChannelManager::ptr cmp = std::make_shared<Fy_mq::ChannelManager>();
    cmp->openChannel("c1",
        std::make_shared<Fy_mq::VirtualHost>("host1","./data/host1/message/","./data/host1/host1.db"),
        std::make_shared<Fy_mq::ConsumerManager>(),
        Fy_mq::ProtobufCodecPtr(),
        muduo::net::TcpConnectionPtr(),
        threadpool::ptr());
    return 0;
}