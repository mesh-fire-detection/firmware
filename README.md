# Mesh Fire Detection firmware

Firmware for [Mesh Fire Detection](https://meshfiredetection.org), based on
[meshtastic/firmware](https://github.com/meshtastic/firmware). We retain Meshtastic's
LoRa mesh protocol and sensor drivers, and add configuration for our sensor nodes
and backend. Our current base is Meshtastic `v2.7.26.54e0d8d`.

## Our configuration

| Setting       | Mesh Fire Detection default                                                 |
| ------------- | --------------------------------------------------------------------------- |
| Build profile | `mesh-fire-sensor`, based on RAK4631                                        |
| Deployment    | Washington, USA; LoRa region `US`                                           |
| Telemetry     | Device, environment, and particle measurements enabled; 15-minute intervals |
| MQTT server   | `mqtt.meshfiredetection.org:1884`                                           |
| MQTT root     | `msh/US`; encrypted protobuf packets                                        |
| Provisioning  | Radio transmission and MQTT connection disabled until configured            |
| Node name     | `MFD Sensor <ID suffix>`; Bluetooth `MFDS_<MAC suffix>`                     |

Sensor nodes send readings over LoRa to a gateway. A gateway forwards packets to
our server; sensor nodes do not need their own internet connection. Private channel
keys, gateway credentials, and node registration are provisioned separately and
are not stored in this repository.

We maintain one codebase and one image for RAK4631 nodes. Roles and enabled modules
are selected through device configuration. The initial role is SENSOR; the
Notecard transport is included in this shared build and stays disabled until
the Notecard is explicitly provisioned. See [gateway setup and recovery](NOTECARD_GATEWAY.md).

| Module                        | Current status                                                                       |
| ----------------------------- | ------------------------------------------------------------------------------------ |
| RAK1906 / Bosch BME680        | Existing driver enabled; measurements verified on the USB node's installed firmware  |
| RAK12039 / Plantower PMSA003I | Existing driver enabled; real hardware test pending                                  |
| RAK13102 / Blues Notecard     | Transport and durable handoff queue implemented; real cellular hardware test pending |

Our build `2.7.26.7d798c3` has been flashed and verified over USB on the RAK4631
sensor node and the Mini comparison node. BME680 readings work on our build;
delivery through our gateway and broker remains unverified. The naming change
has passed compilation and seven host tests; it is included in the next image.

Stock Meshtastic names are replaced automatically when our image starts. Custom
names survive upgrades. A comparison node is named `MFD Test <ID suffix>` with
short name `MFDT`, producing a Bluetooth name such as `MFDT_2e53`.

## Build

```sh
git submodule update --init --recursive
pio run -e mesh-fire-sensor
pio test -d test/test_mesh_fire_defaults -e mesh-fire-defaults-test
```

See [project configuration, hardware evidence, and provisioning steps](MESH_FIRE_DETECTION.md)
before flashing or deploying. Saved node settings are preserved during firmware
upgrades; project defaults apply when a default configuration is created.

## Upstream Meshtastic project

The project information below describes upstream Meshtastic. Its release badges,
CI results, and statistics refer to the upstream project.

<div align="center" markdown="1">

<img src=".github/meshtastic_logo.png" alt="Meshtastic Logo" width="80"/>
<h1>Meshtastic Firmware</h1>

![GitHub release downloads](https://img.shields.io/github/downloads/meshtastic/firmware/total)
[![CI](https://img.shields.io/github/actions/workflow/status/meshtastic/firmware/main_matrix.yml?branch=master&label=actions&logo=github&color=yellow)](https://github.com/meshtastic/firmware/actions/workflows/ci.yml)
[![CLA assistant](https://cla-assistant.io/readme/badge/meshtastic/firmware)](https://cla-assistant.io/meshtastic/firmware)
[![Fiscal Contributors](https://opencollective.com/meshtastic/tiers/badge.svg?label=Fiscal%20Contributors&color=deeppink)](https://opencollective.com/meshtastic/)
[![Vercel](https://img.shields.io/static/v1?label=Powered%20by&message=Vercel&style=flat&logo=vercel&color=000000)](https://vercel.com?utm_source=meshtastic&utm_campaign=oss)

<a href="https://trendshift.io/repositories/5524" target="_blank"><img src="https://trendshift.io/api/badge/repositories/5524" alt="meshtastic%2Ffirmware | Trendshift" style="width: 250px; height: 55px;" width="250" height="55"/></a>

</div>

</div>

<div align="center">
	<a href="https://meshtastic.org">Website</a>
	-
	<a href="https://meshtastic.org/docs/">Documentation</a>
</div>

### Upstream overview

Meshtastic is an open-source LoRa mesh networking project designed for long-range, low-power communication without relying on internet or cellular infrastructure. Its upstream firmware supports various hardware platforms, including ESP32, nRF52, RP2040/RP2350, and Linux-based devices.

Meshtastic enables text messaging, location sharing, and telemetry over a decentralized mesh network, making it ideal for outdoor adventures, emergency preparedness, and remote operations.

### Get Started

- 🔧 **[Building Instructions](https://meshtastic.org/docs/development/firmware/build)** – Learn how to compile the firmware from source.
- ⚡ **[Flashing Instructions](https://meshtastic.org/docs/getting-started/flashing-firmware/)** – Install or update the firmware on your device.

Join our community and help improve Meshtastic! 🚀

### Upstream stats

![Alt](https://repobeats.axiom.co/api/embed/8025e56c482ec63541593cc5bd322c19d5c0bdcf.svg "Repobeats analytics image")
