#include "mq/cli/options.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace mq::cli {

namespace {

// nullptr when the argument is not in the option table.
const OptionSpec* findSpec(const char* arg) {
    for (std::size_t i = 0; i < kOptionSpecCount; ++i) {
        const OptionSpec& spec = kOptionSpecs[i];
        if (spec.long_name != nullptr && std::strcmp(arg, spec.long_name) == 0) {
            return &spec;
        }

        if (spec.short_name != nullptr &&
            std::strcmp(arg, spec.short_name) == 0) {
            return &spec;
        }
    }

    return nullptr;
}

// strtoul rather than atoi: atoi cannot tell "0" from "abc" or overflow, it
// just silently yields 0.
bool parseUnsigned(const char* text, const char* option_name, uint32_t min_value,
                   uint32_t max_value, uint16_t* out, std::string* error) {
    if (text == nullptr || text[0] == '\0') {
        *error = std::string("option requires a numeric value: ") + option_name;
        return false;
    }

    // strtoul turns "-1" into a huge unsigned value, so reject signs up front.
    if (text[0] == '-' || text[0] == '+') {
        *error = std::string("invalid value for ") + option_name + ": \"" +
                 text + "\"";
        return false;
    }

    errno = 0;
    char* end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    const bool consumed_all = (end != text) && (end != nullptr) && (*end == '\0');
    if (!consumed_all || errno == ERANGE || value > max_value ||
        value < min_value) {
        *error = std::string("invalid value for ") + option_name + ": \"" +
                 text + "\"";
        return false;
    }

    *out = static_cast<uint16_t>(value);
    return true;
}

}  // namespace

ParseResult parseOptions(int argc, char** argv) {
    ParseResult result;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];

        // Positional arguments are unsupported; fail instead of ignoring them.
        if (arg[0] != '-') {
            result.ok = false;
            result.error = std::string("unexpected argument: ") + arg;
            return result;
        }

        const OptionSpec* spec = findSpec(arg);
        if (spec == nullptr) {
            result.ok = false;
            result.failed = OptionKind::kUnknown;
            result.error = std::string("unknown option: ") + arg;
            return result;
        }

        const char* value = nullptr;
        if (spec->has_value) {
            if (i + 1 >= argc) {
                result.ok = false;
                result.failed = OptionKind::kMissingValue;
                result.error =
                    std::string("option requires a value: ") + arg;
                return result;
            }

            value = argv[++i];
        }

        if (result.count >= kMaxParsedOptions) {
            result.ok = false;
            result.error = "too many options";
            return result;
        }

        result.items[result.count++] = ParsedOption{spec->kind, value};
        if (spec->kind == OptionKind::kHelp) {
            result.help_requested = true;
        }
    }

    return result;
}

bool applyOptions(const ParseResult& parsed, ServerOptions* out,
                   std::string* error) {
    if (out == nullptr || error == nullptr) return false;

    // Commit only once every option parsed, so failures leave no partial config.
    ServerOptions next;
    for (std::size_t i = 0; i < parsed.count; ++i) {
        const ParsedOption& item = parsed.items[i];
        switch (item.kind) {
            case OptionKind::kPort:
                if (!parseUnsigned(item.value, "--port", kMinPort, kMaxPort,
                                   &next.port, error)) {
                    return false;
                }

                break;
            case OptionKind::kHeartbeat:
                if (!parseUnsigned(item.value, "--heartbeat", kMinHeartbeat,
                                   kMaxPort, &next.heartbeat, error)) {
                    return false;
                }

                break;
            case OptionKind::kHttpPort:
                if (!parseUnsigned(item.value, "--http-port", kMinHeartbeat,
                                   kMaxPort, &next.http_port, error)) {
                    return false;
                }

                break;
            case OptionKind::kData:
                next.data_dir = (item.value != nullptr) ? item.value : "";
                break;
            case OptionKind::kHelp:
                break;  // --help only sets help_requested
            case OptionKind::kUnknown:
            case OptionKind::kMissingValue:
            default:
                *error = "internal error: unresolved option";
                return false;
        }
    }

    *out = std::move(next);
    return true;
}

void handleHelp(const char* program, const std::string& error) {
    const char* name =
        (program != nullptr && program[0] != '\0') ? program : "amqp_server";
    const bool is_error = !error.empty();
    std::FILE* out = is_error ? stderr : stdout;

    if (is_error) {
        std::fprintf(stderr, "%s: %s\n", name, error.c_str());
    }

    std::fprintf(out, "usage: %s [options]\n\n", name);
    for (std::size_t i = 0; i < kOptionSpecCount; ++i) {
        const OptionSpec& spec = kOptionSpecs[i];
        char left[96] = {0};
        if (spec.short_name != nullptr) {
            std::snprintf(left, sizeof(left), "%s, %s%s", spec.short_name,
                          spec.long_name, spec.has_value ? " <VALUE>" : "");
        } else {
            std::snprintf(left, sizeof(left), "    %s%s", spec.long_name,
                          spec.has_value ? " <VALUE>" : "");
        }

        std::fprintf(out, "  %-28s %s\n", left, spec.help);
    }

    std::fprintf(out,
                 "\ndefaults: port=%u heartbeat=%u http-port=%u data=\"%s\"\n",
                 static_cast<unsigned>(kDefaultPort),
                 static_cast<unsigned>(kDefaultHeartbeat),
                 static_cast<unsigned>(kDefaultHttpPort), kDefaultDataDir);
    std::fprintf(out,
                 "env     : MQ_AMQP_USER / MQ_AMQP_PASSWORD / MQ_AMQP_VHOST / "
                 "MQ_USERS\n");
}

}  // namespace mq::cli
