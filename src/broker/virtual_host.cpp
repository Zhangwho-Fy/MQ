#include "mq/broker/virtual_host.hpp"

#include <algorithm>
#include <chrono>
#include <vector>

namespace mq::broker {

namespace {

bool validExchangeType(const std::string& type) {
    return type == "direct" || type == "fanout" || type == "topic" ||
           type == "headers";
}

uint64_t nowMs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
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
            existing.auto_delete != spec.auto_delete ||
            existing.dead_letter_exchange != spec.dead_letter_exchange ||
            existing.dead_letter_routing_key !=
                spec.dead_letter_routing_key ||
            existing.message_ttl_ms != spec.message_ttl_ms) {
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
    expireMessages(name);
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
        Message copy = message;
        copy.id = next_message_id_++;
        copy.redelivered = false;
        if (copy.expire_at_ms == 0 && it->second.spec.message_ttl_ms > 0) {
            copy.expire_at_ms =
                nowMs() + static_cast<uint64_t>(it->second.spec.message_ttl_ms);
        }
        it->second.messages.push_back(copy);
        ++count;
        deliverPending(queue);
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

BrokerResult VirtualHost::registerConsumer(
    const std::string& queue, const std::string& consumer_tag, void* owner,
    ConsumerDeliver deliver, bool no_ack) {
    const auto queue_it = queues_.find(queue);
    if (queue_it == queues_.end()) {
        return BrokerResult{false, kNotFound, "queue not found", 0};
    }
    auto& entries = consumers_[queue];
    for (const auto& entry : entries) {
        if (entry.owner == owner && entry.consumer_tag == consumer_tag) {
            return BrokerResult{};
        }
    }
    entries.push_back(
        ConsumerEntry{consumer_tag, owner, no_ack, std::move(deliver)});
    deliverPending(queue);
    return BrokerResult{};
}

void VirtualHost::unregisterConsumers(void* owner) {
    for (auto it = consumers_.begin(); it != consumers_.end();) {
        auto& entries = it->second;
        entries.erase(std::remove_if(entries.begin(), entries.end(),
                                     [owner](const ConsumerEntry& entry) {
                                         return entry.owner == owner;
                                     }),
                      entries.end());
        consumer_round_robin_.erase(it->first);
        if (entries.empty()) {
            it = consumers_.erase(it);
        } else {
            ++it;
        }
    }
}

void VirtualHost::unregisterConsumer(const std::string& queue,
                                     const std::string& consumer_tag,
                                     void* owner) {
    const auto it = consumers_.find(queue);
    if (it == consumers_.end()) return;
    auto& entries = it->second;
    entries.erase(
        std::remove_if(entries.begin(), entries.end(),
                       [&](const ConsumerEntry& entry) {
                           return entry.owner == owner &&
                                  entry.consumer_tag == consumer_tag;
                       }),
        entries.end());
    consumer_round_robin_.erase(queue);
    if (entries.empty()) consumers_.erase(it);
}

size_t VirtualHost::consumerCount(const std::string& queue) const {
    const auto it = consumers_.find(queue);
    return it == consumers_.end() ? 0 : it->second.size();
}

BrokerResult VirtualHost::ackMessage(uint64_t message_id) {
    const auto it = unacked_.find(message_id);
    if (it == unacked_.end()) {
        return BrokerResult{false, kPreconditionFailed,
                            "unknown delivery tag", 0};
    }
    unacked_.erase(it);
    return BrokerResult{};
}

BrokerResult VirtualHost::getMessage(const std::string& queue, bool no_ack,
                                     void* owner, Message* message,
                                     bool* has_message, uint32_t* remaining) {
    const auto queue_it = queues_.find(queue);
    if (queue_it == queues_.end()) {
        return BrokerResult{false, kNotFound, "queue not found", 0};
    }
    expireMessages(queue);
    if (has_message != nullptr) *has_message = false;
    if (remaining != nullptr) *remaining = 0;
    if (queue_it->second.messages.empty()) return BrokerResult{};

    *message = queue_it->second.messages.front();
    queue_it->second.messages.pop_front();
    if (has_message != nullptr) *has_message = true;
    if (remaining != nullptr) {
        *remaining =
            static_cast<uint32_t>(queue_it->second.messages.size());
    }
    if (!no_ack) {
        unacked_[message->id] =
            UnackedEntry{queue, *message, owner};
    }
    return BrokerResult{};
}

BrokerResult VirtualHost::rejectMessage(uint64_t message_id, bool requeue) {
    const auto it = unacked_.find(message_id);
    if (it == unacked_.end()) {
        return BrokerResult{false, kPreconditionFailed,
                            "unknown delivery tag", 0};
    }
    UnackedEntry entry = std::move(it->second);
    unacked_.erase(it);
    if (requeue) {
        entry.message.redelivered = true;
        queues_[entry.queue].messages.push_front(entry.message);
        deliverPending(entry.queue);
    } else {
        deadLetter(entry.queue, entry.message);
    }
    return BrokerResult{};
}

void VirtualHost::requeueUnacked(void* owner) {
    std::vector<UnackedEntry> entries;
    for (auto it = unacked_.begin(); it != unacked_.end();) {
        if (it->second.owner == owner) {
            entries.push_back(std::move(it->second));
            it = unacked_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto& entry : entries) {
        entry.message.redelivered = true;
        queues_[entry.queue].messages.push_front(entry.message);
    }
    for (const auto& entry : entries) {
        deliverPending(entry.queue);
    }
}

void VirtualHost::deadLetter(const std::string& source_queue,
                             const Message& message) {
    const auto queue_it = queues_.find(source_queue);
    if (queue_it == queues_.end()) return;
    const QueueSpec& spec = queue_it->second.spec;
    if (spec.dead_letter_exchange.empty()) return;

    Message copy = message;
    copy.routing_key = spec.dead_letter_routing_key.empty()
                           ? message.routing_key
                           : spec.dead_letter_routing_key;
    ++copy.dead_letter_count;
    copy.expire_at_ms = 0;
    publish(spec.dead_letter_exchange, copy.routing_key, copy, nullptr);
}

std::string VirtualHost::deadLetterExchange(
    const std::string& queue) const {
    const auto it = queues_.find(queue);
    return it == queues_.end() ? std::string{}
                               : it->second.spec.dead_letter_exchange;
}

int64_t VirtualHost::messageTtl(const std::string& queue) const {
    const auto it = queues_.find(queue);
    return it == queues_.end() ? 0 : it->second.spec.message_ttl_ms;
}

void VirtualHost::expireMessages(const std::string& queue) {
    const auto queue_it = queues_.find(queue);
    if (queue_it == queues_.end()) return;
    auto& messages = queue_it->second.messages;
    const uint64_t now = nowMs();
    while (!messages.empty()) {
        const Message& front = messages.front();
        if (front.expire_at_ms == 0 || front.expire_at_ms > now) break;
        Message expired = messages.front();
        messages.pop_front();
        deadLetter(queue, expired);
    }
}

void VirtualHost::deliverPending(const std::string& queue) {
    const auto queue_it = queues_.find(queue);
    if (queue_it == queues_.end()) return;
    expireMessages(queue);
    const auto consumer_it = consumers_.find(queue);
    if (consumer_it == consumers_.end() || consumer_it->second.empty()) return;

    const std::vector<ConsumerEntry> consumers = consumer_it->second;
    size_t& index = consumer_round_robin_[queue];
    while (!queue_it->second.messages.empty() && !consumers.empty()) {
        const ConsumerEntry& entry = consumers[index % consumers.size()];
        Message message = queue_it->second.messages.front();
        queue_it->second.messages.pop_front();
        ++index;
        if (!entry.no_ack) {
            unacked_[message.id] =
                UnackedEntry{queue, message, entry.owner};
        }
        if (entry.deliver) {
            entry.deliver(entry.consumer_tag, queue, message);
        }
    }
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
