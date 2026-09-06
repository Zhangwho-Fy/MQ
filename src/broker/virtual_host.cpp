#include "mq/broker/virtual_host.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sys/stat.h>
#include <vector>

#include <sqlite3.h>

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

std::string sqlEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        if (ch == '\'') out += "''";
        else out += ch;
    }
    return out;
}

bool sqlExec(sqlite3* db, const std::string& sql) {
    char* error = nullptr;
    const int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error);
    if (rc != SQLITE_OK) {
        sqlite3_free(error);
        return false;
    }
    return true;
}

void appendU16(std::string& out, uint16_t value) {
    out.push_back(static_cast<char>((value >> 8) & 0xff));
    out.push_back(static_cast<char>(value & 0xff));
}

void appendU32(std::string& out, uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<char>((value >> shift) & 0xff));
    }
}

void appendU64(std::string& out, uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        out.push_back(static_cast<char>((value >> shift) & 0xff));
    }
}

bool appendFile(const std::string& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::app);
    if (!out.is_open()) return false;
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.flush();
    return out.good();
}

void ensureDirectory(const std::string& path) {
    if (path.empty()) return;
    size_t pos = 0;
    while ((pos = path.find('/', pos)) != std::string::npos) {
        const std::string sub = path.substr(0, pos);
        if (!sub.empty()) ::mkdir(sub.c_str(), 0755);
        ++pos;
    }
    if (!path.empty()) ::mkdir(path.c_str(), 0755);
}

std::string queueLogPath(const std::string& data_dir,
                         const std::string& queue) {
    return data_dir + "/queues/" + queue + ".log";
}

}  // namespace

VirtualHost::VirtualHost(std::string data_dir)
    : data_dir_(std::move(data_dir)) {
    openStorage();
}

VirtualHost::~VirtualHost() {
    closeStorage();
}

bool VirtualHost::openStorage() {
    if (data_dir_.empty()) return false;
    ensureDirectory(data_dir_);
    ensureDirectory(data_dir_ + "/queues");
    const std::string dbfile = data_dir_ + "/meta.db";
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(dbfile.c_str(), &db,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                        nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return false;
    }
    db_ = db;
    const bool ok =
        sqlExec(db, "create table if not exists exchanges("
                    "name text primary key, type text, durable int, "
                    "auto_delete int, internal int);") &&
        sqlExec(db, "create table if not exists queues("
                    "name text primary key, durable int, exclusive int, "
                    "auto_delete int, dead_letter_exchange text, "
                    "dead_letter_routing_key text, message_ttl_ms int);") &&
        sqlExec(db, "create table if not exists bindings("
                    "exchange text, queue text, routing_key text, "
                    "primary key(exchange, queue, routing_key));");
    if (!ok) {
        closeStorage();
        return false;
    }
    recoverStorage();
    return true;
}

void VirtualHost::closeStorage() {
    if (db_ != nullptr) {
        sqlite3_close(static_cast<sqlite3*>(db_));
        db_ = nullptr;
    }
}

