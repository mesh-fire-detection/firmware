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
not produce fabricated zero readings.

The API address is `https://api.meshfiredetection.org`. Sensor nodes currently
send mesh packets through a gateway; they do not call the HTTP API. The MQTT root
becomes `msh/US/2/e/<channel>/<gateway>` for encrypted protobuf uplink. The backend
subscribes to `msh/US/2/e/#`.

The broker port follows the existing backend deployment: 1884 with MQTT TLS
disabled. Channel payload encryption is separate from transport TLS. Gateway
transport and TLS must be tested on the selected gateway hardware before field
deployment; a future TLS endpoint needs matching firmware and broker settings.

## Hardware support and evidence

| Module                            | Firmware support                                     | Hardware verification                             |
| --------------------------------- | ---------------------------------------------------- | ------------------------------------------------- |
| RAK1906 / Bosch BME680            | Existing I2C driver; environmental telemetry enabled | Read real measurements from the connected RAK4631 |
| RAK12039 / Plantower PMSA003I     | Existing I2C driver; particle telemetry enabled      | Module not connected; pending bench test          |
| RAK13102 / Blues Notecard carrier | No upstream Notecard transport integration           | Module not connected; transport decision pending  |

On October 2, 2026, the USB node identified itself as `!7d258b79`, RAK4631,
Meshtastic `2.7.15.567b8ea`. After enabling only environmental measurement on that
installed firmware, it reported approximately 24.86 °C, 41.79% relative humidity,
and 954.35 hPa. Its radio region remained UNSET. This confirms the attached
BME680 works on its installed firmware; it does not verify this new build or
delivery to our broker.

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

## Gateway decision still required

The backend specification currently uses a gateway with an LTE router and
Meshtastic's native MQTT uplink to our own Mosquitto broker. That remains the
implemented backend transport.

RAK13102 is a carrier for a Blues Notecard. The Notecard connects to Notehub,
which then routes events to an MQTT broker or HTTPS service. It does not expose
an ordinary cellular TCP connection for Meshtastic's MQTT client. See the
[Blues security model](https://dev.blues.io/datasheets/application-notes/blues-security-reliability-and-governance/)
and [Notehub MQTT routing](https://dev.blues.io/guides-and-tutorials/routing-data-to-cloud/mqtt/).

If we choose RAK13102, the next change needs a Notecard queue/transport in this
firmware and an authenticated ingestion adapter for Notehub events. A Notehub
JSON event is not a binary Meshtastic ServiceEnvelope accepted by the current
backend. Record the selected transport in the backend SPEC and ARCHITECTURE in
the same change. Until that choice is made, this build is a sensor-node build;
it does not claim a working cellular gateway.

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

Flashing, radio provisioning, and the complete cellular path remain unverified.
No firmware update, private key installation, or region change has been performed
on the USB node by building this fork.
