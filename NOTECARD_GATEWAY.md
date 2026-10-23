# Notecard gateway setup and recovery

The shared RAK4631 `mesh-fire-sensor` image includes a Blues Notecard transport.
It forwards encrypted Meshtastic telemetry to the backend's Notehub adapter.
Sensor-only nodes use the same image. The transport is disabled until explicitly
provisioned; a successful build does not establish real cellular delivery.

## Hardware and provisioning

1. Use the RAK4631 / RAK19007 with a RAK13102 and compatible cellular Notecard.
   Connect the antennas and supply power as specified in the
   [RAK13102 quick-start](https://docs.rakwireless.com/product-categories/wisblock/rak13102/quickstart/).
   The carrier communicates over the main I2C bus at address `0x17`. USB-only
   power is not sufficient evidence that cellular transmission will work.
2. Provision the same private 16- or 32-byte channel key on sensors, gateway,
   and backend. Enable uplink only on the intended private channel; disable
   downlink and secondary public channels. Sensor `config_ok_to_mqtt` permission
   must be enabled. A gateway should use CLIENT role with power saving off so
   it can hear the mesh continuously. This change does not modify saved radio,
   role, sensor, or sleep settings and does not enable radio transmission.
3. Connect the RAK13102's own USB port to a Blues-compatible Notecard terminal.
   This is separate from the Meshtastic core's protobuf serial connection.
   Provision the project using a real ProductUID from your Notehub account:

   ```json
   {
     "req": "hub.set",
     "product": "<your-product-uid>",
     "mode": "periodic",
     "outbound": 1
   }
   ```

   These settings persist on the Notecard. Verify them with `hub.get`; record
   the returned `device` UID for backend gateway registration. Do not put the
   backend bearer credential into the firmware, channel settings, or this
   local configuration.

4. Explicitly enable the bridge using a local-only Notecard database Note:

   ```json
   {
     "req": "note.update",
     "file": "mfd-config.dbx",
     "note": "gateway",
     "body": { "enabled": true, "product": "<your-product-uid>" }
   }
   ```

   The product must exactly match `hub.get`. `.dbx` is local-only; firmware
   does not subscribe to cloud commands or accept remote configuration. No
   provisioning request is sent automatically. Configuration is checked before
   forwarding, then every 60 seconds while ready, or every 5 seconds when not
   ready. Missing configuration leaves an unprovisioned node disabled.

5. Deploy the backend Notehub adapter from
   [backend PR #27](https://github.com/mesh-fire-detection/backend/pull/27), enable
   `NOTEHUB_ENABLED`, and register both sensors and gateway. Assign the gateway
   its real Notecard UID and a separate Notehub route credential. Follow the
   [backend runbook](https://github.com/mesh-fire-detection/backend/blob/main/docs/NOTEHUB.md)
   to configure an HTTPS route for this gateway's `mesh.qos` events. Use the
   full event JSON, with the credential in an authorization header stored as a
   Notehub Secret. The firmware does not create that route or credential.
6. Validate sensor clocks, real particle readings, and actual backend receipt
   before relying on live alerts. The backend stores measurements older than
   five minutes, out-of-order samples, and clockless samples as history only.

To disable forwarding, write the same `note.update` with `enabled: false`.
Already queued host packets are retained. Disable the gateway in the backend
for an immediate server-side stop. Do not delete configuration as a substitute
for explicit disablement: transient Notecard errors preserve previously read
enablement so the host can buffer during an I2C interruption.

## Payload and delivery

Only successfully decoded telemetry from the allowed private channel is
eligible. Plaintext, PKI, MQTT-originated traffic, other ports, default short
keys, and the known public AES-128 key family are not forwarded. Remote packets
must carry the original forwarding-permission bit. Local telemetry uses the
gateway's explicit `config_ok_to_mqtt` setting. The sender node number, packet
ID, encrypted Data bytes, sensor timestamp, and received signal metadata are
preserved in a protobuf ServiceEnvelope naming the actual gateway.

The Notecard receives an untemplated request of this shape:

```json
{
  "req": "note.add",
  "file": "mesh.qos",
  "body": { "version": 1, "envelope": "<base64-ServiceEnvelope>" },
  "max": 256,
  "sync": true,
  "verify": true
}
```

This uses the body rather than the separate 256-byte binary payload field.
The Blues SDK handles I2C framing and protocol recovery; version 1.8.5 is pinned
only for the shared project build. SDK request/response debug output is disabled,
and raw requests, channel keys, and credentials are never logged by the bridge.
Requests use a two-second nominal SDK response timeout; SDK bus recovery can add
latency, so measure radio responsiveness on hardware under faults.

## Queue limits and failure handling

- Eight host slots under `/mfdq` each hold a fixed 408-byte checksummed record,
  including at most 384 envelope bytes. They share the nRF52 filesystem with
  Meshtastic preferences and the node database. Eight free slots do not promise
  sufficient free flash. New packets are refused when there is less than 8 KiB
  of preference reserve plus 1 KiB of staging headroom. Existing records can
  still drain. Real write errors stop forwarding for inspection.
- Each accepted packet is written to a temporary file, closed, read back,
  checked, and atomically renamed using LittleFS. A valid interrupted temporary
  record is recovered on restart. Corrupt/partial records stop forwarding;
  files are kept for inspection, never silently discarded.
- Mesh Fire builds also disable the upstream nRF52 automatic filesystem format
  on preference-save failure, preserving pending packets and existing state.
  A save failure still needs operator investigation; reserved space is not a
  guarantee that a damaged filesystem can save preferences.
- Queued duplicates with the same sender and packet ID are coalesced. A full
  queue refuses new packets and logs the cumulative rejection count. It does
  not evict old packets or claim that the rejected reading was stored.
- The host removes a record only after Notecard acknowledges `note.add`.
  Failed acknowledgements retain the record, with exponential retry delays of
  5–60 seconds. Local deletion failure retries deletion without sending again
  during the same boot. Power loss in the handoff/ack gap can resend a packet;
  delivery is at least once, and the backend performs deduplication.
- Notecard uses the secure `mesh.qos` outbound queue with a cap of 256 Notes.
  Cellular outages are handled there after handoff. If it refuses a new Note,
  the host retains that packet; if both queues fill, new incoming packets are
  rejected visibly. There is no unbounded or lossless buffering guarantee.
- A host acknowledgement means durable local handoff, not backend receipt.
  Notehub route failures need monitoring and rerouting per the backend runbook.
  Check installed Notecard firmware's durable-write behavior during the bench
  test; `verify` is specifically relevant if templates are used, while this
  transport requires an untemplated file and stops on a template response.
  Repair that configuration and restart the gateway before retrying; the
  template-fault latch is not automatically cleared.
- `/mfdq/identity` binds the spool to the Notecard UID, ProductUID, and local
  Meshtastic node ID. A different binding or filesystem fault stops forwarding
  without deleting data. Before replacing a Notecard, changing project, or
  resetting node identity, drain and back up the queue and plan explicit
  maintenance. Do not erase the filesystem to clear an unexplained fault.
- No packet is accepted by the bridge before valid provisioning has been read
  after boot. During an I2C outage after activation, cached enablement permits
  bounded host buffering; without a reachable Notecard at boot, pending files
  survive but new packets are not accepted until provisioning can be checked.

One Note is sent per telemetry packet, not per metric. Device, environment,
and particle telemetry are separate packets. Immediate sync prioritizes latency
but may increase power use and connection overhead. Measure Notehub event cost,
cellular traffic, queue growth, and power at the intended fleet size before
rollout. Backend deduplication does not eliminate provider event charges.

## Verification

```sh
pio run -e mesh-fire-sensor
pio test -d test/test_mfd_notecard -e mfd-notecard-test
pio test -d test/test_mesh_fire_defaults -e mesh-fire-defaults-test
```

Host tests cover channel/permission filtering, exact protobuf packet retention,
FIFO recovery, overflow without eviction, write failure, retry and deletion
acknowledgement, corrupt records, and the version 1 Note body. Setting
`MFD_NOTE_FIXTURE=/tmp/mfd-note-fixture.json` while running the new suite exports
a synthetic encrypted request for backend interoperability checks.

The C++ fixture was accepted through the backend's authenticated Notehub route,
stored as three PM readings (including zero), and deduplicated on repeat. The
RAK4631 image builds; defaults tests still pass. These checks do not use physical
Notecard hardware or the cloud and do not establish field readiness.

The remaining bench test must cover PMSA003I warm-up and readings, live LoRa
reception, Notecard queueing, Notehub routing, matching SQLite values, absent
network, full queues, gateway/Notecard power interruption, interrupted flash
writes, recovery, duplicate delivery, clock quality, and measured power use.
No device was flashed, rebooted, reset, or reconfigured for this source change.

Provider references:

- [Notecard hub configuration](https://dev.blues.io/api-reference/notecard-api/hub-requests/)
- [Local database Notes and outbound queues](https://dev.blues.io/api-reference/notecard-api/note-requests/)
- [Blues Arduino SDK](https://github.com/blues/note-arduino/tree/v1.8.5)
