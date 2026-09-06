#include "mq/transport/amqp_connection.hpp"

#include "common/mq_logger.hpp"

#include "muduo/net/EventLoop.h"

#include <chrono>
#include <utility>

namespace mq::transport {

AmqpConnectionHandler::AmqpConnectionHandler(
    const amqp091::ConnectionConfig& config,
    const muduo::net::TcpConnectionPtr& connection,
    std::shared_ptr<broker::VirtualHost> virtual_host)
    : connection_(connection),
      session_(config,
               [this](const std::string& bytes) { send(bytes); },
               std::move(virtual_host)),
      last_receive_(std::chrono::steady_clock::now()),
      heartbeat_interval_(config.heartbeat) {}

AmqpConnectionHandler::~AmqpConnectionHandler() {
    if (heartbeat_started_ && connection_) {
        connection_->getLoop()->cancel(heartbeat_timer_);
    }
}

void AmqpConnectionHandler::onMessage(muduo::net::Buffer* buffer) {
    if (closing_) {
        buffer->retrieveAll();
        return;
    }
    last_receive_ = std::chrono::steady_clock::now();
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

void AmqpConnectionHandler::startHeartbeat() {
    if (heartbeat_started_ || heartbeat_interval_ == 0 || !connection_) return;
    muduo::net::EventLoop* loop = connection_->getLoop();
    if (loop == nullptr) return;
    heartbeat_timer_ = loop->runEvery(
        static_cast<double>(heartbeat_interval_),
        [weak = weak_from_this()]() {
            if (const auto self = weak.lock()) self->onHeartbeat();
        });
    heartbeat_started_ = true;
}

void AmqpConnectionHandler::onHeartbeat() {
    if (closing_ || !connection_ || !connection_->connected()) return;
    const auto now = std::chrono::steady_clock::now();
    const auto idle_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_receive_)
            .count();
    if (idle_ms > static_cast<int64_t>(heartbeat_interval_) * 2000) {
        ELOG("AMQP heartbeat timeout, closing connection: %s",
             connection_->peerAddress().toIpPort().c_str());
        closing_ = true;
        connection_->shutdown();
        return;
    }
    const std::string heartbeat =
        mq::amqp091::FrameEncoder::encode(
            mq::amqp091::Frame{
                mq::amqp091::kFrameHeartbeat, 0, std::string{}},
            0);
    connection_->send(heartbeat.data(),
                      static_cast<int>(heartbeat.size()));
}

void AmqpConnectionHandler::send(const std::string& bytes) {
    if (!connection_) return;
    muduo::net::EventLoop* loop = connection_->getLoop();
    if (loop->isInLoopThread()) {
        sendInLoop(bytes);
    } else {
        loop->runInLoop([weak = weak_from_this(), bytes]() {
            if (const auto self = weak.lock()) self->sendInLoop(bytes);
        });
    }
}

void AmqpConnectionHandler::sendInLoop(const std::string& bytes) {
    if (connection_ && connection_->connected()) {
        connection_->send(bytes.data(), static_cast<int>(bytes.size()));
    }
}

AmqpServer::AmqpServer(uint16_t port, const amqp091::ConnectionConfig& config,
                       std::string data_dir)
    : server_(&loop_, muduo::net::InetAddress(port), "AmqpServer"),
      config_(config),
      virtual_host_(std::make_shared<broker::VirtualHost>(
          std::move(data_dir))) {
    server_.setThreadNum(4);
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
        auto handler = std::make_shared<AmqpConnectionHandler>(
            config_, connection, virtual_host_);
        handler->startHeartbeat();
        connections_[connection] = std::move(handler);
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