void VirtualHost::recoverStorage() {
    sqlite3* db = static_cast<sqlite3*>(db_);
    if (db == nullptr) return;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(
            db, "select name,type,durable,auto_delete,internal "
                "from exchanges;",
            -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ExchangeSpec spec;
            spec.name = reinterpret_cast<const char*>(
                sqlite3_column_text(stmt, 0));
            spec.type = reinterpret_cast<const char*>(
                sqlite3_column_text(stmt, 1));
            spec.durable = sqlite3_column_int(stmt, 2) != 0;
            spec.auto_delete = sqlite3_column_int(stmt, 3) != 0;
            spec.internal = sqlite3_column_int(stmt, 4) != 0;
            ExchangeEntry entry;
            entry.spec = spec;
            exchanges_[spec.name] = std::move(entry);
        }
        sqlite3_finalize(stmt);
    }

    if (sqlite3_prepare_v2(
            db, "select name,durable,exclusive,auto_delete,"
                "dead_letter_exchange,dead_letter_routing_key,message_ttl_ms "
                "from queues;",
            -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            QueueSpec spec;
            spec.name = reinterpret_cast<const char*>(
                sqlite3_column_text(stmt, 0));
            spec.durable = sqlite3_column_int(stmt, 1) != 0;
            spec.exclusive = sqlite3_column_int(stmt, 2) != 0;
            spec.auto_delete = sqlite3_column_int(stmt, 3) != 0;
            const char* dlx = reinterpret_cast<const char*>(
                sqlite3_column_text(stmt, 4));
            const char* dlx_rk = reinterpret_cast<const char*>(
                sqlite3_column_text(stmt, 5));
            if (dlx != nullptr) spec.dead_letter_exchange = dlx;
            if (dlx_rk != nullptr) spec.dead_letter_routing_key = dlx_rk;
            spec.message_ttl_ms = sqlite3_column_int64(stmt, 6);
            QueueEntry entry;
            entry.spec = spec;
            entry.bindings.insert({"", spec.name});
            queues_[spec.name] = std::move(entry);
        }
        sqlite3_finalize(stmt);
    }

    if (sqlite3_prepare_v2(
            db, "select exchange,queue,routing_key from bindings;",
            -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const std::string exchange = reinterpret_cast<const char*>(
                sqlite3_column_text(stmt, 0));
            const std::string queue = reinterpret_cast<const char*>(
                sqlite3_column_text(stmt, 1));
            const std::string key = reinterpret_cast<const char*>(
                sqlite3_column_text(stmt, 2));
            const auto queue_it = queues_.find(queue);
            if (queue_it != queues_.end()) {
                queue_it->second.bindings.insert({exchange, key});
            }
        }
        sqlite3_finalize(stmt);
    }

    for (const auto& queue_entry : queues_) {
        if (queue_entry.second.spec.durable) {
            recoverQueueMessages(queue_entry.first);
        }
    }
}

void VirtualHost::appendMessageLog(const std::string& queue,
                                   const Message& message) {
    if (db_ == nullptr) return;
    const auto queue_it = queues_.find(queue);
    if (queue_it == queues_.end() ||
        !queue_it->second.spec.durable) {
        return;
    }
    std::string record;
    record.push_back(1);
    appendU64(record, message.id);
    appendU32(record, static_cast<uint32_t>(message.body.size()));
    record.append(message.body);
    appendU16(record, static_cast<uint16_t>(message.exchange.size()));
    record.append(message.exchange);
    appendU16(record, static_cast<uint16_t>(message.routing_key.size()));
    record.append(message.routing_key);
    appendU64(record, message.expire_at_ms);
    appendU32(record, message.ttl_ms);
    appendU32(record, message.dead_letter_count);
    appendU32(record, static_cast<uint32_t>(message.header_payload.size()));
    record.append(message.header_payload);
    appendFile(queueLogPath(data_dir_, queue), record);
}

void VirtualHost::appendTombstoneLog(const std::string& queue,
                                     uint64_t message_id) {
    if (db_ == nullptr) return;
    std::string record;
    record.push_back(2);
    appendU64(record, message_id);
    appendFile(queueLogPath(data_dir_, queue), record);
}

void VirtualHost::removeQueueLog(const std::string& queue) {
    if (data_dir_.empty()) return;
    std::remove(queueLogPath(data_dir_, queue).c_str());
}

void VirtualHost::compactQueueLog(const std::string& queue) {
    const auto queue_it = queues_.find(queue);
    if (queue_it == queues_.end() || !queue_it->second.spec.durable) return;
    removeQueueLog(queue);
    for (const Message& message : queue_it->second.messages) {
        if (message.persistent) appendMessageLog(queue, message);
    }
    for (const auto& entry : unacked_) {
        if (entry.second.queue == queue && entry.second.message.persistent) {
            appendMessageLog(queue, entry.second.message);
        }
    }
}

