#ifndef MQ_TRANSPORT_AMQP_CONNECTION_HPP
#define MQ_TRANSPORT_AMQP_CONNECTION_HPP

#include "mq/broker/virtual_host.hpp"
#include "mq/protocol/amqp091/connection_session.hpp"

#include "muduo/net/Buffer.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/TcpConnection.h"
#include "muduo/net/TcpServer.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string_view>

namespace mq::transport {

class AmqpConnectionHandler {
public:
    AmqpConnectionHandler(const amqp091::ConnectionConfig& config,
                          const muduo::net::TcpConnectionPtr& connection,
                          std::shared_ptr<broker::VirtualHost> virtual_host);

    void onMessage(muduo::net::Buffer* buffer);
    const muduo::net::TcpConnectionPtr& connection() const { return connection_; }

private:
    void send(const std::string& bytes);

    muduo::net::TcpConnectionPtr connection_;
    amqp091::ConnectionSession session_;
    bool closing_ = false;
};

class AmqpServer {
public:
    explicit AmqpServer(uint16_t port,
                        const amqp091::ConnectionConfig& config =
                            amqp091::ConnectionConfig{});

    void run();

private:
    void onConnection(const muduo::net::TcpConnectionPtr& connection);
    void onMessage(const muduo::net::TcpConnectionPtr& connection,
                   muduo::net::Buffer* buffer, muduo::Timestamp);

    muduo::net::EventLoop loop_;
    muduo::net::TcpServer server_;
    amqp091::ConnectionConfig config_;
    std::shared_ptr<broker::VirtualHost> virtual_host_;
    std::map<muduo::net::TcpConnectionPtr,
             std::unique_ptr<AmqpConnectionHandler>>
        connections_;
};

}  // namespace mq::transport

#endif  // MQ_TRANSPORT_AMQP_CONNECTION_HPP
