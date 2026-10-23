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

void test_stock_sensor_name_is_replaced_without_changing_identity()
{
    meshtastic_User user = meshtastic_User_init_zero;
    std::strcpy(user.id, "!7d258b79");
    std::strcpy(user.long_name, "Meshtastic 8b79");
    std::strcpy(user.short_name, "8b79");
    user.role = meshtastic_Config_DeviceConfig_Role_SENSOR;
    user.is_licensed = true;
    user.has_is_unmessagable = true;
    user.is_unmessagable = true;
    user.public_key.size = 32;
    user.public_key.bytes[0] = 42;
    const auto original = user;

    meshFire::installDefaultOwnerName(user, 0x7d258b79);

    TEST_ASSERT_EQUAL_STRING("MFD Sensor 8b79", user.long_name);
    TEST_ASSERT_EQUAL_STRING("MFDS", user.short_name);
    std::memcpy(user.long_name, original.long_name, sizeof(user.long_name));
    std::memcpy(user.short_name, original.short_name, sizeof(user.short_name));
    TEST_ASSERT_EQUAL_MEMORY(&original, &user, sizeof(user));
}

void test_stock_client_gets_generic_name_with_zero_padded_id()
{
    meshtastic_User user = meshtastic_User_init_zero;
    std::strcpy(user.long_name, "Meshtastic 0001");
    std::strcpy(user.short_name, "0001");
    user.role = meshtastic_Config_DeviceConfig_Role_CLIENT;

    meshFire::installDefaultOwnerName(user, 0x12340001);

    TEST_ASSERT_EQUAL_STRING("MFD Node 0001", user.long_name);
    TEST_ASSERT_EQUAL_STRING("MFDN", user.short_name);
}

void test_custom_long_or_short_name_is_preserved()
{
    meshtastic_User user = meshtastic_User_init_zero;
    std::strcpy(user.long_name, "Lookout");
    std::strcpy(user.short_name, "2e53");
    const auto customLong = user;

    meshFire::installDefaultOwnerName(user, 0x752d2e53);
    TEST_ASSERT_EQUAL_MEMORY(&customLong, &user, sizeof(user));

    std::strcpy(user.long_name, "Meshtastic 2e53");
    std::strcpy(user.short_name, "TEST");
    const auto customShort = user;

    meshFire::installDefaultOwnerName(user, 0x752d2e53);
    TEST_ASSERT_EQUAL_MEMORY(&customShort, &user, sizeof(user));
}

void test_test_node_name_survives_role_changes()
{
    meshtastic_User user = meshtastic_User_init_zero;
    std::strcpy(user.long_name, "MFD Test 2e53");
    std::strcpy(user.short_name, "MFDT");
    user.role = meshtastic_Config_DeviceConfig_Role_SENSOR;
    const auto original = user;

    meshFire::installDefaultOwnerName(user, 0x752d2e53);

    TEST_ASSERT_EQUAL_MEMORY(&original, &user, sizeof(user));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_unprovisioned_sensor_cannot_transmit);
    RUN_TEST(test_gateway_defaults_have_no_public_broker_or_credentials);
    RUN_TEST(test_sensor_and_device_reports_are_enabled_in_metric_units);
    RUN_TEST(test_stock_sensor_name_is_replaced_without_changing_identity);
    RUN_TEST(test_stock_client_gets_generic_name_with_zero_padded_id);
    RUN_TEST(test_custom_long_or_short_name_is_preserved);
    RUN_TEST(test_test_node_name_survives_role_changes);
    return UNITY_END();
}
