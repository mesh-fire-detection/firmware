#include "mesh/generated/meshtastic/mqtt.pb.h"
#include "notecard/Gateway.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <pb_decode.h>
#include <unity.h>
#include <vector>

using namespace meshFire::notecard;

class MemoryStorage : public Storage
{
  public:
    std::array<Record, queueSlots> records{};
    std::array<bool, queueSlots> present{};
    bool failWrite = false;
    bool failErase = false;

    ReadResult read(size_t slot, Record &record) override
    {
        record = records[slot];
        return present[slot] ? ReadResult::Found : ReadResult::Missing;
    }
    bool write(size_t slot, const Record &record) override
    {
        if (failWrite)
            return false;
        present[slot] = true;
        records[slot] = record;
        return true;
    }
    bool erase(size_t slot) override
    {
        if (failErase)
            return false;
        present[slot] = false;
        return true;
    }
};

class FakeTransport : public Transport
{
  public:
    bool online = true;
    std::vector<Record> sent;
    bool send(const Record &record) override
    {
        if (!online)
            return false;
        sent.push_back(record);
        return true;
    }
};

void setUp() {}
void tearDown() {}

meshtastic_MeshPacket encryptedPacket()
{
    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
    // Synthetic AES-128 telemetry fixture, shared with the backend's test key.
    constexpr uint8_t ciphertext[] = {0xff, 0x17, 0x3d, 0x8d, 0x3f, 0x40, 0xda, 0x61, 0x28, 0x79,
                                      0x07, 0xa2, 0x0c, 0xb5, 0xc7, 0x1d, 0x18, 0x0c, 0x90};
    packet.from = 0x0a000002;
    packet.to = UINT32_MAX;
    packet.id = 0x1234;
    packet.hop_start = 3;
    packet.hop_limit = 3;
    packet.rx_snr = 5.5;
    packet.rx_rssi = -101;
    packet.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    packet.encrypted.size = sizeof(ciphertext);
    std::copy_n(ciphertext, sizeof(ciphertext), packet.encrypted.bytes);
    return packet;
}

meshtastic_Channel privateChannel()
{
    meshtastic_Channel channel = meshtastic_Channel_init_zero;
    channel.role = meshtastic_Channel_Role_PRIMARY;
    channel.has_settings = true;
    channel.settings.uplink_enabled = true;
    channel.settings.psk.size = 16;
    return channel;
}

void test_private_uplink_permission_is_mandatory()
{
    auto encrypted = encryptedPacket();
    auto decoded = encrypted;
    decoded.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    decoded.decoded.portnum = meshtastic_PortNum_TELEMETRY_APP;
    auto channel = privateChannel();
    TEST_ASSERT_TRUE(eligible(encrypted, decoded, channel, true));
    TEST_ASSERT_FALSE(eligible(encrypted, decoded, channel, false));
    channel.settings.uplink_enabled = false;
    TEST_ASSERT_FALSE(eligible(encrypted, decoded, channel, true));
    channel.settings.uplink_enabled = true;
    channel.settings.downlink_enabled = true;
    TEST_ASSERT_FALSE(eligible(encrypted, decoded, channel, true));
    channel.settings.downlink_enabled = false;
    for (const auto size : {0, 1, 15, 17}) {
        channel.settings.psk.size = size;
        TEST_ASSERT_FALSE(eligible(encrypted, decoded, channel, true));
    }
    channel.settings.psk.size = 32;
    TEST_ASSERT_TRUE(eligible(encrypted, decoded, channel, true));
    constexpr uint8_t publicPrefix[] = {0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59, 0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69};
    channel.settings.psk.size = 16;
    std::copy_n(publicPrefix, sizeof(publicPrefix), channel.settings.psk.bytes);
    for (unsigned i = 0; i < 256; ++i) {
        channel.settings.psk.bytes[15] = static_cast<uint8_t>(i);
        TEST_ASSERT_FALSE(eligible(encrypted, decoded, channel, true));
    }
}

