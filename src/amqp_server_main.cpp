#include "common/mq_logger.hpp"
#include "mq/transport/amqp_connection.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <string>

namespace {
mq::transport::AmqpServer* g_server = nullptr;

void handleStopSignal(int) {
    if (g_server != nullptr) g_server->requestStop();
}
}  // namespace

int main(int argc, char** argv) {
    uint16_t port = 5672;
    uint16_t heartbeat_seconds = 60;
    uint16_t http_port = 0;
    std::string data_dir;
    for (int i = 1; i < argc; ++i) {
        if ((std::strcmp(argv[i], "-p") == 0 ||
             std::strcmp(argv[i], "--port") == 0) &&
            i + 1 < argc) {
            port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--heartbeat") == 0 &&
                   i + 1 < argc) {
            heartbeat_seconds =
                static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--data") == 0 &&
                   i + 1 < argc) {
            data_dir = argv[++i];
        } else if (std::strcmp(argv[i], "--http-port") == 0 &&
                   i + 1 < argc) {
            http_port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "-h") == 0 ||
                   std::strcmp(argv[i], "--help") == 0) {
            std::printf(
                "usage: amqp_server [-p PORT] [--heartbeat SECONDS] "
                "[--data DIR] [--http-port PORT]\n");
            return 0;
        }
    }

    mq::amqp091::ConnectionConfig config;
    if (const char* user = std::getenv("MQ_AMQP_USER"); user != nullptr) {
        config.username = user;
    }
    if (const char* password = std::getenv("MQ_AMQP_PASSWORD");
        password != nullptr) {
        config.password = password;
    }
    if (const char* vhost = std::getenv("MQ_AMQP_VHOST"); vhost != nullptr) {
        config.virtual_host = vhost;
    }
    config.heartbeat = heartbeat_seconds;

    ILOG("starting AMQP server on 0.0.0.0:%u", static_cast<unsigned>(port));
    mq::transport::AmqpServer server(port, config, data_dir, http_port);
    g_server = &server;
    std::signal(SIGINT, handleStopSignal);
    std::signal(SIGTERM, handleStopSignal);
    if (const char* users = std::getenv("MQ_USERS"); users != nullptr) {
        std::string all = users;
        size_t start = 0;
        while (start <= all.size()) {
            const size_t sep = all.find(';', start);
            const std::string entry =
                all.substr(start, sep == std::string::npos
                                       ? std::string::npos
                                       : sep - start);
            const size_t colon1 = entry.find(':');
            const size_t colon2 =
                colon1 == std::string::npos
                    ? std::string::npos
                    : entry.find(':', colon1 + 1);
            if (colon1 != std::string::npos &&
                colon2 != std::string::npos) {
                const std::string name = entry.substr(0, colon1);
                const std::string password =
                    entry.substr(colon1 + 1, colon2 - colon1 - 1);
                const std::string vhost_name = entry.substr(colon2 + 1);
                server.addUser(name, password, vhost_name);
                ILOG("configured user %s for vhost %s", name.c_str(),
                     vhost_name.c_str());
            }
            if (sep == std::string::npos) break;
            start = sep + 1;
        }
    }
    server.run();
    return 0;
}
