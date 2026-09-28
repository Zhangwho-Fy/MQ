#include "common/mq_logger.hpp"
#include "mq/cli/options.hpp"
#include "mq/cli/users.hpp"
#include "mq/transport/amqp_connection.hpp"

#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <string>

namespace {

mq::transport::AmqpServer* g_server = nullptr;

void handleStopSignal(int) {
    if (g_server != nullptr) g_server->requestStop();
}

// Applies MQ_USERS to the server and logs whatever the parser rejected.
void configureUsersFromEnv(mq::transport::AmqpServer* server) {
    if (server == nullptr) return;

    const mq::cli::UsersParseResult result = mq::cli::loadUsersFromEnv(
        [server](const mq::cli::UserEntry& user) {
            return server->addUser(user.name, user.password, user.vhost);
        });
    for (const std::string& line : result.errors) {
        ELOG("%s", line.c_str());
    }
    
    if (result.accepted != 0) {
        ILOG("configured %zu user(s) from MQ_USERS",
             static_cast<std::size_t>(result.accepted));
    }
}

}  // namespace

int main(int argc, char** argv) {
    // 1) Parse: structural validation only.
    const mq::cli::ParseResult parsed = mq::cli::parseOptions(argc, argv);
    if (parsed.help_requested) {
        mq::cli::handleHelp(argv[0], "");
        return 0;
    }

    if (!parsed.ok) {
        mq::cli::handleHelp(argv[0], parsed.error);
        return 2;
    }

    // 2) Apply: defaults, number parsing, range checks.
    mq::cli::ServerOptions options;
    std::string error;
    if (!mq::cli::applyOptions(parsed, &options, &error)) {
        mq::cli::handleHelp(argv[0], error);
        return 2;
    }

    // 3) Assemble the configuration and start.
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

    config.heartbeat = options.heartbeat;

    ILOG("starting AMQP server on 0.0.0.0:%u",
         static_cast<unsigned>(options.port));
    if (options.http_port != 0) {
        ILOG("management HTTP API on port %u",
             static_cast<unsigned>(options.http_port));
    }

    if (!options.data_dir.empty()) {
        ILOG("data directory: %s", options.data_dir.c_str());
    }

    mq::transport::AmqpServer server(options.port, config, options.data_dir,
                                     options.http_port);
    g_server = &server;
    std::signal(SIGINT, handleStopSignal);
    std::signal(SIGTERM, handleStopSignal);
    configureUsersFromEnv(&server);
    server.run();
    return 0;
}
