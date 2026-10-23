#pragma once

#include "mesh/generated/meshtastic/channel.pb.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace meshFire::notecard
{

constexpr size_t maxEnvelopeBytes = 384;
constexpr size_t queueSlots = 8;
constexpr size_t recordBytes = 20 + maxEnvelopeBytes + 4;
using Record = std::array<uint8_t, recordBytes>;

bool eligible(const meshtastic_MeshPacket &encrypted, const meshtastic_MeshPacket &decoded, const meshtastic_Channel &channel,
              bool permission);
size_t encodeEnvelope(const meshtastic_MeshPacket &packet, const char *channel, uint32_t gateway, uint8_t *output,
                      size_t capacity);
std::string noteRequest(const Record &record);
bool validRecord(const Record &record);

enum class ReadResult { Missing, Found, Error };
enum class EnqueueResult { Stored, Duplicate, Full, Fault };
enum class PumpResult { Empty, Sent, Retry, Fault };

class Storage
{
  public:
    virtual ~Storage() = default;
    virtual ReadResult read(size_t slot, Record &record) = 0;
    virtual bool write(size_t slot, const Record &record) = 0;
    virtual bool erase(size_t slot) = 0;
};

class Transport
{
  public:
    virtual ~Transport() = default;
    virtual bool send(const Record &record) = 0;
};

class Queue
{
  public:
    explicit Queue(Storage &storage) : storage(storage) {}
    bool recover();
    EnqueueResult enqueue(uint32_t from, uint32_t packetId, const uint8_t *envelope, size_t size);
    PumpResult pump(Transport &transport);
    size_t count() const;

  private:
    struct Slot {
        uint32_t sequence = 0;
        uint32_t from = 0;
        uint32_t packetId = 0;
    };
    Storage &storage;
    std::array<Slot, queueSlots> slots{};
    int pendingAck = -1;
    bool fault = true;
};

} // namespace meshFire::notecard
