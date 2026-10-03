#pragma once

#include "mesh/generated/meshtastic/localonly.pb.h"
#include <cstring>

namespace meshFire
{

constexpr uint32_t telemetryIntervalSecs = 15 * 60;
constexpr char mqttAddress[] = "mqtt.meshfiredetection.org:1884";
constexpr char mqttRoot[] = "msh/US";

inline void installDefaultConfig(meshtastic_LocalConfig &localConfig)
{
    localConfig.device.role = meshtastic_Config_DeviceConfig_Role_SENSOR;
    localConfig.lora.region = meshtastic_Config_LoRaConfig_RegionCode_US;
    localConfig.lora.use_preset = true;
    localConfig.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    // Enable transmission only after provisioning the private channel key.
    localConfig.lora.tx_enabled = false;
    localConfig.lora.config_ok_to_mqtt = true;
    localConfig.lora.ignore_mqtt = true;
}

inline void installDefaultModuleConfig(meshtastic_LocalModuleConfig &localModuleConfig)
{
    auto &telemetry = localModuleConfig.telemetry;
    telemetry.device_telemetry_enabled = true;
    telemetry.device_update_interval = telemetryIntervalSecs;
    telemetry.environment_measurement_enabled = true;
    telemetry.environment_update_interval = telemetryIntervalSecs;
    telemetry.environment_display_fahrenheit = false;
    telemetry.air_quality_enabled = true;
    telemetry.air_quality_interval = telemetryIntervalSecs;

    auto &mqtt = localModuleConfig.mqtt;
    mqtt = {};
    static_assert(sizeof(mqttAddress) <= sizeof(mqtt.address));
    static_assert(sizeof(mqttRoot) <= sizeof(mqtt.root));
    std::memcpy(mqtt.address, mqttAddress, sizeof(mqttAddress));
    std::memcpy(mqtt.root, mqttRoot, sizeof(mqttRoot));
    mqtt.encryption_enabled = true;
}

} // namespace meshFire
