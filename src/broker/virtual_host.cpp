#include "mq/broker/virtual_host.hpp"

#include <vector>

namespace mq::broker {

namespace {

bool validExchangeType(const std::string& type) {
    return type == "direct" || type == "fanout" || type == "topic" ||
           type == "headers";
}

bool topicMatches(const std::string& binding_key,
                  const std::string& routing_key) {
    std::vector<std::string> bkeys;
    std::vector<std::string> rkeys;
    size_t pos = 0;
    while (pos <= binding_key.size()) {
        const size_t dot = binding_key.find('.', pos);
        if (dot == std::string::npos) {
            bkeys.push_back(binding_key.substr(pos));
            break;
        }
        bkeys.push_back(binding_key.substr(pos, dot - pos));
        pos = dot + 1;
    }
    pos = 0;
    while (pos <= routing_key.size()) {
        const size_t dot = routing_key.find('.', pos);
        if (dot == std::string::npos) {
            rkeys.push_back(routing_key.substr(pos));
            break;
        }
        rkeys.push_back(routing_key.substr(pos, dot - pos));
        pos = dot + 1;
    }

    std::vector<bool> dp(bkeys.size() + 1, false);
    dp[0] = true;
    if (!bkeys.empty() && bkeys[0] == "#") dp[1] = true;
    for (size_t i = 0; i < rkeys.size(); ++i) {
        if (i > 0) dp[0] = false;
        bool previous = dp[0];
        for (size_t j = 0; j < bkeys.size(); ++j) {
            const bool current = dp[j + 1];
            if (rkeys[i] == bkeys[j] || bkeys[j] == "*") {
                dp[j + 1] = previous;
            } else if (bkeys[j] == "#") {
                dp[j + 1] = dp[j] || dp[j + 1];
            } else {
                dp[j + 1] = false;
            }
            previous = current;
        }
    }
    return bkeys.empty() ? false : dp[bkeys.size()];
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
    if (if_empty && !it->second.messages.empty()) {
        return BrokerResult{false, kPreconditionFailed,
                            "queue is not empty", 0};
    }
    if (if_unused) {
        // Consumer tracking is introduced with Basic.Consume.
    }
    const uint32_t removed =
        static_cast<uint32_t>(it->second.messages.size());
    queues_.erase(it);
    return BrokerResult{true, 0, "", removed};
}

bool VirtualHost::hasQueue(const std::string& name) const {
    return queues_.find(name) != queues_.end();
}

uint32_t VirtualHost::messageCount(const std::string& name) const {
    const auto it = queues_.find(name);
    if (it == queues_.end()) return 0;
    return static_cast<uint32_t>(it->second.messages.size());
}

BrokerResult VirtualHost::purgeQueue(const std::string& name) {
    const auto it = queues_.find(name);
    if (it == queues_.end()) {
        return BrokerResult{false, kNotFound, "queue not found", 0};
    }
    const uint32_t removed =
        static_cast<uint32_t>(it->second.messages.size());
    it->second.messages.clear();
    return BrokerResult{true, 0, "", removed};
}

BrokerResult VirtualHost::publish(const std::string& exchange,
                                  const std::string& routing_key,
                                  const Message& message,
                                  size_t* delivered) {
    size_t count = 0;

    const auto routeToQueue = [&](const std::string& queue) {
        const auto it = queues_.find(queue);
        if (it == queues_.end()) return;
        it->second.messages.push_back(message);
        ++count;
    };

    if (exchange.empty()) {
        // Default exchange: queue name equals routing key.
        if (hasQueue(routing_key)) {
            routeToQueue(routing_key);
        }
        if (delivered != nullptr) *delivered = count;
        return BrokerResult{};
    }

    const auto exchange_it = exchanges_.find(exchange);
    if (exchange_it == exchanges_.end()) {
        return BrokerResult{false, kNotFound, "exchange not found", 0};
    }

    const std::string& type = exchange_it->second.spec.type;
    for (auto& queue_entry : queues_) {
        bool matched = false;
        for (const auto& binding : queue_entry.second.bindings) {
            if (binding.first != exchange) continue;
            if (type == "fanout") {
                matched = true;
            } else if (type == "direct") {
                matched = binding.second == routing_key;
            } else if (type == "topic") {
                matched = topicMatches(binding.second, routing_key);
            }
            if (matched) break;
        }
        if (matched) routeToQueue(queue_entry.first);
    }

    if (delivered != nullptr) *delivered = count;
    return BrokerResult{};
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