void VirtualHost::recoverQueueMessages(const std::string& queue) {
    if (data_dir_.empty()) return;
    std::ifstream in(queueLogPath(data_dir_, queue), std::ios::binary);
    if (!in.is_open()) return;
    std::string data((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    size_t pos = 0;
    std::vector<uint64_t> order;
    std::map<uint64_t, Message> by_id;
    while (pos + 1 <= data.size()) {
        const uint8_t type = static_cast<uint8_t>(data[pos++]);
        if (type == 1) {
            if (pos + 8 > data.size()) break;
            uint64_t id = 0;
            for (int i = 0; i < 8; ++i) {
                id = (id << 8) | static_cast<uint8_t>(data[pos++]);
            }
            if (pos + 4 > data.size()) break;
            uint32_t body_len = 0;
            for (int i = 0; i < 4; ++i) {
                body_len =
                    (body_len << 8) | static_cast<uint8_t>(data[pos++]);
            }
            if (pos + body_len > data.size()) break;
            Message message;
            message.id = id;
            message.body.assign(data.data() + pos, body_len);
            pos += body_len;

            auto read_string = [&](std::string& out) {
                if (pos + 2 > data.size()) return false;
                const uint16_t len =
                    (static_cast<uint16_t>(
                         static_cast<uint8_t>(data[pos]))
                     << 8) |
                    static_cast<uint16_t>(
                        static_cast<uint8_t>(data[pos + 1]));
                pos += 2;
                if (pos + len > data.size()) return false;
                out.assign(data.data() + pos, len);
                pos += len;
                return true;
            };
            if (!read_string(message.exchange) ||
                !read_string(message.routing_key)) {
                break;
            }
            if (pos + 8 + 4 + 4 > data.size()) break;
            message.expire_at_ms = 0;
            for (int i = 0; i < 8; ++i) {
                message.expire_at_ms =
                    (message.expire_at_ms << 8) |
                    static_cast<uint8_t>(data[pos++]);
            }
            uint32_t ttl = 0;
            uint32_t dlc = 0;
            for (int i = 0; i < 4; ++i) {
                ttl = (ttl << 8) | static_cast<uint8_t>(data[pos++]);
            }
            for (int i = 0; i < 4; ++i) {
                dlc = (dlc << 8) | static_cast<uint8_t>(data[pos++]);
            }
            message.ttl_ms = ttl;
            message.dead_letter_count = dlc;
            if (pos + 4 > data.size()) break;
            uint32_t header_len = 0;
            for (int i = 0; i < 4; ++i) {
                header_len =
                    (header_len << 8) | static_cast<uint8_t>(data[pos++]);
            }
            if (pos + header_len > data.size()) break;
            message.header_payload.assign(data.data() + pos, header_len);
            pos += header_len;
            message.persistent = true;
            order.push_back(id);
            by_id[id] = std::move(message);
            if (id >= next_message_id_) next_message_id_ = id + 1;
        } else if (type == 2) {
            if (pos + 8 > data.size()) break;
            uint64_t id = 0;
            for (int i = 0; i < 8; ++i) {
                id = (id << 8) | static_cast<uint8_t>(data[pos++]);
            }
            by_id.erase(id);
        } else {
            break;
        }
    }
    auto& messages = queues_[queue].messages;
    for (const uint64_t id : order) {
        const auto it = by_id.find(id);
        if (it != by_id.end()) {
            messages.push_back(it->second);
        }
    }
}

void VirtualHost::persistExchange(const ExchangeSpec& spec) {
    if (db_ == nullptr) return;
    const std::string sql =
        "insert or replace into exchanges values('" + sqlEscape(spec.name) +
        "','" + sqlEscape(spec.type) + "'," +
        std::to_string(spec.durable ? 1 : 0) + "," +
        std::to_string(spec.auto_delete ? 1 : 0) + "," +
        std::to_string(spec.internal ? 1 : 0) + ");";
    sqlExec(static_cast<sqlite3*>(db_), sql);
}

void VirtualHost::removeExchangeRow(const std::string& name) {
    if (db_ == nullptr) return;
    sqlExec(static_cast<sqlite3*>(db_),
            "delete from exchanges where name='" + sqlEscape(name) + "';");
}

void VirtualHost::persistQueue(const QueueSpec& spec) {
    if (db_ == nullptr) return;
    const std::string sql =
        "insert or replace into queues values('" + sqlEscape(spec.name) +
        "'," + std::to_string(spec.durable ? 1 : 0) + "," +
        std::to_string(spec.exclusive ? 1 : 0) + "," +
        std::to_string(spec.auto_delete ? 1 : 0) + ",'" +
        sqlEscape(spec.dead_letter_exchange) + "','" +
        sqlEscape(spec.dead_letter_routing_key) + "'," +
        std::to_string(spec.message_ttl_ms) + ");";
    sqlExec(static_cast<sqlite3*>(db_), sql);
}

void VirtualHost::removeQueueRow(const std::string& name) {
    if (db_ == nullptr) return;
    sqlExec(static_cast<sqlite3*>(db_),
            "delete from queues where name='" + sqlEscape(name) + "';");
}

void VirtualHost::persistBinding(const std::string& exchange,
                                 const std::string& queue,
                                 const std::string& routing_key) {
    if (db_ == nullptr) return;
    const std::string sql =
        "insert or ignore into bindings values('" + sqlEscape(exchange) +
        "','" + sqlEscape(queue) + "','" + sqlEscape(routing_key) + "');";
    sqlExec(static_cast<sqlite3*>(db_), sql);
}

void VirtualHost::removeBindingRow(const std::string& exchange,
                                   const std::string& queue,
                                   const std::string& routing_key) {
    if (db_ == nullptr) return;
    const std::string sql =
        "delete from bindings where exchange='" + sqlEscape(exchange) +
        "' and queue='" + sqlEscape(queue) + "' and routing_key='" +
        sqlEscape(routing_key) + "';";
    sqlExec(static_cast<sqlite3*>(db_), sql);
}

void VirtualHost::removeBindingsForExchange(const std::string& exchange) {
    if (db_ == nullptr) return;
    sqlExec(static_cast<sqlite3*>(db_),
            "delete from bindings where exchange='" + sqlEscape(exchange) +
                "';");
}

void VirtualHost::removeBindingsForQueue(const std::string& queue) {
    if (db_ == nullptr) return;
    sqlExec(static_cast<sqlite3*>(db_),
            "delete from bindings where queue='" + sqlEscape(queue) + "';");
}

BrokerResult VirtualHost::declareExchange(const ExchangeSpec& spec) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
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
    if (spec.durable) persistExchange(spec);
    return BrokerResult{};
}

BrokerResult VirtualHost::deleteExchange(const std::string& name,
                                         bool if_unused) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
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
    removeExchangeRow(name);
    removeBindingsForExchange(name);
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
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return exchanges_.find(name) != exchanges_.end();
}

