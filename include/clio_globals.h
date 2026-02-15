#ifndef CLIO_GLOBALS_H
#define CLIO_GLOBALS_H
//includes
#include "clio_structs.h"
#include "secrets.h"
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <RTClib.h>
#include <WiFi.h>
#include <SPI.h>

//firmware version
#define FIRMWARE_VERSION "1.0.0"

// pinout
#define NETWORK_LED 2
#define RADAR 26
#define AP_BTN 19
#define MANUAL_BTN 34
#define CASE_TEMP 32

// Classes
extern WiFiClientSecure mqttWiFiClient;
extern PubSubClient mqtt_client;
extern RTC_DS3231 DS3231_RTC;

// Enum vars
extern SysStateEnum SysState;
extern SysStateEnum PrevSysState;
extern SysStatusFlag SysFaultState; // default status
extern SysStateEnum SysStateBuffer;
extern SysModeEnum SysMode;
extern SysModeEnum peersMode;
extern FlowFlag sleep_flag;
extern EspNowState espnow_connection_state;

// ** GLOBAL STRUCTS **
extern system_config_struct system_config;
extern controller_data_struct controller_data;
extern monitor_data_struct monitor_data;

// global variables
// TODO: Declare volatile type for global variables that are accesed from both cores.
extern const char Week_days[7][12];
extern char AP_SSID[33]; //
extern char esid[33];  // Max SSID length is 32
extern char epass[65]; // Max WPA2 passphrase length is 64
extern char hub_device_serial[13]; // MAC address without colons, 12 chars + null
extern char last_ntp_update[10]; // HH:MM:SS
extern volatile float case_pcb_temperature;
extern volatile LedAnimationStyle ntw_led_style;
extern bool controller_peer_online;
extern bool monitor_peer_online;
extern int activeSetpoint; // default setpoint
extern bool fault_reset_flag;
extern bool daySleepControl; // Variable de activación del modo sleep por cada día.
extern bool radarState;
extern bool postVariablesToBroker;
extern bool postMqttStateUpdate;
extern int system_hourmeter;

// ## FUNCTIONS ##

//- setup
void clio_fsdata_setup();

//- helpers
char *print_device_mac(const uint8_t *mac_addr);
char *print_device_serial(const uint8_t *mac_addr);
void parse_mac_address(const char* str, char sep, byte* bytes, int maxBytes, int base);
void network_led_animation(LedAnimationStyle animation_style);
void clio_sntp_setup();
esp_err_t clio_rtc_setup();

//-spiffs
char* load_data_from_fs(const char *target_file);
void save_data_in_fs(const char* data_to_save, const char* target_file);
esp_err_t clio_spiffs_setup();

//-temp sensor
void clio_case_tempsensor_setup();
void update_case_temperature();

//-wifi
void save_wifi_data_in_fs();
void clio_wifi_loop();
void clio_wifi_setup();

//-espnow
void clio_espnow_loop();
esp_err_t clio_espnow_cnf();

//-mqtt
esp_err_t clio_mqtt_setup();
esp_err_t post_incident_to_broker(incident_struct *args);
void clio_mqtt_loop();

//-hourmeter
void update_time_counter();

//-handlers
void handle_peerlist_update(const char *received_data);
void handle_temp_sp_from_broker(const char *json);
void handle_system_settings_from_broker(const char* json);
void handle_system_config_from_broker(const char* json);
void handle_op_state_from_broker(const char* json);

//-server
void startProvisioning();

#endif // CLIO_GLOBALS_H