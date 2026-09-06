#include "mq/broker/broker.hpp"

#include "mq/broker/password.hpp"

#include <cctype>
#include <utility>

namespace mq::broker {

Broker::Broker(std::string data_dir) : data_dir_(std::move(data_dir)) {
    const std::string salt = PasswordHasher::generateSalt();
    User guest;
    guest.salt = salt;
    guest.password_hash = PasswordHasher::hashPassword("guest", salt);
    guest.vhosts.insert("/");
    users_["guest"] = std::move(guest);
    vhosts_["/"] = std::make_shared<VirtualHost>(vhostDataDir("/"));
}

bool Broker::authenticate(const std::string& username,
                          const std::string& password) const {
    const auto it = users_.find(username);
    if (it == users_.end()) return false;
    return PasswordHasher::verify(password, it->second.salt,
                                  it->second.password_hash);
}

std::shared_ptr<VirtualHost> Broker::resolveVhost(
    const std::string& username, const std::string& vhost_name) const {
    const auto user = users_.find(username);
    if (user == users_.end()) return {};
    if (user->second.vhosts.find(vhost_name) ==
        user->second.vhosts.end()) {
        return {};
    }
    return vhost(vhost_name);
}

std::shared_ptr<VirtualHost> Broker::vhost(
    const std::string& name) const {
    const auto it = vhosts_.find(name);
    return it == vhosts_.end() ? nullptr : it->second;
}

bool Broker::addUser(const std::string& username,
                     const std::string& password,
                     const std::set<std::string>& allowed_vhosts) {
    for (const std::string& vhost_name : allowed_vhosts) {
        if (vhosts_.find(vhost_name) == vhosts_.end()) {
            vhosts_[vhost_name] =
                std::make_shared<VirtualHost>(vhostDataDir(vhost_name));
        }
    }
    const std::string salt = PasswordHasher::generateSalt();
    User user;
    user.salt = salt;
    user.password_hash = PasswordHasher::hashPassword(password, salt);
    user.vhosts = allowed_vhosts;
    users_[username] = std::move(user);
    return true;
}

std::string Broker::vhostDataDir(const std::string& name) const {
    if (data_dir_.empty()) return {};
    std::string dir = name;
    for (char& ch : dir) {
        if (!std::isalnum(static_cast<unsigned char>(ch))) ch = '_';
    }
    return data_dir_ + "/" + dir;
}

}  // namespace mq::broker