BrokerResult VirtualHost::declareQueue(const QueueSpec& spec, void* owner) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const auto it = queues_.find(spec.name);
    if (it != queues_.end()) {
        const QueueSpec& existing = it->second.spec;
        if (it->second.exclusive_owner != nullptr &&
            it->second.exclusive_owner != owner) {
            return BrokerResult{false, kResourceLocked,
                                "exclusive queue is locked", 0};
        }
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
    entry.exclusive_owner = spec.exclusive ? owner : nullptr;
    // The default exchange is the empty-name direct exchange.
    entry.bindings.insert({"", spec.name});
    queues_[spec.name] = std::move(entry);
    if (spec.durable) {
        persistQueue(spec);
        persistBinding("", spec.name, spec.name);
    }
    return BrokerResult{};
}

BrokerResult VirtualHost::deleteQueue(const std::string& name, bool if_unused,
                                      bool if_empty) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const auto it = queues_.find(name);
    if (it == queues_.end()) {
        return BrokerResult{false, kNotFound, "queue not found", 0};
    }
    if (if_empty && !it->second.messages.empty()) {
        return BrokerResult{false, kPreconditionFailed,
                            "queue is not empty", 0};
    }
    if (if_unused && consumerCount(name) > 0) {
        return BrokerResult{false, kPreconditionFailed,
                            "queue is in use", 0};
    }
    const uint32_t removed =
        static_cast<uint32_t>(it->second.messages.size());
    for (auto uit = unacked_.begin(); uit != unacked_.end();) {
        if (uit->second.queue == name) {
            uit = unacked_.erase(uit);
        } else {
            ++uit;
        }
    }
    consumers_.erase(name);
    consumer_round_robin_.erase(name);
    if (it->second.spec.durable) removeQueueLog(name);
    queues_.erase(it);
    removeQueueRow(name);
    removeBindingsForQueue(name);
    return BrokerResult{true, 0, "", removed};
}

