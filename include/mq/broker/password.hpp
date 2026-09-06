#ifndef MQ_BROKER_PASSWORD_HPP
#define MQ_BROKER_PASSWORD_HPP

#include <string>

namespace mq::broker {

// Salted PBKDF2-HMAC-SHA256 password hashing for the demo user store.
class PasswordHasher {
public:
    static std::string generateSalt();
    static std::string hashPassword(const std::string& password,
                                    const std::string& salt_hex);
    static bool verify(const std::string& password,
                       const std::string& salt_hex,
                       const std::string& expected_hex);
};

}  // namespace mq::broker

#endif  // MQ_BROKER_PASSWORD_HPP
