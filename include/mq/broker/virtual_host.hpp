#ifndef MQ_BROKER_VIRTUAL_HOST_HPP
#define MQ_BROKER_VIRTUAL_HOST_HPP

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace mq::broker {

struct BrokerResult {
    bool ok = true;
    uint16_t reply_code = 0;
    std::string error;
    uint32_t count = 0;
};

struct ExchangeSpec {
    std::string name;
    std::string type;
    bool durable = false;
    bool auto_delete = false;
    bool internal = false;
};

struct QueueSpec {
    std::string name;
    bool durable = false;
    bool exclusive = false;
    bool auto_delete = false;
};

// In-memory virtual host used by the AMQP method layer until persistence is
// introduced. It deliberately tracks multiple binding keys per (exchange,
// queue) pair, which the legacy binding table could not represent.
class VirtualHost {
public:
    static constexpr uint16_t kNotFound = 404;
    static constexpr uint16_t kPreconditionFailed = 406;

    BrokerResult declareExchange(const ExchangeSpec& spec);
    BrokerResult deleteExchange(const std::string& name, bool if_unused);
    bool hasExchange(const std::string& name) const;
    size_t exchangeCount() const { return exchanges_.size(); }

    BrokerResult declareQueue(const QueueSpec& spec);
    BrokerResult deleteQueue(const std::string& name, bool if_unused,
                             bool if_empty);
    BrokerResult purgeQueue(const std::string& name);
    bool hasQueue(const std::string& name) const;
    size_t queueCount() const { return queues_.size(); }

    BrokerResult bind(const std::string& exchange, const std::string& queue,
                      const std::string& routing_key);
    BrokerResult unbind(const std::string& exchange, const std::string& queue,
                        const std::string& routing_key);
    size_t bindingCount() const;
    size_t bindingCount(const std::string& exchange,
                        const std::string& queue) const;

private:
    struct QueueEntry {
        QueueSpec spec;
        std::set<std::pair<std::string, std::string>> bindings;
    };

    struct ExchangeEntry {
        ExchangeSpec spec;
    };

    std::map<std::string, ExchangeEntry> exchanges_;
    std::map<std::string, QueueEntry> queues_;
};

}  // namespace mq::broker

#endif  // MQ_BROKER_VIRTUAL_HOST_HPP
