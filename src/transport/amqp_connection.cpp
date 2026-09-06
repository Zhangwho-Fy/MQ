#include "mq/transport/amqp_connection.hpp"

#include "common/mq_logger.hpp"

#include "muduo/net/EventLoop.h"
#include "muduo/net/http/HttpRequest.h"
#include "muduo/net/http/HttpResponse.h"

#include <chrono>
#include <utility>

namespace mq::transport {

namespace {

std::string jsonEscape(const std::string& value) {
    std::string out;
    for (char ch : value) {
        if (ch == '"' || ch == '\\') {
            out.push_back('\\');
            out.push_back(ch);
        } else if (ch == '\n') {
            out += "\\n";
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

}  // namespace

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
                      std::string data_dir, uint16_t management_port)
    : server_(&loop_, muduo::net::InetAddress(port), "AmqpServer"),
      config_(config),
      virtual_host_(std::make_shared<broker::VirtualHost>(
          std::move(data_dir))),
      management_port_(management_port) {
    server_.setThreadNum(4);
    server_.setConnectionCallback(
        std::bind(&AmqpServer::onConnection, this, std::placeholders::_1));
    server_.setMessageCallback(std::bind(
        &AmqpServer::onMessage, this, std::placeholders::_1,
        std::placeholders::_2, std::placeholders::_3));
    if (management_port != 0) {
        http_server_ = std::make_unique<muduo::net::HttpServer>(
            &loop_, muduo::net::InetAddress(management_port), "MgmtServer");
        http_server_->setHttpCallback(
            std::bind(&AmqpServer::onHttpRequest, this,
                      std::placeholders::_1, std::placeholders::_2));
    }
}

void AmqpServer::run() {
    server_.start();
    ILOG("AMQP server listening on %s", server_.ipPort().c_str());
    if (http_server_) {
        http_server_->start();
        ILOG("management HTTP server listening on port %u",
             static_cast<unsigned>(management_port_));
    }
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
        std::lock_guard<std::mutex> lock(connections_mutex_);
        connections_[connection] = std::move(handler);
    } else {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        connections_.erase(connection);
    }
}

void AmqpServer::onMessage(const muduo::net::TcpConnectionPtr& connection,
                           muduo::net::Buffer* buffer, muduo::Timestamp) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    const auto it = connections_.find(connection);
    if (it != connections_.end()) {
        it->second->onMessage(buffer);
    } else {
        buffer->retrieveAll();
    }
}

size_t AmqpServer::connectionCount() {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    return connections_.size();
}

void AmqpServer::onHttpRequest(const muduo::net::HttpRequest& request,
                               muduo::net::HttpResponse* response) {
    response->setContentType("application/json");
    response->setStatusCode(muduo::net::HttpResponse::k200Ok);
    response->setStatusMessage("OK");
    const std::string& path = request.path();

    if (path == "/api/overview") {
        const auto queues = virtual_host_->listQueues();
        const auto exchanges = virtual_host_->listExchanges();
        response->setBody(
            "{\"connections\":" + std::to_string(connectionCount()) +
            ",\"queues\":" + std::to_string(queues.size()) +
            ",\"exchanges\":" + std::to_string(exchanges.size()) + "}");
        return;
    }

    if (path == "/api/queues") {
        std::string body = "[";
        bool first = true;
        for (const auto& queue : virtual_host_->listQueues()) {
            if (!first) body += ",";
            first = false;
            body += "{\"name\":\"" + jsonEscape(queue.name) +
                    "\",\"message_count\":" +
                    std::to_string(queue.message_count) +
                    ",\"consumer_count\":" +
                    std::to_string(queue.consumer_count) +
                    ",\"durable\":" +
                    std::string(queue.durable ? "true" : "false") +
                    ",\"exclusive\":" +
                    std::string(queue.exclusive ? "true" : "false") +
                    ",\"auto_delete\":" +
                    std::string(queue.auto_delete ? "true" : "false") +
                    ",\"dead_letter_exchange\":\"" +
                    jsonEscape(queue.dead_letter_exchange) +
                    "\",\"message_ttl_ms\":" +
                    std::to_string(queue.message_ttl_ms) + "}";
        }
        body += "]";
        response->setBody(body);
        return;
    }

    if (path == "/api/exchanges") {
        std::string body = "[";
        bool first = true;
        for (const auto& exchange : virtual_host_->listExchanges()) {
            if (!first) body += ",";
            first = false;
            body += "{\"name\":\"" + jsonEscape(exchange.name) +
                    "\",\"type\":\"" + jsonEscape(exchange.type) +
                    "\",\"durable\":" +
                    std::string(exchange.durable ? "true" : "false") +
                    ",\"auto_delete\":" +
                    std::string(exchange.auto_delete ? "true" : "false") +
                    ",\"internal\":" +
                    std::string(exchange.internal ? "true" : "false") + "}";
        }
        body += "]";
        response->setBody(body);
        return;
    }

    if (path.rfind("/api/queues/", 0) == 0) {
        const std::string queue_name = path.substr(std::string("/api/queues/").size());
        if (request.method() == muduo::net::HttpRequest::kDelete) {
            const broker::BrokerResult result =
                virtual_host_->deleteQueue(queue_name, false, false);
            if (result.ok) {
                response->setBody("{\"deleted\":true}");
            } else {
                response->setStatusCode(
                    muduo::net::HttpResponse::k404NotFound);
                response->setStatusMessage("Not Found");
                response->setBody("{\"error\":\"queue not found\"}");
            }
            return;
        }
        for (const auto& queue : virtual_host_->listQueues()) {
            if (queue.name == queue_name) {
                response->setBody("{\"name\":\"" + jsonEscape(queue.name) +
                                  "\",\"message_count\":" +
                                  std::to_string(queue.message_count) + "}");
                return;
            }
        }
        response->setStatusCode(muduo::net::HttpResponse::k404NotFound);
        response->setStatusMessage("Not Found");
        response->setBody("{\"error\":\"queue not found\"}");
        return;
    }

    response->setStatusCode(muduo::net::HttpResponse::k404NotFound);
    response->setStatusMessage("Not Found");
    response->setBody("{\"error\":\"not found\"}");
}

}  // namespace mq::transport
