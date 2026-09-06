#include "common/mq_logger.hpp"
#include "mq/transport/amqp_connection.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char** argv) {
    uint16_t port = 5672;
    uint16_t heartbeat_seconds = 60;
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
        } else if (std::strcmp(argv[i], "-h") == 0 ||
                   std::strcmp(argv[i], "--help") == 0) {
            std::printf(
                "usage: amqp_server [-p PORT] [--heartbeat SECONDS] "
                "[--data DIR]\n");
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
    mq::transport::AmqpServer server(port, config, data_dir);
    server.run();
    return 0;
}