bool VirtualHost::hasQueue(const std::string& name) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return queues_.find(name) != queues_.end();
}

uint32_t VirtualHost::messageCount(const std::string& name) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const auto it = queues_.find(name);
    if (it == queues_.end()) return 0;
    return static_cast<uint32_t>(it->second.messages.size());
}

BrokerResult VirtualHost::purgeQueue(const std::string& name) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const auto it = queues_.find(name);
    if (it == queues_.end()) {
        return BrokerResult{false, kNotFound, "queue not found", 0};
    }
    expireMessages(name);
    if (it->second.spec.durable) removeQueueLog(name);
    const uint32_t removed =
        static_cast<uint32_t>(it->second.messages.size());
    it->second.messages.clear();
    return BrokerResult{true, 0, "", removed};
}

BrokerResult VirtualHost::publish(const std::string& exchange,
                                  const std::string& routing_key,
                                  const Message& message,
                                  size_t* delivered) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    ++published_count_;
    size_t count = 0;

    const auto routeToQueue = [&](const std::string& queue) {
        const auto it = queues_.find(queue);
        if (it == queues_.end()) return;
        Message copy = message;
        copy.id = next_message_id_++;
        copy.redelivered = false;
        uint64_t deadline = 0;
        if (it->second.spec.message_ttl_ms > 0) {
            deadline =
                nowMs() + static_cast<uint64_t>(
                              it->second.spec.message_ttl_ms);
        }
        if (copy.ttl_ms > 0) {
            const uint64_t message_deadline = nowMs() + copy.ttl_ms;
            deadline = deadline == 0
                           ? message_deadline
                           : std::min(deadline, message_deadline);
        }
        if (deadline != 0) {
            copy.expire_at_ms = deadline;
        }
        if (copy.persistent) {
            appendMessageLog(queue, copy);
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
    ConsumerDeliver deliver, bool no_ack, uint16_t prefetch_count,
    bool no_local, bool exclusive) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const auto queue_it = queues_.find(queue);
    if (queue_it == queues_.end()) {
        return BrokerResult{false, kNotFound, "queue not found", 0};
    }
    auto& entries = consumers_[queue];
    if (!entries.empty()) {
        for (const auto& entry : entries) {
            if (entry.exclusive || exclusive) {
                return BrokerResult{false, kAccessRefused,
                                    "exclusive consumer conflict", 0};
            }
        }
    }
    for (auto& entry : entries) {
        if (entry.owner == owner && entry.consumer_tag == consumer_tag) {
            entry.prefetch_count = prefetch_count;
            deliverPending(queue);
            return BrokerResult{};
        }
    }
    ConsumerEntry entry;
    entry.consumer_tag = consumer_tag;
    entry.owner = owner;
    entry.no_ack = no_ack;
    entry.no_local = no_local;
    entry.exclusive = exclusive;
    entry.deliver = std::move(deliver);
    entry.prefetch_count = prefetch_count;
    entries.push_back(std::move(entry));
    queue_it->second.consumer_count = entries.size();
    queue_it->second.ever_had_consumer = true;
    deliverPending(queue);
    return BrokerResult{};
}

