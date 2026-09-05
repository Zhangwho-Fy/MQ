#include "mq/broker/virtual_host.hpp"

namespace mq::broker {

namespace {

bool validExchangeType(const std::string& type) {
    return type == "direct" || type == "fanout" || type == "topic" ||
           type == "headers";
}

}  // namespace

BrokerResult VirtualHost::declareExchange(const ExchangeSpec& spec) {
    if (!validExchangeType(spec.type)) {
        return BrokerResult{false, kPreconditionFailed,
                            "unsupported exchange type", 0};
    }
    const auto it = exchanges_.find(spec.name);
    if (it != exchanges_.end()) {
        const ExchangeSpec& existing = it->second.spec;
        if (existing.type != spec.type || existing.durable != spec.durable ||
            existing.auto_delete != spec.auto_delete ||
            existing.internal != spec.internal) {
            return BrokerResult{false, kPreconditionFailed,
                                "inequivalent exchange declaration", 0};
        }
        return BrokerResult{};
    }
    exchanges_[spec.name] = ExchangeEntry{spec};
    return BrokerResult{};
}

BrokerResult VirtualHost::deleteExchange(const std::string& name,
                                         bool if_unused) {
    const auto it = exchanges_.find(name);
    if (it == exchanges_.end()) {
        return BrokerResult{false, kNotFound, "exchange not found", 0};
    }
    if (if_unused) {
        for (auto& queue_entry : queues_) {
            for (const auto& binding : queue_entry.second.bindings) {
                if (binding.first == name) {
                    return BrokerResult{false, kPreconditionFailed,
                                        "exchange is in use", 0};
                }
            }
        }
    }
    exchanges_.erase(it);
    for (auto& queue_entry : queues_) {
        auto& bindings = queue_entry.second.bindings;
        for (auto bit = bindings.begin(); bit != bindings.end();) {
            if (bit->first == name) {
                bit = bindings.erase(bit);
            } else {
                ++bit;
            }
        }
    }
    return BrokerResult{};
}

bool VirtualHost::hasExchange(const std::string& name) const {
    return exchanges_.find(name) != exchanges_.end();
}

BrokerResult VirtualHost::declareQueue(const QueueSpec& spec) {
    const auto it = queues_.find(spec.name);
    if (it != queues_.end()) {
        const QueueSpec& existing = it->second.spec;
        if (existing.durable != spec.durable ||
            existing.exclusive != spec.exclusive ||
            existing.auto_delete != spec.auto_delete) {
            return BrokerResult{false, kPreconditionFailed,
                                "inequivalent queue declaration", 0};
        }
        return BrokerResult{};
    }

    QueueEntry entry;
    entry.spec = spec;
    // The default exchange is the empty-name direct exchange.
    entry.bindings.insert({"", spec.name});
    queues_[spec.name] = std::move(entry);
    return BrokerResult{};
}

BrokerResult VirtualHost::deleteQueue(const std::string& name, bool if_unused,
                                      bool if_empty) {
    const auto it = queues_.find(name);
    if (it == queues_.end()) {
        return BrokerResult{false, kNotFound, "queue not found", 0};
    }
    // Message/consumer counts do not exist yet, so if_empty/if_unused are
    // always satisfied until the Basic class is implemented.
    (void)if_empty;
    (void)if_unused;
    queues_.erase(it);
    return BrokerResult{};
}

bool VirtualHost::hasQueue(const std::string& name) const {
    return queues_.find(name) != queues_.end();
}

BrokerResult VirtualHost::bind(const std::string& exchange,
                               const std::string& queue,
                               const std::string& routing_key) {
    if (!hasExchange(exchange)) {
        return BrokerResult{false, kNotFound, "exchange not found", 0};
    }
    const auto it = queues_.find(queue);
    if (it == queues_.end()) {
        return BrokerResult{false, kNotFound, "queue not found", 0};
    }
    it->second.bindings.insert({exchange, routing_key});
    return BrokerResult{};
}

BrokerResult VirtualHost::unbind(const std::string& exchange,
                                 const std::string& queue,
                                 const std::string& routing_key) {
    const auto it = queues_.find(queue);
    if (it == queues_.end()) {
        return BrokerResult{false, kNotFound, "queue not found", 0};
    }
    const auto binding = it->second.bindings.find({exchange, routing_key});
    if (binding == it->second.bindings.end()) {
        return BrokerResult{false, kNotFound, "binding not found", 0};
    }
    it->second.bindings.erase(binding);
    return BrokerResult{};
}

size_t VirtualHost::bindingCount() const {
    size_t count = 0;
    for (const auto& queue_entry : queues_) {
        count += queue_entry.second.bindings.size();
    }
    return count;
}

size_t VirtualHost::bindingCount(const std::string& exchange,
                                 const std::string& queue) const {
    const auto it = queues_.find(queue);
    if (it == queues_.end()) return 0;
    size_t count = 0;
    for (const auto& binding : it->second.bindings) {
        if (binding.first == exchange) ++count;
    }
    return count;
}

}  // namespace mq::broker
