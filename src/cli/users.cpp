#include "mq/cli/users.hpp"

#include <cstddef>
#include <cstdlib>
#include <set>

namespace mq::cli {

namespace {

std::string trim(const std::string& text) {
    const char* kWhitespace = " \t\r\n";
    const std::size_t first = text.find_first_not_of(kWhitespace);
    if (first == std::string::npos) return {};
    const std::size_t last = text.find_last_not_of(kWhitespace);
    return text.substr(first, last - first + 1);
}

void reject(UsersParseResult* result, const std::string& reason) {
    ++result->rejected;
    result->errors.push_back(reason);
}

}  // namespace

UsersParseResult parseUsersSpec(const std::string& spec, const AddUserFn& add) {
    UsersParseResult result;
    std::set<std::string> seen;
    std::size_t start = 0;

    while (start < spec.size()) {
        const std::size_t sep = spec.find(';', start);
        const std::size_t entry_end =
            sep == std::string::npos ? spec.size() : sep;
        const std::string entry = trim(spec.substr(start, entry_end - start));
        start = entry_end + 1;

        if (entry.empty()) continue;

        const std::size_t first_colon = entry.find(':');
        const std::size_t second_colon =
            first_colon == std::string::npos
                ? std::string::npos
                : entry.find(':', first_colon + 1);

        if (first_colon == std::string::npos ||
            second_colon == std::string::npos) {
            reject(&result, "MQ_USERS entry needs user:password:vhost: \"" +
                                entry + "\"");
            continue;
        }

        UserEntry user;
        user.name = trim(entry.substr(0, first_colon));
        user.password =
            trim(entry.substr(first_colon + 1, second_colon - first_colon - 1));
        user.vhost = trim(entry.substr(second_colon + 1));

        if (user.name.empty()) {
            reject(&result,
                   "MQ_USERS entry has an empty user name: \"" + entry + "\"");
            continue;
        }

        if (user.vhost.empty()) {
            reject(&result,
                   "MQ_USERS entry has an empty vhost (use \"/\" for the "
                   "default vhost): \"" +
                       entry + "\"");
            continue;
        }

        if (!seen.insert(user.name).second) {
            reject(&result, "user \"" + user.name + "\" is configured twice");
            continue;
        }

        if (user.password.empty()) {
            result.errors.push_back("user \"" + user.name +
                                    "\" has an empty password (allowed, but "
                                    "check it)");
        }

        if (add && !add(user)) {
            reject(&result,
                   "user \"" + user.name + "\" was rejected by the caller");
            continue;
        }

        ++result.accepted;
    }

    return result;
}

UsersParseResult loadUsersFromEnv(const AddUserFn& add) {
    const char* spec = std::getenv("MQ_USERS");
    if (spec == nullptr) return UsersParseResult{};
    return parseUsersSpec(spec, add);
}

}  // namespace mq::cli
