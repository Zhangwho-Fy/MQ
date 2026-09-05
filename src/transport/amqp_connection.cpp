#include "mq/transport/amqp_connection.hpp"

#include "common/mq_logger.hpp"

#include "muduo/net/EventLoop.h"

#include <utility>

namespace mq::transport {

AmqpConnectionHandler::AmqpConnectionHandler(
    const amqp091::ConnectionConfig& config,
    const muduo::net::TcpConnectionPtr& connection,
    std::shared_ptr<broker::VirtualHost> virtual_host)
    : connection_(connection),
      session_(config,
               [this](const std::string& bytes) { send(bytes); },
               std::move(virtual_host)) {}

void AmqpConnectionHandler::onMessage(muduo::net::Buffer* buffer) {
    if (closing_) {
        buffer->retrieveAll();
        return;
    }
    const std::string_view data(buffer->peek(), buffer->readableBytes());
    const amqp091::SessionResult result = session_.feed(data);
    buffer->retrieveAll();
    if (!result.ok) {
        ELOG("AMQP session error: %s (reply-code=%u)", result.error.c_str(),
             static_cast<unsigned>(result.reply_code));
        closing_ = true;
        connection_->shutdown();
    }
}

void AmqpConnectionHandler::send(const std::string& bytes) {
    if (connection_ && connection_->connected()) {
        connection_->send(bytes.data(), static_cast<int>(bytes.size()));
    }
}

AmqpServer::AmqpServer(uint16_t port, const amqp091::ConnectionConfig& config)
    : server_(&loop_, muduo::net::InetAddress(port), "AmqpServer"),
      config_(config),
      virtual_host_(std::make_shared<broker::VirtualHost>()) {
    server_.setConnectionCallback(
        std::bind(&AmqpServer::onConnection, this, std::placeholders::_1));
    server_.setMessageCallback(std::bind(
        &AmqpServer::onMessage, this, std::placeholders::_1,
        std::placeholders::_2, std::placeholders::_3));
}

void AmqpServer::run() {
    server_.start();
    ILOG("AMQP server listening on %s", server_.ipPort().c_str());
    loop_.loop();
}

void AmqpServer::onConnection(
    const muduo::net::TcpConnectionPtr& connection) {
    if (connection->connected()) {
        ILOG("AMQP connection established: %s",
             connection->peerAddress().toIpPort().c_str());
        connections_[connection] = std::make_unique<AmqpConnectionHandler>(
            config_, connection, virtual_host_);
    } else {
        connections_.erase(connection);
    }
}

void AmqpServer::onMessage(const muduo::net::TcpConnectionPtr& connection,
                           muduo::net::Buffer* buffer, muduo::Timestamp) {
    const auto it = connections_.find(connection);
    if (it != connections_.end()) {
        it->second->onMessage(buffer);
    } else {
        buffer->retrieveAll();
    }
}

}  // namespace mq::transport
