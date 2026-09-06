#include "mq/broker/password.hpp"

#include <cstring>
#include <dlfcn.h>
#include <random>
#include <vector>

namespace mq::broker {

namespace {

constexpr int kIterations = 10000;
constexpr int kKeyLength = 32;

using Pbkdf2Fn = int (*)(const char*, int, const unsigned char*, int, int,
                         const void*, int, unsigned char*);
using EvpSha256Fn = const void* (*)();

bool pbkdf2(const char* password, const unsigned char* salt, int salt_len,
            unsigned char* out) {
    static void* crypto = dlopen("libcrypto.so.3", RTLD_LAZY);
    if (crypto == nullptr) return false;
    static auto sha256 = reinterpret_cast<EvpSha256Fn>(
        dlsym(crypto, "EVP_sha256"));
    static auto kdf = reinterpret_cast<Pbkdf2Fn>(
        dlsym(crypto, "PKCS5_PBKDF2_HMAC"));
    if (sha256 == nullptr || kdf == nullptr) return false;
    return kdf(password, static_cast<int>(std::strlen(password)), salt,
               salt_len, kIterations, sha256(), kKeyLength, out) == 1;
}

std::string toHex(const unsigned char* data, size_t size) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) {
        out.push_back(digits[(data[i] >> 4) & 0xf]);
        out.push_back(digits[data[i] & 0xf]);
    }
    return out;
}

bool fromHex(const std::string& hex, std::vector<unsigned char>& out) {
    auto value = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
    };
    if (hex.size() % 2 != 0) return false;
    out.resize(hex.size() / 2);
    for (size_t i = 0; i < out.size(); ++i) {
        const int hi = value(hex[i * 2]);
        const int lo = value(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<unsigned char>((hi << 4) | lo);
    }
    return true;
}

}  // namespace

std::string PasswordHasher::generateSalt() {
    std::random_device random;
    unsigned char salt[16]{};
    for (unsigned char& byte : salt) byte = static_cast<unsigned char>(random());
    return toHex(salt, sizeof(salt));
}

std::string PasswordHasher::hashPassword(const std::string& password,
                                         const std::string& salt_hex) {
    std::vector<unsigned char> salt;
    if (!fromHex(salt_hex, salt)) return {};
    unsigned char digest[kKeyLength]{};
    if (!pbkdf2(password.c_str(), salt.data(),
                static_cast<int>(salt.size()), digest)) {
        return {};
    }
    return toHex(digest, sizeof(digest));
}

bool PasswordHasher::verify(const std::string& password,
                            const std::string& salt_hex,
                            const std::string& expected_hex) {
    const std::string actual = hashPassword(password, salt_hex);
    if (actual.empty() || actual.size() != expected_hex.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < actual.size(); ++i) {
        diff |= static_cast<unsigned char>(actual[i] ^ expected_hex[i]);
    }
    return diff == 0;
}

}  // namespace mq::broker