void VirtualHost::unregisterConsumers(void* owner) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    for (auto it = consumers_.begin(); it != consumers_.end();) {
        const std::string queue = it->first;
        auto& entries = it->second;
        entries.erase(std::remove_if(entries.begin(), entries.end(),
                                     [owner](const ConsumerEntry& entry) {
                                         return entry.owner == owner;
                                     }),
                      entries.end());
        consumer_round_robin_.erase(queue);
        if (entries.empty()) {
            const auto queue_it = queues_.find(queue);
            if (queue_it != queues_.end()) {
                queue_it->second.consumer_count = 0;
            }
            const bool should_auto_delete =
                queue_it != queues_.end() &&
                queue_it->second.spec.auto_delete &&
                queue_it->second.ever_had_consumer;
            if (should_auto_delete) {
                maybeAutoDelete(queue);
                it = consumers_.begin();  // map may have changed
            } else {
                it = consumers_.erase(it);
            }
        } else {
            const auto queue_it = queues_.find(queue);
            if (queue_it != queues_.end()) {
                queue_it->second.consumer_count = entries.size();
            }
            ++it;
        }
    }
}

void VirtualHost::unregisterConsumer(const std::string& queue,
                                     const std::string& consumer_tag,
                                     void* owner) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
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
    const auto queue_it = queues_.find(queue);
    if (queue_it != queues_.end()) {
        queue_it->second.consumer_count = entries.size();
    }
    if (entries.empty()) {
        const bool should_auto_delete =
            queue_it != queues_.end() &&
            queue_it->second.spec.auto_delete &&
            queue_it->second.ever_had_consumer;
        if (should_auto_delete) {
            maybeAutoDelete(queue);
        } else {
            consumers_.erase(it);
        }
    }
}

size_t VirtualHost::consumerCount(const std::string& queue) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const auto it = consumers_.find(queue);
    return it == consumers_.end() ? 0 : it->second.size();
}

BrokerResult VirtualHost::ackMessage(uint64_t message_id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const auto it = unacked_.find(message_id);
    if (it == unacked_.end()) {
        return BrokerResult{false, kPreconditionFailed,
                            "unknown delivery tag", 0};
    }
    UnackedEntry entry = std::move(it->second);
    unacked_.erase(it);
    ++acked_count_;
    if (entry.message.persistent) {
        const auto queue_it = queues_.find(entry.queue);
        if (queue_it != queues_.end() &&
            queue_it->second.spec.durable) {
            appendTombstoneLog(entry.queue, message_id);
            compactQueueLog(entry.queue);
        }
    }
    if (!entry.consumer_tag.empty()) {
        const auto consumers_it = consumers_.find(entry.queue);
        if (consumers_it != consumers_.end()) {
            for (auto& consumer : consumers_it->second) {
                if (consumer.consumer_tag == entry.consumer_tag &&
                    consumer.unacked_count > 0) {
                    --consumer.unacked_count;
                }
            }
        }
    }
    deliverPending(entry.queue);
    return BrokerResult{};
}

BrokerResult VirtualHost::getMessage(const std::string& queue, bool no_ack,
                                     void* owner, Message* message,
                                     bool* has_message, uint32_t* remaining) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
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
    if (no_ack && message->persistent &&
        queue_it->second.spec.durable) {
        appendTombstoneLog(queue, message->id);
        compactQueueLog(queue);
    }
    if (has_message != nullptr) *has_message = true;
    if (remaining != nullptr) {
        *remaining =
            static_cast<uint32_t>(queue_it->second.messages.size());
    }
    if (!no_ack) {
        unacked_[message->id] =
            UnackedEntry{queue, *message, owner, ""};
    }
    return BrokerResult{};
}