void test_only_original_channel_encrypted_telemetry_is_forwarded()
{
    const auto packet = encryptedPacket();
    auto decoded = packet;
    decoded.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    decoded.decoded.portnum = meshtastic_PortNum_TELEMETRY_APP;
    const auto channel = privateChannel();
    auto altered = packet;
    altered.via_mqtt = true;
    TEST_ASSERT_FALSE(eligible(altered, decoded, channel, true));
    altered = packet;
    altered.pki_encrypted = true;
    TEST_ASSERT_FALSE(eligible(altered, decoded, channel, true));
    altered = decoded;
    TEST_ASSERT_FALSE(eligible(altered, decoded, channel, true));
    decoded.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    TEST_ASSERT_FALSE(eligible(packet, decoded, channel, true));
    decoded.decoded.portnum = meshtastic_PortNum_TELEMETRY_APP;
    decoded.from++;
    TEST_ASSERT_FALSE(eligible(packet, decoded, channel, true));
}

void test_service_envelope_preserves_packet_and_gateway_identity()
{
    const auto packet = encryptedPacket();
    std::array<uint8_t, maxEnvelopeBytes> bytes{};
    const auto size = encodeEnvelope(packet, "FireNet", 0x0a000001, bytes.data(), bytes.size());
    TEST_ASSERT_GREATER_THAN(0, size);
    meshtastic_ServiceEnvelope envelope = meshtastic_ServiceEnvelope_init_zero;
    auto stream = pb_istream_from_buffer(bytes.data(), size);
    TEST_ASSERT_TRUE(pb_decode(&stream, meshtastic_ServiceEnvelope_fields, &envelope));
    TEST_ASSERT_EQUAL_STRING("!0a000001", envelope.gateway_id);
    TEST_ASSERT_EQUAL_STRING("FireNet", envelope.channel_id);
    TEST_ASSERT_EQUAL_UINT32(packet.from, envelope.packet->from);
    TEST_ASSERT_EQUAL_UINT32(packet.id, envelope.packet->id);
    TEST_ASSERT_EQUAL_MEMORY(packet.encrypted.bytes, envelope.packet->encrypted.bytes, packet.encrypted.size);
    pb_release(meshtastic_ServiceEnvelope_fields, &envelope);
    TEST_ASSERT_EQUAL(0, encodeEnvelope(packet, "FireNet", 0, bytes.data(), bytes.size()));
    TEST_ASSERT_EQUAL(0, encodeEnvelope(packet, "FireNet", 1, bytes.data(), 1));
}

void test_outage_and_restart_keep_packets_in_fifo_order()
{
    MemoryStorage storage;
    Queue queue(storage);
    FakeTransport transport;
    TEST_ASSERT_TRUE(queue.recover());
    const uint8_t first[] = {1, 2, 3}, second[] = {4, 5};
    TEST_ASSERT_EQUAL(EnqueueResult::Stored, queue.enqueue(1, 10, first, sizeof(first)));
    TEST_ASSERT_EQUAL(EnqueueResult::Stored, queue.enqueue(2, 20, second, sizeof(second)));
    transport.online = false;
    TEST_ASSERT_EQUAL(PumpResult::Retry, queue.pump(transport));
    TEST_ASSERT_EQUAL(2, queue.count());
    Queue restarted(storage);
    TEST_ASSERT_TRUE(restarted.recover());
    transport.online = true;
    TEST_ASSERT_EQUAL(PumpResult::Sent, restarted.pump(transport));
    TEST_ASSERT_EQUAL(PumpResult::Sent, restarted.pump(transport));
    TEST_ASSERT_EQUAL(PumpResult::Empty, restarted.pump(transport));
    TEST_ASSERT_EQUAL_UINT8(1, transport.sent[0][20]);
    TEST_ASSERT_EQUAL_UINT8(4, transport.sent[1][20]);
}

void test_full_queue_never_overwrites_pending_packets()
{
    MemoryStorage storage;
    Queue queue(storage);
    queue.recover();
    const uint8_t bytes[] = {1};
    for (size_t i = 0; i < queueSlots; ++i)
        TEST_ASSERT_EQUAL(EnqueueResult::Stored, queue.enqueue(1, i + 1, bytes, sizeof(bytes)));
    const auto original = storage.records;
    TEST_ASSERT_EQUAL(EnqueueResult::Full, queue.enqueue(2, 99, bytes, sizeof(bytes)));
    TEST_ASSERT_EQUAL(EnqueueResult::Duplicate, queue.enqueue(1, 1, bytes, sizeof(bytes)));
    TEST_ASSERT_TRUE(original == storage.records);
}

void test_write_failure_is_not_acknowledged_as_queued()
{
    MemoryStorage storage;
    Queue queue(storage);
    queue.recover();
    storage.failWrite = true;
    const uint8_t bytes[] = {1};
    TEST_ASSERT_EQUAL(EnqueueResult::Fault, queue.enqueue(1, 1, bytes, sizeof(bytes)));
    TEST_ASSERT_EQUAL(0, queue.count());
}

