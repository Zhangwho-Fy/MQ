#ifndef MQ_TRANSPORT_AMQP_CONNECTION_HPP
#define MQ_TRANSPORT_AMQP_CONNECTION_HPP

#include "mq/broker/virtual_host.hpp"
#include "mq/broker/broker.hpp"
#include "mq/protocol/amqp091/connection_session.hpp"

#include "muduo/net/Buffer.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/http/HttpServer.h"
#include "muduo/net/TcpConnection.h"
#include "muduo/net/TcpServer.h"

#include <cstdint>
#include <chrono>
#include <csignal>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace mq::transport {

class AmqpConnectionHandler
    : public std::enable_shared_from_this<AmqpConnectionHandler> {
public:
    AmqpConnectionHandler(const amqp091::ConnectionConfig& config,
                          const muduo::net::TcpConnectionPtr& connection,
                          std::shared_ptr<broker::Broker> broker);
    ~AmqpConnectionHandler();

    void onMessage(muduo::net::Buffer* buffer);
    void startHeartbeat();
    const muduo::net::TcpConnectionPtr& connection() const { return connection_; }

private:
    void send(const std::string& bytes);
    void sendInLoop(const std::string& bytes);
    void onHeartbeat();

    muduo::net::TcpConnectionPtr connection_;
    amqp091::ConnectionSession session_;
    bool closing_ = false;
    std::chrono::steady_clock::time_point last_receive_;
    uint16_t heartbeat_interval_ = 0;
    bool heartbeat_started_ = false;
    muduo::net::TimerId heartbeat_timer_;
};

class AmqpServer {
public:
    explicit AmqpServer(uint16_t port,
                        const amqp091::ConnectionConfig& config =
                            amqp091::ConnectionConfig{},
                        std::string data_dir = {},
                        uint16_t management_port = 0);

    void run();
    void requestStop();
    bool addUser(const std::string& username, const std::string& password,
                 const std::string& vhost_name);

private:
    void onConnection(const muduo::net::TcpConnectionPtr& connection);
    void onMessage(const muduo::net::TcpConnectionPtr& connection,
                   muduo::net::Buffer* buffer, muduo::Timestamp);
    void onHttpRequest(const muduo::net::HttpRequest& request,
                       muduo::net::HttpResponse* response);
    size_t connectionCount();

    muduo::net::EventLoop loop_;
    muduo::net::TcpServer server_;
    amqp091::ConnectionConfig config_;
    std::shared_ptr<broker::Broker> broker_;
    std::map<muduo::net::TcpConnectionPtr,
             std::shared_ptr<AmqpConnectionHandler>>
        connections_;
    mutable std::mutex connections_mutex_;
    std::unique_ptr<muduo::net::HttpServer> http_server_;
    uint16_t management_port_ = 0;
    volatile std::sig_atomic_t stop_requested_ = 0;
};

}  // namespace mq::transport

#endif  // MQ_TRANSPORT_AMQP_CONNECTION_HPP
