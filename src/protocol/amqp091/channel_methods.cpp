#include "mq/protocol/amqp091/channel_methods.hpp"

#include "mq/protocol/amqp091/wire_reader.hpp"
#include "mq/protocol/amqp091/wire_writer.hpp"


namespace mq::amqp091 {

namespace {

std::string encodeBitArgument(bool value) {
    WireWriter writer;
    // Method argument bits are packed least-significant bit first.
    writer.writeU8(value ? 0x01 : 0x00);
    return writer.takeBytes();
}

bool decodeBitArgument(std::string_view arguments, bool& value,
                       std::string& error) {
    WireReader reader(arguments);
    uint8_t octet = 0;
    if (!reader.readU8(octet) || reader.remaining() != 0) {
        error = "invalid channel flow bit argument";
        return false;
    }

    value = (octet & 0x01) != 0;
    return true;
}

}  // namespace

std::string encodeChannelOpen(const ChannelOpen&) {
    return {};
}

bool decodeChannelOpen(std::string_view arguments, std::string& error) {
    if (arguments.empty()) return true;
    WireReader reader(arguments);
    std::string reserved;
    if (!readShortString(reader, reserved)) {
        error = "invalid channel.open reserved field";
        return false;
    }

    return true;
}

std::string encodeChannelOpenOk() {
    WireWriter writer;
    writer.writeU32(0);  // reserved channel-id longstr
    return writer.takeBytes();
}

std::string encodeChannelFlow(const ChannelFlow& flow) {
    return encodeBitArgument(flow.active);
}

bool decodeChannelFlow(std::string_view arguments, ChannelFlow& flow,
                       std::string& error) {
    return decodeBitArgument(arguments, flow.active, error);
}

std::string encodeChannelFlowOk(const ChannelFlow& flow) {
    return encodeBitArgument(flow.active);
}

bool decodeChannelFlowOk(std::string_view arguments, ChannelFlow& flow,
                         std::string& error) {
    return decodeBitArgument(arguments, flow.active, error);
}

std::string encodeChannelClose(const ChannelClose& close) {
    WireWriter writer;
    writer.writeU16(close.reply_code);
    writeShortString(writer, close.reply_text);
    writer.writeU16(close.class_id);
    writer.writeU16(close.method_id);
    return writer.takeBytes();
}

bool decodeChannelClose(std::string_view arguments, ChannelClose& close,
                        std::string& error) {
    WireReader reader(arguments);
    if (!reader.readU16(close.reply_code) ||
        !readShortString(reader, close.reply_text) ||
        !reader.readU16(close.class_id) || !reader.readU16(close.method_id)) {
        error = "truncated channel.close";
        return false;
    }

    return true;
}

std::string encodeChannelCloseOk() {
    return {};
}

}  // namespace mq::amqp091
