#ifndef MQ_PROTOCOL_AMQP091_FIELDS_HPP
#define MQ_PROTOCOL_AMQP091_FIELDS_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mq::amqp091 {

// A field-table entry is stored as its AMQP type tag plus the raw encoded
// value bytes. This preserves arbitrary peer properties and is sufficient
// for both decoding peer tables and constructing server tables.
struct FieldTableEntry {
    std::string name;
    char type = 0;
    std::string value;
};

class FieldTable {
public:
    const std::vector<FieldTableEntry>& entries() const { return entries_; }

    void addBool(const std::string& name, bool value);
    void addString(const std::string& name, const std::string& value);
    void addTable(const std::string& name, const std::string& encoded_table);
    void addInt32(const std::string& name, int32_t value);
    void addEntry(FieldTableEntry entry);

    const std::string* findString(const std::string& name) const;
    bool findBool(const std::string& name, bool& value) const;
    bool findInt64(const std::string& name, int64_t& value) const;

private:
    std::vector<FieldTableEntry> entries_;
};

// Wire format helpers: encoded table is u32 length followed by entries.
std::string encodeFieldTable(const FieldTable& table);
bool decodeFieldTable(std::string_view bytes, FieldTable& table, std::string& error);
bool decodeFieldTableBody(std::string_view body, FieldTable& table,
                          std::string& error);

}  // namespace mq::amqp091

#endif  // MQ_PROTOCOL_AMQP091_FIELDS_HPP