BrokerResult VirtualHost::rejectMessage(uint64_t message_id, bool requeue) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const auto it = unacked_.find(message_id);
    if (it == unacked_.end()) {
        return BrokerResult{false, kPreconditionFailed,
                            "unknown delivery tag", 0};
    }
    UnackedEntry entry = std::move(it->second);
    unacked_.erase(it);
    if (!entry.consumer_tag.empty()) {
        const auto consumers_it = consumers_.find(entry.queue);
        if (consumers_it != consumers_.end()) {
            for (auto& consumer : consumers_it->second) {
                if (consumer.consumer_tag == entry.consumer_tag &&
                    consumer.unacked_count > 0) {
                    --consumer.unacked_count;
                }
            }
        }
    }
    if (requeue) {
        entry.message.redelivered = true;
        queues_[entry.queue].messages.push_front(entry.message);
        deliverPending(entry.queue);
    } else {
        if (entry.message.persistent) {
            const auto queue_it = queues_.find(entry.queue);
            if (queue_it != queues_.end() &&
                queue_it->second.spec.durable) {
                appendTombstoneLog(entry.queue, message_id);
                compactQueueLog(entry.queue);
            }
        }
        deadLetter(entry.queue, entry.message);
    }
    deliverPending(entry.queue);
    return BrokerResult{};
}

void VirtualHost::requeueUnacked(void* owner) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
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
        if (!entry.consumer_tag.empty()) {
            const auto consumers_it = consumers_.find(entry.queue);
            if (consumers_it != consumers_.end()) {
                for (auto& consumer : consumers_it->second) {
                    if (consumer.consumer_tag == entry.consumer_tag &&
                        consumer.unacked_count > 0) {
                        --consumer.unacked_count;
                    }
                }
            }
        }
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
    copy.ttl_ms = 0;
    publish(spec.dead_letter_exchange, copy.routing_key, copy, nullptr);
}

std::string VirtualHost::deadLetterExchange(
    const std::string& queue) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const auto it = queues_.find(queue);
    return it == queues_.end() ? std::string{}
                               : it->second.spec.dead_letter_exchange;
}

int64_t VirtualHost::messageTtl(const std::string& queue) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
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

void VirtualHost::maybeAutoDelete(const std::string& queue) {
    const auto queue_it = queues_.find(queue);
    if (queue_it == queues_.end()) return;
    if (!queue_it->second.spec.auto_delete ||
        !queue_it->second.ever_had_consumer) {
        return;
    }
    const auto consumer_it = consumers_.find(queue);
    if (consumer_it != consumers_.end() && !consumer_it->second.empty()) {
        return;
    }
    consumers_.erase(queue);
    consumer_round_robin_.erase(queue);
    queues_.erase(queue_it);
}

void VirtualHost::disconnectOwner(void* owner) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    unregisterConsumers(owner);
    requeueUnacked(owner);
    for (auto it = queues_.begin(); it != queues_.end();) {
        if (it->second.exclusive_owner == owner) {
            consumers_.erase(it->first);
            consumer_round_robin_.erase(it->first);
            it = queues_.erase(it);
        } else {
            ++it;
        }
    }
}

void VirtualHost::deliverPending(const std::string& queue) {
    const auto queue_it = queues_.find(queue);
    if (queue_it == queues_.end()) return;
    expireMessages(queue);
    size_t& index = consumer_round_robin_[queue];
    while (!queue_it->second.messages.empty()) {
        const auto consumer_it = consumers_.find(queue);
        if (consumer_it == consumers_.end() ||
            consumer_it->second.empty()) {
            return;
        }
        auto& entries = consumer_it->second;
        size_t chosen = entries.size();
        for (size_t probe = 0; probe < entries.size(); ++probe) {
            const size_t candidate = (index + probe) % entries.size();
            const ConsumerEntry& entry = entries[candidate];
            const Message& front = queue_it->second.messages.front();
            const bool can_receive =
                entry.no_ack || entry.prefetch_count == 0 ||
                entry.unacked_count < entry.prefetch_count;
            const bool no_local_skip =
                entry.no_local && front.publisher_owner != nullptr &&
                front.publisher_owner == entry.owner;
            if (can_receive) {
                if (!no_local_skip) {
                    chosen = candidate;
                    break;
                }
            }
        }
        if (chosen == entries.size()) return;

        ConsumerEntry& entry = entries[chosen];
        Message message = queue_it->second.messages.front();
        queue_it->second.messages.pop_front();
        index = chosen + 1;
        if (!entry.no_ack) {
            ++entry.unacked_count;
            unacked_[message.id] =
                UnackedEntry{queue, message, entry.owner,
                             entry.consumer_tag};
        } else if (message.persistent &&
                   queue_it->second.spec.durable) {
            appendTombstoneLog(queue, message.id);
            compactQueueLog(queue);
        }
        if (entry.deliver) {
            entry.deliver(entry.consumer_tag, queue, message);
        }
    }
}

