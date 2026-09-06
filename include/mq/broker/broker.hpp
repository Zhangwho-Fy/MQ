#ifndef MQ_BROKER_BROKER_HPP
#define MQ_BROKER_BROKER_HPP

#include "virtual_host.hpp"

#include <map>
#include <memory>
#include <set>
#include <string>

namespace mq::broker {

struct User {
    std::string password_hash;
    std::string salt;
    std::set<std::string> vhosts;
};

class Broker {
public:
    explicit Broker(std::string data_dir = {});

    bool authenticate(const std::string& username,
                      const std::string& password) const;
    std::shared_ptr<VirtualHost> resolveVhost(
        const std::string& username, const std::string& vhost_name) const;
    std::shared_ptr<VirtualHost> vhost(const std::string& name) const;

    bool addUser(const std::string& username, const std::string& password,
                 const std::set<std::string>& allowed_vhosts);

private:
    std::string vhostDataDir(const std::string& name) const;

    std::string data_dir_;
    std::map<std::string, std::shared_ptr<VirtualHost>> vhosts_;
    std::map<std::string, User> users_;
};

}  // namespace mq::broker

#endif  // MQ_BROKER_BROKER_HPP
