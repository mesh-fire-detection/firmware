#include "Gateway.h"
#include "mesh/generated/meshtastic/mqtt.pb.h"
#include <ErriezCRC32.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <pb_encode.h>

namespace meshFire::notecard
{
namespace
{
constexpr uint32_t magic = 0x3144464d;

uint32_t get32(const Record &record, size_t offset)
{
    uint32_t value = 0;
    for (size_t i = 0; i < 4; ++i)
        value |= static_cast<uint32_t>(record[offset + i]) << (8 * i);
    return value;
}

void put32(Record &record, size_t offset, uint32_t value)
{
    for (size_t i = 0; i < 4; ++i)
        record[offset + i] = static_cast<uint8_t>(value >> (8 * i));
}

std::string base64(const uint8_t *data, size_t size)
{
    constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve(4 * ((size + 2) / 3));
    for (size_t i = 0; i < size; i += 3) {
        const uint32_t value = (static_cast<uint32_t>(data[i]) << 16) |
                               (i + 1 < size ? static_cast<uint32_t>(data[i + 1]) << 8 : 0) | (i + 2 < size ? data[i + 2] : 0);
        output += alphabet[(value >> 18) & 63];
        output += alphabet[(value >> 12) & 63];
        output += i + 1 < size ? alphabet[(value >> 6) & 63] : '=';
        output += i + 2 < size ? alphabet[value & 63] : '=';
    }
    return output;
}
} // namespace

bool eligible(const meshtastic_MeshPacket &encrypted, const meshtastic_MeshPacket &decoded, const meshtastic_Channel &channel,
              bool permission)
{
    const auto &key = channel.settings.psk;
    constexpr uint8_t publicPrefix[] = {0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59, 0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69};
    return permission && channel.has_settings && channel.role != meshtastic_Channel_Role_DISABLED &&
           channel.settings.uplink_enabled && !channel.settings.downlink_enabled && (key.size == 16 || key.size == 32) &&
           !(key.size == 16 && std::memcmp(key.bytes, publicPrefix, sizeof(publicPrefix)) == 0) &&
           encrypted.which_payload_variant == meshtastic_MeshPacket_encrypted_tag && encrypted.encrypted.size > 0 &&
           !encrypted.via_mqtt && !encrypted.pki_encrypted && !decoded.pki_encrypted && encrypted.from != 0 &&
           encrypted.from != UINT32_MAX && encrypted.id != 0 && encrypted.from == decoded.from && encrypted.id == decoded.id &&
           decoded.which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
           decoded.decoded.portnum == meshtastic_PortNum_TELEMETRY_APP;
}

size_t encodeEnvelope(const meshtastic_MeshPacket &packet, const char *channel, uint32_t gateway, uint8_t *output,
                      size_t capacity)
{
    if (channel == nullptr || gateway == 0 || gateway == UINT32_MAX || std::strlen(channel) > 12)
        return 0;
    char gatewayId[10];
    std::snprintf(gatewayId, sizeof(gatewayId), "!%08lx", static_cast<unsigned long>(gateway));
    const meshtastic_ServiceEnvelope envelope = {const_cast<meshtastic_MeshPacket *>(&packet), const_cast<char *>(channel),
                                                 gatewayId};
    auto stream = pb_ostream_from_buffer(output, capacity);
    return pb_encode(&stream, meshtastic_ServiceEnvelope_fields, &envelope) ? stream.bytes_written : 0;
}

bool validRecord(const Record &record)
{
    return get32(record, 0) == magic && get32(record, 4) != 0 && get32(record, 8) != 0 && get32(record, 12) != 0 &&
           get32(record, 16) > 0 && get32(record, 16) <= maxEnvelopeBytes &&
           crc32Buffer(record.data(), record.size() - 4) == get32(record, record.size() - 4);
}

std::string noteRequest(const Record &record)
{
    if (!validRecord(record))
        return {};
    return "{\"req\":\"note.add\",\"file\":\"mesh.qos\",\"body\":{\"version\":1,\"envelope\":\"" +
           base64(record.data() + 20, get32(record, 16)) + "\"},\"max\":256,\"sync\":true,\"verify\":true}";
}

bool Queue::recover()
{
    slots = {};
    pendingAck = -1;
    fault = false;
    for (size_t i = 0; i < slots.size(); ++i) {
        Record record{};
        const auto result = storage.read(i, record);
        if (result == ReadResult::Missing)
            continue;
        if (result == ReadResult::Error || !validRecord(record)) {
            fault = true;
            continue;
        }
        slots[i] = {get32(record, 4), get32(record, 8), get32(record, 12)};
        for (size_t j = 0; j < i; ++j)
            if (slots[j].sequence == slots[i].sequence)
                fault = true;
    }
    return !fault;
}

size_t Queue::count() const
{
    return std::count_if(slots.begin(), slots.end(), [](const Slot &slot) { return slot.sequence != 0; });
}

EnqueueResult Queue::enqueue(uint32_t from, uint32_t packetId, const uint8_t *envelope, size_t size)
{
    if (fault || from == 0 || packetId == 0 || envelope == nullptr || size == 0 || size > maxEnvelopeBytes)
        return EnqueueResult::Fault;
    size_t freeSlot = slots.size();
    uint32_t sequence = 0;
    for (size_t i = 0; i < slots.size(); ++i) {
        if (slots[i].sequence != 0 && slots[i].from == from && slots[i].packetId == packetId)
            return EnqueueResult::Duplicate;
        if (slots[i].sequence == 0)
            freeSlot = i;
        sequence = std::max(sequence, slots[i].sequence);
    }
    if (freeSlot == slots.size())
        return EnqueueResult::Full;
    if (sequence == std::numeric_limits<uint32_t>::max())
        return EnqueueResult::Fault;
    Record record{};
    put32(record, 0, magic);
    put32(record, 4, sequence + 1);
    put32(record, 8, from);
    put32(record, 12, packetId);
    put32(record, 16, static_cast<uint32_t>(size));
    std::copy_n(envelope, size, record.begin() + 20);
    put32(record, record.size() - 4, crc32Buffer(record.data(), record.size() - 4));
    if (!storage.write(freeSlot, record)) {
        fault = true;
        return EnqueueResult::Fault;
    }
    slots[freeSlot] = {sequence + 1, from, packetId};
    return EnqueueResult::Stored;
}

PumpResult Queue::pump(Transport &transport)
{
    if (fault)
        return PumpResult::Fault;
    if (pendingAck < 0) {
        size_t oldest = slots.size();
        uint32_t sequence = UINT32_MAX;
        for (size_t i = 0; i < slots.size(); ++i) {
            if (slots[i].sequence != 0 && (oldest == slots.size() || slots[i].sequence < sequence)) {
                sequence = slots[i].sequence;
                oldest = i;
            }
        }
        if (oldest == slots.size())
            return PumpResult::Empty;
        Record record{};
        if (storage.read(oldest, record) != ReadResult::Found || !validRecord(record) ||
            get32(record, 4) != slots[oldest].sequence || get32(record, 8) != slots[oldest].from ||
            get32(record, 12) != slots[oldest].packetId) {
            fault = true;
            return PumpResult::Fault;
        }
        if (!transport.send(record))
            return PumpResult::Retry;
        pendingAck = static_cast<int>(oldest);
    }
    if (!storage.erase(static_cast<size_t>(pendingAck)))
        return PumpResult::Retry;
    slots[static_cast<size_t>(pendingAck)] = {};
    pendingAck = -1;
    return PumpResult::Sent;
}
} // namespace meshFire::notecard
