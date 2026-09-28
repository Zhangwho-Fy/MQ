#ifndef MQ_CLI_OPTIONS_HPP
#define MQ_CLI_OPTIONS_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace mq::cli {

// Command-line options implemented so far. Adding one means a new enumerator
// plus one line in kOptionSpecs.
enum class OptionKind {
    kPort = 0,
    kHeartbeat,
    kData,
    kHttpPort,
    kHelp,
    kUnknown,       // present in argv, but not in the option table
    kMissingValue,  // takes a value, but none followed
};

constexpr uint16_t kDefaultPort = 5672;
constexpr uint16_t kDefaultHeartbeat = 60;   // 0 disables heartbeats
constexpr uint16_t kDefaultHttpPort = 0;     // 0 disables the management API
constexpr const char* kDefaultDataDir = "";  // empty keeps everything in memory

constexpr uint16_t kMinPort = 1;
constexpr uint16_t kMaxPort = 65535;
constexpr uint16_t kMinHeartbeat = 0;

// Cap on accepted options (repeats included) so a long argv cannot overflow
// the fixed-size array in ParseResult.
constexpr std::size_t kMaxParsedOptions = 32;

struct OptionSpec {
    const char* short_name;  // "-p", or nullptr without a short form
    const char* long_name;   // "--port"
    OptionKind kind;
    bool has_value;
    const char* help;
};

// The single place to add an option. inline (C++17) keeps this header-only.
inline constexpr OptionSpec kOptionSpecs[] = {
    {"-p", "--port", OptionKind::kPort, true, "AMQP listen port"},
    {nullptr, "--heartbeat", OptionKind::kHeartbeat, true,
     "heartbeat interval in seconds, 0 disables"},
    {nullptr, "--data", OptionKind::kData, true,
     "data directory, empty means in-memory"},
    {nullptr, "--http-port", OptionKind::kHttpPort, true,
     "management HTTP port, 0 disables"},
    {"-h", "--help", OptionKind::kHelp, false, "show this help and exit"},
};

constexpr std::size_t kOptionSpecCount =
    sizeof(kOptionSpecs) / sizeof(kOptionSpecs[0]);

struct ParsedOption {
    OptionKind kind = OptionKind::kUnknown;
    const char* value = nullptr;  // nullptr for flag options
};

struct ParseResult {
    std::array<ParsedOption, kMaxParsedOptions> items{};
    std::size_t count = 0;
    bool ok = true;
    bool help_requested = false;
    OptionKind failed = OptionKind::kUnknown;
    std::string error;  // includes the offending argument
};

struct ServerOptions {
    uint16_t port = kDefaultPort;
    uint16_t heartbeat = kDefaultHeartbeat;
    uint16_t http_port = kDefaultHttpPort;
    std::string data_dir = kDefaultDataDir;
};

// Structural validation only: unknown options, missing values, too many
// options, positional arguments. No number parsing, no printing, no exit.
ParseResult parseOptions(int argc, char** argv);

// Number parsing and range checks happen here. out is left untouched on
// failure, and error explains why.
bool applyOptions(const ParseResult& parsed, ServerOptions* out,
                   std::string* error);

// Writes usage to stdout, or to stderr after the error when error is not empty.
void handleHelp(const char* program, const std::string& error = {});

}  // namespace mq::cli

#endif  // MQ_CLI_OPTIONS_HPP
