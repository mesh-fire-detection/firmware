# Mesh Fire Detection firmware

This checkout is a local fork of [Meshtastic firmware](https://github.com/meshtastic/firmware),
based on release `v2.7.26.54e0d8d`, commit `54e0d8d0ab2ff56b3a9ce967e53f79e49af560fb`.
Our remote is `origin`, pointing to [mesh-fire-detection/firmware](https://github.com/mesh-fire-detection/firmware).
The upstream remote is named `upstream`; local work is on `main`.
Submodules retain their upstream release revisions.

## Build and validation

Install PlatformIO Core, initialize submodules, and run from this directory:

```sh
git submodule update --init --recursive
pio run -e mesh-fire-sensor
pio test -d test/test_mesh_fire_defaults -e mesh-fire-defaults-test
```

The first build downloads the upstream nRF52840 toolchain, framework, and sensor
libraries. The host tests check the initial radio, telemetry, and broker defaults
without opening a serial port. The sensor environment inherits `rak4631`, including
its pin assignments and existing BME680 and PMSA003I drivers; no new sensor library
is required. Other upstream build environments retain their original defaults.

Build outputs are under `.pio/build/mesh-fire-sensor/`. Building does not flash the
USB node. Review the build and provision a node before enabling radio transmission.

Verified locally on October 2, 2026: the sensor build succeeded and produced UF2
and DFU ZIP artifacts; all three host tests passed. The build used 105,260 bytes
of RAM (42.3%) and 759,260 bytes of flash (93.1%). Additional gateway code will
need its own build and size check.

## Releases

Pushing a tag `mfd-v<version>` (for example `mfd-v2.7.26.7d798c3`) runs
`.github/workflows/mfd_release.yml`. It builds `mesh-fire-sensor`, repackages the
output with `bin/mfd_package_release.sh`, attests build provenance, and publishes
a GitHub release with:

| File                                 | Purpose                                                     |
| ------------------------------------ | ----------------------------------------------------------- |
| `mfd-rak4631-sensor-<version>.zip`   | Upload to the Meshtastic Web Flasher; contains only the UF2 |
| `firmware-rak4631-<version>.uf2`     | Copy onto the RAK4631 bootloader drive                      |
| `firmware-rak4631-<version>-ota.zip` | Nordic DFU package                                          |
| `firmware-rak4631-<version>.mt.json` | Per-board manifest in the format the web flasher reads      |
| `firmware-<version>.json`            | Release manifest listing `rak4631` as the only target       |
| `SHA256SUMS`                         | Checksums of every file above                               |

Files carry the `rak4631` board name rather than `mesh-fire-sensor` because the
web flasher looks them up as `firmware-<board>-<version>.<ext>`. The
[Mesh Fire Detection flasher](https://github.com/mesh-fire-detection/flasher)
serves these files; bump `types/mfdFirmware.json` there. In the web
repository, set `MFD_RELEASE` in `src/core/nearby/firmware.ts` to the new
release and keep the previous commit in `MFD_BUILDS`.

## Initial configuration

RAK4631 nodes share the same firmware image. Their deployment role and enabled
modules are configured per device, rather than compiled into separate images.
The initial role is SENSOR. Gateway credentials are assigned only to gateways;
the Notecard transport is still pending. Other processor or board types require
their own build from the same source tree.

| Setting                                                   | Default                                     |
| --------------------------------------------------------- | ------------------------------------------- |
| Hardware                                                  | RAK4631, nRF52840 / SX1262                  |
| Device role                                               | SENSOR                                      |
| Physical deployment                                       | Washington, USA                             |
| LoRa region                                               | US                                          |
| LoRa modem preset                                         | LONG_FAST                                   |
| Radio transmission                                        | Disabled until private-channel provisioning |
| Device telemetry                                          | Enabled; 900 seconds                        |
| Environmental telemetry                                   | Enabled; 900 seconds                        |
| Particle telemetry                                        | Enabled; 900 seconds                        |
| Temperature display                                       | Celsius                                     |
| Permission to forward telemetry to MQTT                   | Enabled                                     |
| Received mesh packets originating from MQTT               | Ignored                                     |
| MQTT broker                                               | `mqtt.meshfiredetection.org:1884`           |
| MQTT root                                                 | `msh/US`                                    |
| MQTT payload encryption                                   | Enabled                                     |
| MQTT connection, JSON, client proxy, public map reporting | Disabled                                    |
| MQTT credentials                                          | Empty; assigned to each gateway separately  |

These settings are applied only when the firmware creates a default local or
module configuration, including after a configuration reset. Valid persisted
configuration is preserved during upgrades. Updating firmware on the currently
connected node therefore does not automatically change its saved region, role,
channel, or reporting intervals. Do not reset it simply to apply defaults.

Reporting uses Meshtastic's scheduling and airtime checks, so 900 seconds is a
configured interval rather than a guaranteed delivery deadline. Device,
environment, and particle readings are separate packets. A missing sensor does
not produce fabricated zero readings. While the default public channel is still
present, Meshtastic raises these intervals to a minimum of 1,800 seconds at boot.
Both USB nodes currently have that 30-minute minimum; private-channel provisioning
is still pending.

The API address is `https://api.meshfiredetection.org`. Sensor nodes currently
send mesh packets through a gateway; they do not call the HTTP API. The MQTT root
becomes `msh/US/2/e/<channel>/<gateway>` for encrypted protobuf uplink. The backend
subscribes to `msh/US/2/e/#`.

The broker port follows the existing backend deployment: 1884 with MQTT TLS
disabled. Channel payload encryption is separate from transport TLS. Gateway
transport and TLS must be tested on the selected gateway hardware before field
deployment; a future TLS endpoint needs matching firmware and broker settings.

## Node names during flashing and provisioning

On startup, our build replaces the stock pair `Meshtastic <last four ID digits>` /
`<last four ID digits>` with `MFD Sensor <suffix>` / `MFDS` for SENSOR devices,
or `MFD Node <suffix>` / `MFDN` for other roles. It saves the name in the owner
configuration and node database. A custom long or short name prevents this
replacement, including on subsequent upgrades. Identity, keys, and other owner
fields are preserved.

When preparing a comparison device, set its owner names to `MFD Test <suffix>` /
`MFDT` after selecting the CLIENT profile. The purpose in the name is independent
of the Meshtastic protocol role; the same RAK4631 image serves both devices.
Restart after changing names so Bluetooth advertises the new short name.

| Device                         | Long name         | Short name | Bluetooth name |
| ------------------------------ | ----------------- | ---------- | -------------- |
| RAK4631 + RAK1906, `!7d258b79` | `MFD Sensor 8b79` | `MFDS`     | `MFDS_8b79`    |
| Mini comparison, `!752d2e53`   | `MFD Test 2e53`   | `MFDT`     | `MFDT_2e53`    |

Bluetooth adds the last two MAC bytes to the short name. The long-name suffix
uses the node ID; backend registration continues to use the full node ID.
On October 3, 2026, both names were applied over USB and verified after restart;
their owner fields apart from the two names, local/module settings, and channels
were unchanged.
The automatic startup behavior is in the next build, with seven passing host
tests covering defaults, custom-name preservation, and identity preservation.

## Hardware support and evidence

| Module                            | Firmware support                                        | Hardware verification                                        |
| --------------------------------- | ------------------------------------------------------- | ------------------------------------------------------------ |
| RAK1906 / Bosch BME680            | Existing I2C driver; environmental telemetry enabled    | Read real measurements from the connected RAK4631            |
| RAK12039 / Plantower PMSA003I     | Existing I2C driver; particle telemetry enabled         | Module not connected; pending bench test                     |
| RAK13102 / Blues Notecard carrier | Project transport and durable handoff queue implemented | No physical Notecard/cellular test performed for this change |

On October 2, 2026, the USB node identified itself as `!7d258b79`, RAK4631,
Meshtastic `2.7.15.567b8ea`. After enabling only environmental measurement on that
installed firmware, it reported approximately 24.86 °C, 41.79% relative humidity,
and 954.35 hPa. Its radio region remained UNSET. This confirms the attached
BME680 works on its installed firmware; it does not verify this new build or
delivery to our broker.

Later on October 2, 2026, the same sensor node was flashed with our
`2.7.26.7d798c3` build. USB verification confirmed role SENSOR, region US, radio
transmission disabled, and BME680 readings of approximately 24.23 °C, 41.30%
relative humidity, and 954.85 hPa. The Mini node was flashed on October 3 with
the same image and configured as CLIENT with environmental and particle
measurements disabled. Its existing PKI keys and channels were preserved.
The Mini has a broken reset button; local USB administration successfully
entered its UF2 bootloader without pressing the button.

BME680 is detected at I2C address 0x76 or 0x77. The upstream build includes Bosch
BSEC. Its initial IAQ value of 50 had accuracy 0 in the device log, so it must not
be treated as a calibrated reading. BME680 IAQ is different from PM-based US EPA
AQI; neither value by itself establishes that a fire is present.

PMSA003I uses I2C address 0x12 and a 30-second warm-up in the upstream driver.
The RAK4631 variant controls its enable signal through `PMSA003I_ENABLE_PIN`.
RAK12039 has an onboard 5 V boost converter and an active sensor current below
100 mA according to its [datasheet](https://docs.rakwireless.com/product-categories/wisblock/rak12039/datasheet/).
Confirm readings, warm-up, standby behavior, and the power budget on real hardware.

RAK12039 and RAK13102 both occupy a WisBlock IO slot. Check the baseboard's IO
slots and mechanical clearance before trying to place both on one node. RAK1906
uses a sensor slot.

## Cellular gateway transport

The shared `mesh-fire-sensor` image now includes an opt-in Notecard gateway
transport for RAK4631. Its contract matches the authenticated Notehub HTTPS
adapter in [backend PR #27](https://github.com/mesh-fire-detection/backend/pull/27).
The existing native MQTT transport remains available for gateways with a normal
internet connection. Neither path is enabled automatically by this change.

RAK13102 is a carrier for a Blues Notecard. The Notecard connects to Notehub,
which then routes events to an MQTT broker or HTTPS service. It does not expose
an ordinary cellular TCP connection for Meshtastic's MQTT client. See the
[Blues security model](https://dev.blues.io/datasheets/application-notes/blues-security-reliability-and-governance/)
and [Notehub MQTT routing](https://dev.blues.io/guides-and-tutorials/routing-data-to-cloud/mqtt/).

The transport preserves the original encrypted ServiceEnvelope and wraps its
base64 bytes in the `body` of an untemplated `mesh.qos` Note. It does not attempt
to use the Notecard as a TCP modem. A locally provisioned `mfd-config.dbx` record
must enable the gateway and name the expected Notehub ProductUID. Private-channel
uplink and per-packet forwarding permission are required; channel MQTT downlink
must be off. The firmware never provisions a public channel, enables radio
transmission, resets a device, or stores the backend bearer credential.

Eight host flash slots retain pending packets before Notecard handoff. Files
are checksummed and committed through LittleFS rename; only a successful
Notecard acknowledgement permits deletion. Full queues refuse new packets and
log the rejection count. Corruption or a different Notecard/project/node binding
stops forwarding and preserves the files. There is no automatic pruning.
Mesh Fire builds preserve an 8 KiB preference reserve plus staging headroom
and disable the upstream automatic filesystem format after a preference-save
failure, so a failed save cannot silently erase the handoff queue.
The Notecard queue is separately bounded at 256 Notes. See
[provisioning, limits, recovery, and validation](NOTECARD_GATEWAY.md).

The RAK4631 image compiles and host tests cover filtering, protobuf preservation,
queue recovery, overflow, retries, and corruption. A C++ fixture was also
accepted by the backend Notehub route and stored as three particle readings,
including zero; repeated delivery was deduplicated. This is software evidence,
not a physical cellular deployment test. No device was flashed for this change.

The [RAK13102 quick-start guide](https://docs.rakwireless.com/product-categories/wisblock/rak13102/quickstart/)
requires a suitable battery or regulated 5 V external supply for reliable
operation. USB power alone is not sufficient evidence of a working gateway.

RAK sells the carrier with or without a Notecard. A cellular Notecard includes
an embedded SIM and 500 MB of prepaid traffic valid for up to 10 years. This is
a total data allowance. Notehub Essential includes 5,000 free incoming application
events per billing account per month; extra events cost $0.00075 each at the
[published pricing](https://blues.com/pricing/) checked on October 2, 2026.
Budget event count for the entire mesh forwarded by a gateway. Sending several
nodes' readings together can reduce events, subject to Notecard buffer limits
and the chosen delivery-latency requirements.

## Provisioning and deployment work still required

1. Install the same private 32-byte channel key on sensor nodes and gateways and
   configure its base64 value as `MESH_CHANNEL_KEY` on the backend. Keep it out
   of source, build flags, command history, and device logs. The upstream factory
   primary channel remains public until replaced during provisioning.
2. Set matching primary-channel name, modem preset, and frequency slot on all
   nodes. Disable secondary public channels and MQTT downlink. Set uplink on the
   gateway's private channel only.
3. Register sender node IDs and gateway node IDs on the backend. Assign each
   MQTT gateway its own broker credentials and topic ACL; sensor nodes need no
   broker login. Configure deployment names and coordinates.
4. Apply the saved US region, role, metric telemetry settings, and intervals to
   existing nodes explicitly. Enable radio transmission after private-channel
   configuration is verified. Provision a gateway using the selected transport.
5. Test encrypted ServiceEnvelope delivery and backend readings with real sensor
   and gateway hardware, including duplicate packets and loss of connectivity.
   Verify battery and sensor failures, PM warm-up, and BME680 calibration state.
6. Measure the power budget with all intended modules installed. Keep deep sleep
   disabled until both sensor sampling and gateway delivery have been tested.
7. Review and explicitly authorize flashing the connected node. Preserve its
   configuration and identity; a factory reset is not part of an upgrade.

Flashing and local sensor measurements have been verified on the devices above.
Private-channel provisioning, physical gateway delivery, and the complete cellular path
remain unverified. Radio transmission stays disabled on both nodes until their
private-channel configuration is ready.

## Audit follow-ups

See the [October 4, 2026 firmware audit](MFD_AUDIT_2026-10-04.md) for provisioning,
MQTT delivery, hardware assumptions, and end-to-end validation follow-ups.
The audit does not establish hardware verification or authorize flashing a device.