BrokerResult VirtualHost::bind(const std::string& exchange,
                               const std::string& queue,
                               const std::string& routing_key) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!hasExchange(exchange)) {
        return BrokerResult{false, kNotFound, "exchange not found", 0};
    }
    const auto it = queues_.find(queue);
    if (it == queues_.end()) {
        return BrokerResult{false, kNotFound, "queue not found", 0};
    }
    it->second.bindings.insert({exchange, routing_key});
    if (hasExchange(exchange) && hasQueue(queue)) {
        const bool durable_both =
            exchanges_[exchange].spec.durable && queues_[queue].spec.durable;
        if (durable_both) {
            persistBinding(exchange, queue, routing_key);
        }
    }
    return BrokerResult{};
}

BrokerResult VirtualHost::unbind(const std::string& exchange,
                                 const std::string& queue,
                                 const std::string& routing_key) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const auto it = queues_.find(queue);
    if (it == queues_.end()) {
        return BrokerResult{false, kNotFound, "queue not found", 0};
    }
    const auto binding = it->second.bindings.find({exchange, routing_key});
    if (binding == it->second.bindings.end()) {
        return BrokerResult{false, kNotFound, "binding not found", 0};
    }
    it->second.bindings.erase(binding);
    removeBindingRow(exchange, queue, routing_key);
    return BrokerResult{};
}

size_t VirtualHost::bindingCount() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    size_t count = 0;
    for (const auto& queue_entry : queues_) {
        count += queue_entry.second.bindings.size();
    }
    return count;
}

size_t VirtualHost::bindingCount(const std::string& exchange,
                                 const std::string& queue) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const auto it = queues_.find(queue);
    if (it == queues_.end()) return 0;
    size_t count = 0;
    for (const auto& binding : it->second.bindings) {
        if (binding.first == exchange) ++count;
    }
    return count;
}

std::vector<QueueInfo> VirtualHost::listQueues() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<QueueInfo> result;
    result.reserve(queues_.size());
    for (const auto& entry : queues_) {
        const QueueSpec& spec = entry.second.spec;
        QueueInfo info;
        info.name = spec.name;
        info.message_count =
            static_cast<uint32_t>(entry.second.messages.size());
        info.consumer_count = entry.second.consumer_count;
        info.durable = spec.durable;
        info.exclusive = spec.exclusive;
        info.auto_delete = spec.auto_delete;
        info.dead_letter_exchange = spec.dead_letter_exchange;
        info.message_ttl_ms = spec.message_ttl_ms;
        result.push_back(std::move(info));
    }
    return result;
}

std::vector<ExchangeInfo> VirtualHost::listExchanges() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<ExchangeInfo> result;
    result.reserve(exchanges_.size());
    for (const auto& entry : exchanges_) {
        const ExchangeSpec& spec = entry.second.spec;
        ExchangeInfo info;
        info.name = spec.name;
        info.type = spec.type;
        info.durable = spec.durable;
        info.auto_delete = spec.auto_delete;
        info.internal = spec.internal;
        result.push_back(std::move(info));
    }
    return result;
}

uint64_t VirtualHost::publishedCount() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return published_count_;
}

uint64_t VirtualHost::ackedCount() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return acked_count_;
}

}  // namespace mq::broker
