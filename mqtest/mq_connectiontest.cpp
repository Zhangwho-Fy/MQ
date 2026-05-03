#include "../mqserver/mq_connection.hpp"

int main()
{
    Fy_mq::ConnectionManager::ptr cmp = std::make_shared<Fy_mq::ConnectionManager>();
    cmp->newConnection(std::make_shared<Fy_mq::VirtualHost>("host1", "./data/host1/message/", "./data/host1/host1.db"),
        std::make_shared<Fy_mq::ConsumerManager>(),
        Fy_mq::ProtobufCodecPtr(),
        muduo::net::TcpConnectionPtr(),
        threadpool::ptr());
    return 0;
}