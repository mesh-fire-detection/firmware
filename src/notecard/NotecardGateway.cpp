#include "NotecardGateway.h"

#if defined(MESH_FIRE_DETECTION) && defined(ARCH_NRF52) && !MESHTASTIC_EXCLUDE_I2C
#include "Channels.h"
#include "FSCommon.h"
#include "Gateway.h"
#include "NodeDB.h"
#include "Router.h"
#include "SPILock.h"
#include "Throttle.h"
#include "concurrency/LockGuard.h"
#include "concurrency/OSThread.h"
#include <Notecard.h>
#include <Wire.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace
{
using namespace meshFire::notecard;
constexpr uint8_t notecardAddress = 0x17;
constexpr char directory[] = "/mfdq";

class FlashStorage : public Storage
{
    static void filename(size_t slot, char *path, size_t size, bool temporary = false)
    {
        std::snprintf(path, size, "/mfdq/q%u%s", static_cast<unsigned>(slot), temporary ? ".tmp" : "");
    }

    static bool readFile(const char *path, Record &record)
    {
        auto file = FSCom.open(path, FILE_O_READ);
        if (!file)
            return false;
        const bool valid = file.size() == record.size() && file.read(record.data(), record.size()) == record.size();
        file.close();
        return valid && validRecord(record);
    }

    static bool matchesIdentity(const char *path, const std::string &identity)
    {
        auto file = FSCom.open(path, FILE_O_READ);
        if (!file)
            return false;
        char bytes[192] = {};
        const size_t size = file.size();
        const bool matches = size == identity.size() && size < sizeof(bytes) && file.read(bytes, size) == size &&
                             std::memcmp(bytes, identity.data(), size) == 0;
        file.close();
        return matches;
    }

  public:
    static bool hasCapacity()
    {
        concurrency::LockGuard guard(spiLock);
        // Adafruit has no capacity API; traversal must hold its filesystem mutex.
        FSCom._lockFS();
        auto *fs = FSCom._getFS();
        size_t used = 0;
        const auto result = lfs_traverse(
            fs,
            [](void *context, lfs_block_t) {
                ++*static_cast<size_t *>(context);
                return 0;
            },
            &used);
        const size_t blocks = fs->cfg->block_count;
        const size_t blockSize = fs->cfg->block_size;
        FSCom._unlockFS();
        constexpr size_t preferenceReserve = 8 * 1024;
        constexpr size_t stagingAllowance = 1024;
        return result >= 0 && used <= blocks && (blocks - used) * blockSize >= preferenceReserve + stagingAllowance;
    }

    ReadResult read(size_t slot, Record &record) override
    {
        concurrency::LockGuard guard(spiLock);
        char path[24], temporary[24];
        filename(slot, path, sizeof(path));
        filename(slot, temporary, sizeof(temporary), true);
        if (FSCom.exists(temporary)) {
            if (FSCom.exists(path) || !readFile(temporary, record) || !FSCom.rename(temporary, path))
                return ReadResult::Error;
        }
        if (!FSCom.exists(path))
            return ReadResult::Missing;
        return readFile(path, record) ? ReadResult::Found : ReadResult::Error;
    }

    bool write(size_t slot, const Record &record) override
    {
        concurrency::LockGuard guard(spiLock);
        if (!FSCom.exists(directory) && !FSCom.mkdir(directory))
            return false;
        char path[24], temporary[24];
        filename(slot, path, sizeof(path));
        filename(slot, temporary, sizeof(temporary), true);
        if (FSCom.exists(path) || FSCom.exists(temporary))
            return false;
        auto file = FSCom.open(temporary, FILE_O_WRITE);
        if (!file)
            return false;
        const bool written = file.write(record.data(), record.size()) == record.size();
        file.flush();
        file.close();
        Record verified{};
        return written && readFile(temporary, verified) && verified == record && FSCom.rename(temporary, path);
    }

    bool erase(size_t slot) override
    {
        concurrency::LockGuard guard(spiLock);
        char path[24];
        filename(slot, path, sizeof(path));
        return FSCom.remove(path);
    }

    bool bind(const std::string &identity, bool empty)
    {
        concurrency::LockGuard guard(spiLock);
        constexpr char path[] = "/mfdq/identity";
        constexpr char temporary[] = "/mfdq/identity.tmp";
        if (FSCom.exists(path))
            return !FSCom.exists(temporary) && matchesIdentity(path, identity);
        if (FSCom.exists(temporary))
            return matchesIdentity(temporary, identity) && FSCom.rename(temporary, path);
        if (!empty || identity.size() >= 192 || (!FSCom.exists(directory) && !FSCom.mkdir(directory)))
            return false;
        auto file = FSCom.open(temporary, FILE_O_WRITE);
        if (!file)
            return false;
        const bool written = file.write(reinterpret_cast<const uint8_t *>(identity.data()), identity.size()) == identity.size();
        file.flush();
        file.close();
        return written && matchesIdentity(temporary, identity) && FSCom.rename(temporary, path);
    }
};

class NotecardGateway : private concurrency::OSThread, private Transport
{
    concurrency::Lock queueLock;
    FlashStorage storage;
    Queue queue{storage};
    Notecard card;
    bool enabled = false;
    bool ready = false;
    bool checked = false;
    bool templateFault = false;
    bool queueHealthy;
    uint32_t lastCheck = 0;
    uint32_t backoff = 5000;
    uint32_t rejected = 0;

    J *request(J *req)
    {
        if (!req)
            return nullptr;
        const auto timeout = NoteSetRequestTimeout(2);
        J *response = card.requestAndResponse(req);
        NoteSetRequestTimeout(timeout);
        return response;
    }

    bool checkConfiguration()
    {
        Wire.beginTransmission(notecardAddress);
        if (Wire.endTransmission() != 0)
            return false;
        J *req = card.newRequest("note.get");
        if (!req)
            return false;
        JAddStringToObject(req, "file", "mfd-config.dbx");
        JAddStringToObject(req, "note", "gateway");
        J *response = request(req);
        if (!response)
            return false;
        if (NoteResponseError(response)) {
            card.deleteResponse(response);
            return false;
        }
        J *body = JGetObject(response, "body");
        const bool provisioned = JGetBool(body, "enabled");
        const std::string product = JGetString(body, "product");
        card.deleteResponse(response);
        if (!provisioned || product.empty() || product.size() > 128 || product.find('\n') != std::string::npos) {
            enabled = false;
            return false;
        }
        response = request(card.newRequest("hub.get"));
        if (!response)
            return false;
        const std::string device = JGetString(response, "device");
        const bool matches = !NoteResponseError(response) && product == JGetString(response, "product") && device.size() == 19 &&
                             device.compare(0, 4, "dev:") == 0 &&
                             std::all_of(device.begin() + 4, device.end(), [](char ch) { return ch >= '0' && ch <= '9'; });
        card.deleteResponse(response);
        if (!matches) {
            enabled = false;
            return false;
        }
        const std::string identity = "v1\n" + device + "\n" + product + "\n" + nodeDB->getNodeId();
        if (!queueHealthy || !storage.bind(identity, queue.count() == 0)) {
            enabled = false;
            LOG_ERROR("Notecard queue binding/storage fault; preserve /mfdq for inspection");
            return false;
        }
        if (!enabled)
            LOG_INFO("Notecard gateway enabled; pending=%u", static_cast<unsigned>(queue.count()));
        enabled = true;
        return true;
    }

    bool send(const Record &record) override
    {
        const auto json = noteRequest(record);
        J *response = request(JParse(json.c_str()));
        if (!response)
            return false;
        const bool accepted = !NoteResponseError(response) && JGetNumber(response, "total") > 0;
        templateFault = JGetBool(response, "template");
        card.deleteResponse(response);
        if (templateFault)
            LOG_ERROR("Notecard mesh.qos must be untemplated; pending packet retained");
        return accepted && !templateFault;
    }

    int32_t runOnce() override
    {
        concurrency::LockGuard guard(&queueLock);
        if (!checked || !Throttle::isWithinTimespanMs(lastCheck, ready ? 60000 : 5000)) {
            checked = true;
            lastCheck = millis();
            const bool wasReady = ready;
            ready = checkConfiguration();
            if (wasReady && !ready)
                LOG_WARN("Notecard not ready; queued packets retained");
        }
        if (!enabled || !ready || templateFault || config.power.is_power_saving)
            return 5000;
        const auto result = queue.pump(*this);
        if (result == PumpResult::Fault) {
            queueHealthy = false;
            enabled = false;
            LOG_ERROR("Notecard queue fault; pending files retained for inspection");
        }
        if (result == PumpResult::Retry) {
            LOG_WARN("Notecard handoff failed; pending=%u, retryMs=%u", static_cast<unsigned>(queue.count()), backoff);
            const auto delayMs = backoff;
            backoff = std::min<uint32_t>(backoff * 2, 60000);
            return static_cast<int32_t>(delayMs);
        }
        backoff = 5000;
        return result == PumpResult::Sent ? 1000 : 5000;
    }

  public:
    NotecardGateway() : concurrency::OSThread("notecard"), queueHealthy(queue.recover())
    {
        card.begin(notecardAddress, 32, Wire);
        card.setDebugOutputStream(nullptr);
        setIntervalFromNow(5000);
    }

    void capture(const meshtastic_MeshPacket &encrypted, const meshtastic_MeshPacket &decoded, uint8_t channelIndex)
    {
        concurrency::LockGuard guard(&queueLock);
        if (!enabled || config.power.is_power_saving || templateFault || channelIndex >= channels.getNumChannels())
            return;
        const bool permission = decoded.from == nodeDB->getNodeNum()
                                    ? config.lora.config_ok_to_mqtt
                                    : decoded.decoded.has_bitfield && (decoded.decoded.bitfield & BITFIELD_OK_TO_MQTT_MASK);
        if (!eligible(encrypted, decoded, channels.getByIndex(channelIndex), permission))
            return;
        if (!FlashStorage::hasCapacity()) {
            LOG_WARN("Notecard packet not queued; flash reserve reached, rejected=%u", ++rejected);
            return;
        }
        std::array<uint8_t, maxEnvelopeBytes> envelope{};
        const auto size =
            encodeEnvelope(encrypted, channels.getGlobalId(channelIndex), nodeDB->getNodeNum(), envelope.data(), envelope.size());
        const auto result = queue.enqueue(encrypted.from, encrypted.id, envelope.data(), size);
        if (result == EnqueueResult::Full || result == EnqueueResult::Fault) {
            ++rejected;
            LOG_WARN("Notecard packet not queued; reason=%s, rejected=%u, pending=%u",
                     result == EnqueueResult::Full ? "full" : "storage/encoding", rejected, static_cast<unsigned>(queue.count()));
        }
    }
};

NotecardGateway *gateway = nullptr;
} // namespace

void initNotecardGateway()
{
    if (!gateway)
        gateway = new NotecardGateway();
}

void queueNotecardTelemetry(const meshtastic_MeshPacket &encrypted, const meshtastic_MeshPacket &decoded, uint8_t channelIndex)
{
    if (gateway)
        gateway->capture(encrypted, decoded, channelIndex);
}
#endif
