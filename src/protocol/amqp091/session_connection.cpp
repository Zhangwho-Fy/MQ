#include "mq/protocol/amqp091/connection_session.hpp"

#include <algorithm>
#include <cstddef>

namespace mq::amqp091 {

namespace {

bool decodePlainResponse(const std::string& response, std::string& user,
                         std::string& password) {
    const size_t first = response.find('\0');

    if (first == std::string::npos) {
        return false;
    }

    const size_t second = response.find('\0', first + 1);

    if (second == std::string::npos) {
        return false;
    }

    user = response.substr(first + 1, second - first - 1);
    password = response.substr(second + 1);
    return true;
}

}  // namespace

SessionResult ConnectionSession::handleConnectionMethod(
    const MethodHeader& header) {
    switch (static_cast<ConnectionMethodId>(header.method_id)) {
        case ConnectionMethodId::StartOk:
            return handleConnectionStartOk(header.arguments);
        case ConnectionMethodId::TuneOk:
            return handleConnectionTuneOk(header.arguments);
        case ConnectionMethodId::Open:
            return handleConnectionOpen(header.arguments);
        case ConnectionMethodId::Close:
            return handleConnectionClose(header.arguments);
        case ConnectionMethodId::CloseOk:
            return handleConnectionCloseOk(header.arguments);
        // Server-to-client only.
        case ConnectionMethodId::Start:
        case ConnectionMethodId::Secure:
        case ConnectionMethodId::SecureOk:
        case ConnectionMethodId::Tune:
        case ConnectionMethodId::OpenOk:
            break;
    }

    return fail("unsupported or unexpected connection method", 503);
}

SessionResult ConnectionSession::handleConnectionStartOk(
    std::string_view arguments) {
    if (state_ != ConnectionState::kWaitStartOk) {
        return fail("unexpected connection.start-ok", 503);
    }

    std::string error;
    ConnectionStartOk start_ok;
    if (!decodeConnectionStartOk(arguments, start_ok, error)) {
        return fail(error, 502);
    }

    if (start_ok.mechanism != "PLAIN") {
        return fail("unsupported SASL mechanism", 530);
    }

    std::string user;
    std::string password;
    if (!decodePlainResponse(start_ok.response, user, password)) {
        return fail("authentication failed", 403);
    }

    bool authenticated = false;
    if (auth_callback_) {
        authenticated = auth_callback_(user, password);
    } else {
        authenticated =
            user == config_.username && password == config_.password;
    }

    if (!authenticated) return fail("authentication failed", 403);
    authenticated_user_ = user;

    ConnectionTune tune;
    tune.channel_max = config_.channel_max;
    tune.frame_max = config_.frame_max;
    tune.heartbeat = config_.heartbeat;
    const SessionResult sent =
        sendMethod(kConnectionClassId,
                   static_cast<uint16_t>(ConnectionMethodId::Tune),
                   encodeConnectionTune(tune));
    if (!sent.ok) return sent;
    state_ = ConnectionState::kWaitTuneOk;
    return SessionResult{};
}

SessionResult ConnectionSession::handleConnectionTuneOk(
    std::string_view arguments) {
    if (state_ != ConnectionState::kWaitTuneOk) {
        return fail("unexpected connection.tune-ok", 503);
    }

    std::string error;
    ConnectionTune tune_ok;
    if (!decodeConnectionTune(arguments, tune_ok, error)) {
        return fail(error, 502);
    }

    if (tune_ok.frame_max != 0 && tune_ok.frame_max < kFrameMinSize) {
        return fail("frame-max below AMQP frame-min-size (4096)", 502);
    }

    channel_max_ =
        tune_ok.channel_max == 0
            ? config_.channel_max
            : std::min(config_.channel_max, tune_ok.channel_max);
    frame_max_ =
        tune_ok.frame_max == 0
            ? config_.frame_max
            : std::min(config_.frame_max, tune_ok.frame_max);
    heartbeat_ =
        (config_.heartbeat == 0 || tune_ok.heartbeat == 0)
            ? 0
            : std::min(config_.heartbeat, tune_ok.heartbeat);
    decoder_.setFrameMax(frame_max_);
    state_ = ConnectionState::kWaitOpen;
    return SessionResult{};
}

SessionResult ConnectionSession::handleConnectionOpen(
    std::string_view arguments) {
    if (state_ != ConnectionState::kWaitOpen) {
        return fail("unexpected connection.open", 503);
    }

    std::string error;
    ConnectionOpen open;
    if (!decodeConnectionOpen(arguments, open, error)) {
        return fail(error, 502);
    }

    std::string vhost_name =
        open.virtual_host.empty() ? "/" : open.virtual_host;
    if (vhost_resolver_) {
        if (authenticated_user_.empty()) {
            return fail("authentication required", 403);
        }

        virtual_host_ =
            vhost_resolver_(authenticated_user_, vhost_name);
        if (!virtual_host_) {
            return fail("virtual host not allowed", 403);
        }
    } else if (vhost_name != config_.virtual_host) {
        return fail("unknown virtual host", 402);
    }

    const std::string open_ok = encodeMethodHeader(
        kConnectionClassId,
        static_cast<uint16_t>(ConnectionMethodId::OpenOk),
        encodeConnectionOpenOk());
    sendFrame(kFrameMethod, 0, open_ok);
    state_ = ConnectionState::kReady;
    return SessionResult{};
}

SessionResult ConnectionSession::handleConnectionClose(
    std::string_view arguments) {
    std::string error;
    ConnectionClose close;
    if (!decodeConnectionClose(arguments, close, error)) {
        return fail(error, 502);
    }

    const std::string close_ok = encodeMethodHeader(
        kConnectionClassId,
        static_cast<uint16_t>(ConnectionMethodId::CloseOk), "");
    sendFrame(kFrameMethod, 0, close_ok);
    state_ = ConnectionState::kClosed;
    return SessionResult{};
}

SessionResult ConnectionSession::handleConnectionCloseOk(
    std::string_view) {
    if (state_ != ConnectionState::kClosing) {
        return fail("unexpected connection.close-ok", 503);
    }

    state_ = ConnectionState::kClosed;
    return SessionResult{};
}

}  // namespace mq::amqp091
