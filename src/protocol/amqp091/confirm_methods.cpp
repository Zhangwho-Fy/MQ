#include "mq/protocol/amqp091/confirm_methods.hpp"

#include "mq/protocol/amqp091/wire_reader.hpp"
#include "mq/protocol/amqp091/wire_writer.hpp"

namespace mq::amqp091 {

namespace {

bool readBits(WireReader& reader, bool& bit) {
    uint8_t octet = 0;
    if (!reader.readU8(octet)) return false;
    bit = (octet & 0x80U) != 0;
    return true;
}

void writeBits(WireWriter& writer, bool bit) {
    writer.writeU8(bit ? 0x80 : 0x00);
}

}  // namespace

std::string encodeConfirmSelect(const ConfirmSelect& select) {
    WireWriter writer;
    writeBits(writer, select.no_wait);
    return writer.takeBytes();
}

bool decodeConfirmSelect(std::string_view arguments, ConfirmSelect& select,
                         std::string& error) {
    WireReader reader(arguments);
    if (!readBits(reader, select.no_wait)) {
        error = "invalid confirm.select";
        return false;
    }
    return true;
}

std::string encodeConfirmSelectOk() {
    return {};
}

}  // namespace mq::amqp091
