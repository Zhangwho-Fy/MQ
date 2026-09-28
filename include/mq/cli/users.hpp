#ifndef MQ_CLI_USERS_HPP
#define MQ_CLI_USERS_HPP

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace mq::cli {

// One MQ_USERS entry: user name, password and the vhost it may use.
struct UserEntry {
    std::string name;
    std::string password;
    std::string vhost;
};

// The caller decides how a user is created (AmqpServer::addUser). Returning
// false rejects that single entry and leaves the others alone.
using AddUserFn = std::function<bool(const UserEntry&)>;

struct UsersParseResult {
    std::size_t accepted = 0;
    std::size_t rejected = 0;
    std::vector<std::string> errors;  // one line per rejected or suspicious entry
};

// Parses 'user:pass:vhost;user2:pass2:vhost2':
//   - ';' separates entries, empty entries (extra semicolons) are skipped
//   - each entry splits on its first two ':', fields are trimmed
//   - missing fields, empty user name or empty vhost -> rejected + errors
//   - empty password is accepted, but reported in errors
//   - a password therefore cannot contain ':'
UsersParseResult parseUsersSpec(const std::string& spec, const AddUserFn& add);

// Reads MQ_USERS and forwards it to parseUsersSpec. A missing or empty
// variable yields an all-zero result.
UsersParseResult loadUsersFromEnv(const AddUserFn& add);

}  // namespace mq::cli

#endif  // MQ_CLI_USERS_HPP
