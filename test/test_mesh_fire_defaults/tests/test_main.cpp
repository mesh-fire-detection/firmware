#include "mesh/MeshFireDefaults.h"
#include <unity.h>

void setUp() {}
void tearDown() {}

void test_unprovisioned_sensor_cannot_transmit()
{
    meshtastic_LocalConfig config = meshtastic_LocalConfig_init_zero;
    config.lora.tx_enabled = true;
    meshFire::installDefaultConfig(config);

    TEST_ASSERT_FALSE(config.lora.tx_enabled);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_RegionCode_US, config.lora.region);
    TEST_ASSERT_EQUAL(meshtastic_Config_DeviceConfig_Role_SENSOR, config.device.role);
    TEST_ASSERT_TRUE(config.lora.use_preset);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST, config.lora.modem_preset);
    TEST_ASSERT_TRUE(config.lora.config_ok_to_mqtt);
    TEST_ASSERT_TRUE(config.lora.ignore_mqtt);
}

void test_gateway_defaults_have_no_public_broker_or_credentials()
{
    meshtastic_LocalModuleConfig config = meshtastic_LocalModuleConfig_init_zero;
    std::strcpy(config.mqtt.address, "mqtt.meshtastic.org");
    std::strcpy(config.mqtt.username, "old-user");
    std::strcpy(config.mqtt.password, "old-password");
    config.mqtt.enabled = true;
    config.mqtt.json_enabled = true;
    config.mqtt.proxy_to_client_enabled = true;
    config.mqtt.map_reporting_enabled = true;
    meshFire::installDefaultModuleConfig(config);

    TEST_ASSERT_EQUAL_STRING("mqtt.meshfiredetection.org:1884", config.mqtt.address);
    TEST_ASSERT_EQUAL_STRING("msh/US", config.mqtt.root);
    TEST_ASSERT_EQUAL_STRING("", config.mqtt.username);
    TEST_ASSERT_EQUAL_STRING("", config.mqtt.password);
    TEST_ASSERT_FALSE(config.mqtt.enabled);
    TEST_ASSERT_FALSE(config.mqtt.json_enabled);
    TEST_ASSERT_FALSE(config.mqtt.proxy_to_client_enabled);
    TEST_ASSERT_FALSE(config.mqtt.map_reporting_enabled);
    TEST_ASSERT_FALSE(config.mqtt.tls_enabled);
    TEST_ASSERT_TRUE(config.mqtt.encryption_enabled);
}

void test_sensor_and_device_reports_are_enabled_in_metric_units()
{
    meshtastic_LocalModuleConfig config = meshtastic_LocalModuleConfig_init_zero;
    config.telemetry.environment_display_fahrenheit = true;
    meshFire::installDefaultModuleConfig(config);

    TEST_ASSERT_TRUE(config.telemetry.device_telemetry_enabled);
    TEST_ASSERT_TRUE(config.telemetry.environment_measurement_enabled);
    TEST_ASSERT_TRUE(config.telemetry.air_quality_enabled);
    TEST_ASSERT_FALSE(config.telemetry.environment_display_fahrenheit);
    TEST_ASSERT_EQUAL_UINT32(900, config.telemetry.device_update_interval);
    TEST_ASSERT_EQUAL_UINT32(900, config.telemetry.environment_update_interval);
    TEST_ASSERT_EQUAL_UINT32(900, config.telemetry.air_quality_interval);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_unprovisioned_sensor_cannot_transmit);
    RUN_TEST(test_gateway_defaults_have_no_public_broker_or_credentials);
    RUN_TEST(test_sensor_and_device_reports_are_enabled_in_metric_units);
    return UNITY_END();
}
