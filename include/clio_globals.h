#ifndef CLIO_GLOBALS_H
#define CLIO_GLOBALS_H
// includes
#include "clio_firmware.h"
#include "clio_structs.h"
#include "secrets.h"
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <RTClib.h>
#include <WiFi.h>
#include <SPI.h>

// pinout
#define NETWORK_LED 2
#define RADAR 26
#define AP_BTN 19
#define MANUAL_BTN 34
#define ROOM_TEMP_PIN 32

// Classes
extern WiFiClientSecure mqttWiFiClient;
extern PubSubClient mqtt_client;
extern RTC_DS3231 DS3231_RTC;

// Enum vars
extern SysStateEnum SysState;
extern SysFaultEnum SysFaultState;
extern SysModeEnum SysMode;
extern SysModeEnum peersMode;
extern FlowFlag sleep_flag;
extern EspNowState espnow_connection_state;

// ** GLOBAL STRUCTS **
extern system_config_struct system_config;
extern controller_data_struct controller_data;
extern monitor_data_struct monitor_data;
extern system_alarms_struct system_alarms;

// global variables
extern const char Week_days[7][12];
extern char AP_SSID[33];           //
extern char esid[33];              // Max SSID length is 32
extern char epass[65];             // Max WPA2 passphrase length is 64
extern char hub_device_serial[13]; // MAC address without colons, 12 chars + null
extern char last_ntp_update[10];   // HH:MM:SS
extern volatile float room_temperature;
extern volatile LedAnimationStyle ntw_led_style;
extern bool controller_peer_online;
extern bool monitor_peer_online;
extern int activeSetpoint; // default setpoint
extern bool fault_restart_attempt_flag;
extern bool daySleepControl; // Variable de activación del modo sleep por cada día.
extern bool radarState;
extern int system_hourmeter;

// ## FUNCTIONS ##

//- setup
void clio_fsdata_setup();

//- helpers
bool is_valid_mac_str(const char *str);
void parse_mac_address(const char *str, char sep, byte *bytes, int base);
void network_led_animation(LedAnimationStyle animation_style);
void format_device_mac(const uint8_t *mac_addr, char *out_str);
void format_device_serial(const uint8_t *mac_addr, char *out_str);
void format_ap_ssid(const uint8_t *mac_addr, char *out_str);
void clio_sntp_setup();
bool is_time_synchronized();
esp_err_t clio_rtc_setup();

//-littleFS
char *load_data_from_fs(const char *target_file);
void save_wifi_data_in_fs();
void save_operation_state_in_fs();
void save_config_in_fs();
void save_operation_mode_in_fs();
esp_err_t save_data_in_fs(const char *data_to_save, const char *target_file);
esp_err_t clio_spiffs_setup();

//-temp sensor
void clio_temp_sensors_setup();
void clio_temp_sensors_loop();

//-wifi
void clio_wifi_loop();
void clio_wifi_setup();

//-espnow
void clio_espnow_loop();
esp_err_t clio_espnow_cnf();

//-mqtt
esp_err_t clio_mqtt_setup();
esp_err_t publish_compressor_startup();
esp_err_t publish_new_incident(incident_struct *incident);
void clio_mqtt_loop();

//-hourmeter
void time_counter_loop();

//-handlers
esp_err_t handle_peerlist_update(JsonDocument &received_json);
esp_err_t handle_system_settings_from_broker(JsonDocument &doc);
esp_err_t handle_system_config_from_broker(JsonDocument &json);
esp_err_t handle_cmd_from_broker(JsonDocument &json);

//-server
void startProvisioning();

#endif // CLIO_GLOBALS_H