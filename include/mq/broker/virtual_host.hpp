#ifndef MQ_BROKER_VIRTUAL_HOST_HPP
#define MQ_BROKER_VIRTUAL_HOST_HPP

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

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
    std::string dead_letter_exchange;
    std::string dead_letter_routing_key;
};

struct Message {
    std::string body;
    bool persistent = false;
    bool redelivered = false;
    std::string exchange;
    std::string routing_key;
    uint64_t id = 0;
    uint32_t dead_letter_count = 0;
};

using ConsumerDeliver =
    std::function<void(const std::string& consumer_tag,
                       const std::string& queue, const Message& message)>;

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
    uint32_t messageCount(const std::string& name) const;
    bool hasQueue(const std::string& name) const;
    size_t queueCount() const { return queues_.size(); }

    BrokerResult registerConsumer(const std::string& queue,
                                  const std::string& consumer_tag,
                                  void* owner,
                                  ConsumerDeliver deliver,
                                  bool no_ack = false);
    void unregisterConsumers(void* owner);
    void unregisterConsumer(const std::string& queue,
                            const std::string& consumer_tag, void* owner);
    size_t consumerCount(const std::string& queue) const;
    BrokerResult ackMessage(uint64_t message_id);
    BrokerResult rejectMessage(uint64_t message_id, bool requeue);
    void requeueUnacked(void* owner);
    size_t unackedCount() const { return unacked_.size(); }
    std::string deadLetterExchange(const std::string& queue) const;

    BrokerResult publish(const std::string& exchange,
                         const std::string& routing_key,
                         const Message& message,
                         size_t* delivered = nullptr);

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
        std::deque<Message> messages;
        std::set<std::pair<std::string, std::string>> bindings;
    };

    struct ConsumerEntry {
        std::string consumer_tag;
        void* owner = nullptr;
        bool no_ack = false;
        ConsumerDeliver deliver;
    };

    struct UnackedEntry {
        std::string queue;
        Message message;
        void* owner = nullptr;
    };

    void deliverPending(const std::string& queue);
    void deadLetter(const std::string& source_queue, const Message& message);

    struct ExchangeEntry {
        ExchangeSpec spec;
    };

    std::map<std::string, ExchangeEntry> exchanges_;
    std::map<std::string, QueueEntry> queues_;
    std::map<std::string, std::vector<ConsumerEntry>> consumers_;
    std::map<std::string, size_t> consumer_round_robin_;
    std::map<uint64_t, UnackedEntry> unacked_;
    uint64_t next_message_id_ = 1;
};

}  // namespace mq::broker

#endif  // MQ_BROKER_VIRTUAL_HOST_HPP
