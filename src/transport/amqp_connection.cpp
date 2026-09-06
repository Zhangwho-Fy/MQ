#include "mq/transport/amqp_connection.hpp"

#include "common/mq_logger.hpp"

#include "muduo/net/EventLoop.h"
#include "muduo/net/http/HttpRequest.h"
#include "muduo/net/http/HttpResponse.h"

#include <chrono>
#include <map>
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

std::string base64Decode(const std::string& input) {
    static const std::string table =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int buffer = 0;
    int bits = 0;
    for (char ch : input) {
        if (ch == '=') break;
        const size_t pos = table.find(ch);
        if (pos == std::string::npos) continue;
        buffer = (buffer << 6) | static_cast<int>(pos);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((buffer >> bits) & 0xff));
        }
    }
    return out;
}

std::map<std::string, std::string> parseQuery(
    const std::string& query) {
    std::map<std::string, std::string> result;
    size_t start = 0;
    while (start <= query.size()) {
        const size_t amp = query.find('&', start);
        const std::string pair =
            query.substr(start, amp == std::string::npos
                                     ? std::string::npos
                                     : amp - start);
        const size_t eq = pair.find('=');
        if (eq != std::string::npos) {
            result[pair.substr(0, eq)] = pair.substr(eq + 1);
        }
        if (amp == std::string::npos) break;
        start = amp + 1;
    }
    return result;
}

}  // namespace

AmqpConnectionHandler::AmqpConnectionHandler(
    const amqp091::ConnectionConfig& config,
    const muduo::net::TcpConnectionPtr& connection,
    std::shared_ptr<broker::Broker> broker)
    : connection_(connection),
      session_(config,
               [this](const std::string& bytes) { send(bytes); },
               broker ? broker->vhost("/") : nullptr,
               [broker](const std::string& username,
                        const std::string& password) {
                   return broker &&
                          broker->authenticate(username, password);
               },
               [broker](const std::string& username,
                        const std::string& vhost_name) {
                   return broker ? broker->resolveVhost(username, vhost_name)
                                 : nullptr;
               }),
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
      broker_(std::make_shared<broker::Broker>(std::move(data_dir))),
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
    loop_.runEvery(0.2, [this]() {
        if (stop_requested_ != 0) {
            ILOG("graceful shutdown requested");
            loop_.quit();
        }
    });
    loop_.loop();
}

void AmqpServer::requestStop() {
    stop_requested_ = 1;
}

bool AmqpServer::addUser(const std::string& username,
                         const std::string& password,
                         const std::string& vhost_name) {
    return broker_->addUser(username, password, {vhost_name});
}

void AmqpServer::onConnection(
    const muduo::net::TcpConnectionPtr& connection) {
    if (connection->connected()) {
        ILOG("AMQP connection established: %s",
             connection->peerAddress().toIpPort().c_str());
        auto handler = std::make_shared<AmqpConnectionHandler>(
            config_, connection, broker_);
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
    const std::string full_path = request.path();
    const size_t question = full_path.find('?');
    const std::string path =
        question == std::string::npos
            ? full_path
            : full_path.substr(0, question);
    std::string query_string =
        request.query().empty() && question != std::string::npos
            ? full_path.substr(question + 1)
            : request.query();

    const auto query = parseQuery(query_string);
    const auto vhost_it = query.find("virtual_host");
    const auto header_vhost = request.headers().find("X-Virtual-Host");
    const std::string vhost_name =
        header_vhost != request.headers().end()
            ? header_vhost->second
            : (vhost_it == query.end() ? "/" : vhost_it->second);
    const auto auth_it = request.headers().find("Authorization");
    if (auth_it == request.headers().end()) {
        response->setStatusCode(muduo::net::HttpResponse::k400BadRequest);
        response->setStatusMessage("Unauthorized");
        response->setBody("{\"error\":\"missing authorization\"}");
        return;
    }
    const std::string& header = auth_it->second;
    const std::string token =
        header.size() > 6 && header.compare(0, 6, "Basic ") == 0
            ? header.substr(6)
            : "";
    const std::string decoded = base64Decode(token);
    const size_t colon = decoded.find(':');
    if (colon == std::string::npos ||
        !broker_->authenticate(decoded.substr(0, colon),
                                decoded.substr(colon + 1))) {
        response->setStatusCode(muduo::net::HttpResponse::k400BadRequest);
        response->setStatusMessage("Unauthorized");
        response->setBody("{\"error\":\"unauthorized\"}");
        return;
    }
    const std::shared_ptr<broker::VirtualHost> vhost =
        broker_->resolveVhost(decoded.substr(0, colon), vhost_name);
    if (!vhost) {
        response->setStatusCode(muduo::net::HttpResponse::k404NotFound);
        response->setStatusMessage("Not Found");
        response->setBody("{\"error\":\"vhost not allowed\"}");
        return;
    }

    if (path == "/api/overview") {
        const auto queues = vhost->listQueues();
        const auto exchanges = vhost->listExchanges();
        response->setBody(
            "{\"connections\":" + std::to_string(connectionCount()) +
            ",\"queues\":" + std::to_string(queues.size()) +
            ",\"exchanges\":" + std::to_string(exchanges.size()) +
            ",\"published\":" + std::to_string(vhost->publishedCount()) +
            ",\"acked\":" + std::to_string(vhost->ackedCount()) + "}");
        return;
    }

    if (path == "/api/connections") {
        std::string body = "[";
        bool first = true;
        {
            std::lock_guard<std::mutex> lock(connections_mutex_);
            for (const auto& entry : connections_) {
                if (!first) body += ",";
                first = false;
                body += "{\"peer\":\"" +
                        jsonEscape(entry.first->peerAddress().toIpPort()) +
                        "\"}";
            }
        }
        body += "]";
        response->setBody(body);
        return;
    }

    if (path == "/api/queues") {
        std::string body = "[";
        bool first = true;
        for (const auto& queue : vhost->listQueues()) {
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
        for (const auto& exchange : vhost->listExchanges()) {
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
                vhost->deleteQueue(queue_name, false, false);
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
        for (const auto& queue : vhost->listQueues()) {
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