void test_handoff_ack_is_required_before_local_deletion()
{
    MemoryStorage storage;
    Queue queue(storage);
    queue.recover();
    FakeTransport transport;
    const uint8_t bytes[] = {1};
    queue.enqueue(1, 1, bytes, sizeof(bytes));
    storage.failErase = true;
    TEST_ASSERT_EQUAL(PumpResult::Retry, queue.pump(transport));
    TEST_ASSERT_EQUAL(1, queue.count());
    TEST_ASSERT_EQUAL(PumpResult::Retry, queue.pump(transport));
    TEST_ASSERT_EQUAL(1, transport.sent.size());
    storage.failErase = false;
    TEST_ASSERT_EQUAL(PumpResult::Sent, queue.pump(transport));
    TEST_ASSERT_EQUAL(1, transport.sent.size());
}

void test_corruption_stops_forwarding_and_retains_data()
{
    MemoryStorage storage;
    Queue queue(storage);
    queue.recover();
    const uint8_t bytes[] = {1};
    queue.enqueue(1, 1, bytes, sizeof(bytes));
    const auto slot = std::find(storage.present.begin(), storage.present.end(), true) - storage.present.begin();
    storage.records[slot][20] ^= 1;
    Queue restarted(storage);
    TEST_ASSERT_FALSE(restarted.recover());
    FakeTransport transport;
    TEST_ASSERT_EQUAL(PumpResult::Fault, restarted.pump(transport));
    TEST_ASSERT_TRUE(storage.present[slot]);
    TEST_ASSERT_EQUAL(0, transport.sent.size());
}

void test_power_loss_after_handoff_retries_the_identical_packet()
{
    MemoryStorage storage;
    Queue queue(storage);
    queue.recover();
    FakeTransport transport;
    const uint8_t bytes[] = {10, 20, 30};
    queue.enqueue(1, 1, bytes, sizeof(bytes));
    storage.failErase = true;
    TEST_ASSERT_EQUAL(PumpResult::Retry, queue.pump(transport));
    Queue restarted(storage);
    TEST_ASSERT_TRUE(restarted.recover());
    storage.failErase = false;
    TEST_ASSERT_EQUAL(PumpResult::Sent, restarted.pump(transport));
    TEST_ASSERT_EQUAL(2, transport.sent.size());
    TEST_ASSERT_TRUE(transport.sent[0] == transport.sent[1]);
}

void test_note_body_matches_backend_v1_and_can_export_interop_fixture()
{
    MemoryStorage storage;
    Queue queue(storage);
    queue.recover();
    const auto packet = encryptedPacket();
    std::array<uint8_t, maxEnvelopeBytes> bytes{};
    const auto size = encodeEnvelope(packet, "FireNet", 0x0a000001, bytes.data(), bytes.size());
    queue.enqueue(packet.from, packet.id, bytes.data(), size);
    FakeTransport transport;
    queue.pump(transport);
    const auto json = noteRequest(transport.sent[0]);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, json.find("\"file\":\"mesh.qos\""));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, json.find("\"body\":{\"version\":1,\"envelope\":\""));
    TEST_ASSERT_EQUAL(std::string::npos, json.find("\"payload\""));
    if (const char *path = std::getenv("MFD_NOTE_FIXTURE")) {
        std::ofstream file(path);
        file << json;
        TEST_ASSERT_TRUE(file.good());
    }
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_private_uplink_permission_is_mandatory);
    RUN_TEST(test_only_original_channel_encrypted_telemetry_is_forwarded);
    RUN_TEST(test_service_envelope_preserves_packet_and_gateway_identity);
    RUN_TEST(test_outage_and_restart_keep_packets_in_fifo_order);
    RUN_TEST(test_full_queue_never_overwrites_pending_packets);
    RUN_TEST(test_write_failure_is_not_acknowledged_as_queued);
    RUN_TEST(test_handoff_ack_is_required_before_local_deletion);
    RUN_TEST(test_corruption_stops_forwarding_and_retains_data);
    RUN_TEST(test_power_loss_after_handoff_retries_the_identical_packet);
    RUN_TEST(test_note_body_matches_backend_v1_and_can_export_interop_fixture);
    return UNITY_END();
}
