#include "clio_globals.h"
static const char *TAG = "CLIO-SETUP";

// Variables de configuracion
// Enum vars
SysStateEnum SysState = UNKN;
SysFaultEnum SysFaultState = STATUS_OK; // default status
SysModeEnum SysMode = AUTO_MODE;
SysModeEnum peersMode = FAN_MODE;
FlowFlag sleep_flag = FLAG_UNSET;
EspNowState espnow_connection_state = ESPNOW_IDLE;

WiFiClientSecure mqttWiFiClient;
PubSubClient mqtt_client(mqttWiFiClient);
RTC_DS3231 DS3231_RTC;

// GLOBAL VARIABLES
const char Week_days[7][12] = {"Domingo", "Lunes", "Martes", "Miercoles", "Jueves", "Viernes", "Sabado"};
char AP_SSID[33] = "";
char esid[33] = "";                   // Max SSID length is 32
char epass[65] = "";                  // Max WPA2 passphrase length is 64
char last_ntp_update[10] = "";        // HH:MM:SS
char hub_device_serial[13] = "";      // MAC address without colons, 12 chars + null
volatile float room_temperature = 22; // requested by both cores.
volatile LedAnimationStyle ntw_led_style = ALLWAYS_OFF;
int activeSetpoint = 24;  // default setpoint
int system_hourmeter = 0; // system on hourmeter
bool controller_peer_online = false;
bool monitor_peer_online = false;
bool fault_restart_attempt_flag = false;
bool daySleepControl = false; // Variable de activación del modo sleep por cada día.
bool radarState = false;

// structs
system_config_struct system_config;
controller_data_struct controller_data;
monitor_data_struct monitor_data;
system_alarms_struct system_alarms;

// functions
void load_operation_state_from_fs() //[OK] [OK]
{
  // operation state.
  ESP_LOGI(TAG, "-> loading System State from fs");
  const char *data_loaded = load_data_from_fs("/Estado.txt");
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, data_loaded);
  if (error)
  {
    ESP_LOGE(TAG, "[JSON.] Deserialization error %s", error.c_str());
    return;
  }

  const char *state_loaded = doc["sys_state"] | "off";
  const char *err_loaded = doc["err_state"] | "ok";

  if (strcmp(state_loaded, "on") == 0)
  {
    SysState = SYSTEM_ON;
    ESP_LOGI(TAG, "system is on..");
  }
  else if (strcmp(state_loaded, "off") == 0)
  {
    SysState = SYSTEM_OFF;
    ESP_LOGI(TAG, "system is off");
  }
  else if (strcmp(state_loaded, "sleep") == 0)
  {
    SysState = SYSTEM_SLEEP;
    ESP_LOGI(TAG, "system is in sleep mode.");
  }
  else if (strcmp(state_loaded, "error") == 0)
  {
    SysState = SYSTEM_ERROR;
    ESP_LOGI(TAG, "system is in error state.");
  }
  else
  {
    SysState = SYSTEM_OFF;
    ESP_LOGE(TAG, "Error: Bad value stored in /Estado.txt");
  }

  ESP_LOGI(TAG, "System State loaded from fs: %s", state_loaded);

  if (strcmp(err_loaded, "ok") == 0)
  {
    SysFaultState = STATUS_OK;
    ESP_LOGI(TAG, "system fault status is ok.");
  }
  else if (strcmp(err_loaded, "error") == 0)
  {
    SysFaultState = STATUS_ERROR;
    ESP_LOGI(TAG, "system fault status is error.");
  }
  else
  {
    SysFaultState = STATUS_OK;
    ESP_LOGE(TAG, "Error: Bad value stored in /Estado.txt");
  }

  ESP_LOGI(TAG, "System Fault Status loaded from fs: %s", err_loaded);

  return;
}

void load_operation_mode_from_fs() //[OK]
{
  ESP_LOGI(TAG, "-> loading operation mode from fs");
  const char *data_loaded = load_data_from_fs("/Modo.txt");
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, data_loaded);
  if (err)
  {
    ESP_LOGE(TAG, "[JSON.] Deserialization error %s", err.c_str());
    return;
  }

  static const char *ReadModo = doc["sys_mode"] | "fan";
  if (strcmp(ReadModo, "auto") == 0)
  {
    SysMode = AUTO_MODE;
  }
  else if (strcmp(ReadModo, "fan") == 0)
  {
    SysMode = FAN_MODE;
  }
  else if (strcmp(ReadModo, "cool") == 0)
  {
    SysMode = COOL_MODE;
  }

  ESP_LOGI(TAG, "System Mode loaded from fs: %s", ReadModo);
  return;
}

void load_system_config_from_fs() //[OK]
{
  const char *Settings = load_data_from_fs("/Settings.txt");
  // String input;
  JsonDocument cnf;
  DeserializationError error = deserializeJson(cnf, Settings);

  if (error)
  {
    ESP_LOGE(TAG, "Error loading config from filesystem.");
    ESP_LOGE(TAG, "%s", error.c_str());
    return;
  }

  system_config.sleep_control_en = cnf["sleep_control_en"] | false;
  system_config.room_temp_control_en = cnf["room_temp_control_en"] | false;
  system_config.comp_nominal_amp = cnf["comp_nominal_amp"] | 24;
  system_config.comp_amp_threshold = cnf["comp_amp_threshold"] | 32;
  system_config.discharge_max_temp = cnf["discharge_max_t"] | 107;
  system_config.liquid_max_temp = cnf["liquid_max_t"] | 60;
  system_config.vapor_line_min_temp = cnf["vapor_line_min_t"] | -5;
  system_config.max_recovery_attempts = cnf["max_recovery_attempts"] | 3;
  system_config.recovery_window = cnf["recovery_window"] | 120;
  system_config.user_setpoint = cnf["user_setpoint"] | 24;
  system_config.auto_setpoint = cnf["auto_setpoint"] | 28;
  system_config.auto_wait_time = cnf["auto_wait_time"] | 10;

  ESP_LOGI(TAG, "System Config loaded from fs.");
  return;
}

void load_wifi_data_from_fs()
{
  ESP_LOGI(TAG, "-> loading WiFi info from fs");
  const char *wifi_data = load_data_from_fs("/WiFi.txt");
  // String input;

  JsonDocument doc1;
  DeserializationError error = deserializeJson(doc1, wifi_data);

  if (error)
  {
    ESP_LOGE(TAG, "[JSON.] Deserialization error %s", error.c_str());
    return;
  }
  const char *ESID = doc1["ssid"] | "null";
  const char *EPAS = doc1["pass"] | "null";
  strcpy(esid, ESID);
  strcpy(epass, EPAS);

  ESP_LOGI(TAG, "WiFi data loaded from fs: %s", ESID);

  return;
}

void load_hourmeter_from_fs()
{
  ESP_LOGI(TAG, "-> loading hourmeter from fs");
  JsonDocument json;
  const char *hourmeter_data = load_data_from_fs("/Hourmeter.txt");

  DeserializationError error = deserializeJson(json, hourmeter_data);
  if (error)
  {
    ESP_LOGE(TAG, "hourmeter Deserialization error raised with code: %s", error.c_str());
    return;
  }

  const int hours = json["hours"] | 0;
  const int minutes = json["minutes"] | 0;
  system_hourmeter = hours; // update global hourmeter value in hours.
  ESP_LOGI(TAG, "hourmeter loaded from fs: %d hours and %d minutes", hours, minutes);

  return;
}

void initialize_vars()
{

  // controller struct.
  controller_data.msg_type = MessageTypeEnum::DATA;
  controller_data.sender_role = PeerRoleID::CONTROLLER;
  controller_data.alarm_code = AlarmCode::NORMAL;
  controller_data.air_return_temp = 24;
  controller_data.air_supply_temp = 24;
  controller_data.drain_switch = true;
  controller_data.cooling_relay = false;
  controller_data.fan_relay = false;
  controller_data.seconds_since_last_cooling_rq = 0;

  // monitor struct
  //  DATA, MONITOR, 0, 24, 24, 24, 24, 5, 5, 0, 0, false, AlarmCode::NORMAL, 0, 0
  monitor_data.msg_type = MessageTypeEnum::DATA;
  monitor_data.sender_role = PeerRoleID::MONITOR;
  monitor_data.alarm_code = AlarmCode::NORMAL;
  monitor_data.ambient_temp = 24;
  monitor_data.discharge_temp = 24;
  monitor_data.liquid_temp = 24;
  monitor_data.vapor_temp = 24;
  monitor_data.low_pressure = 5;
  monitor_data.high_pressure = 5;
  monitor_data.ac_mains_voltage = 0;
  monitor_data.compressor_current = 0;
  monitor_data.compressor_state = 0;
  monitor_data.compressor_ctrl_signal = false;
  monitor_data.compressor_startup_ms = 0;
  monitor_data.seconds_since_last_cooling_rq = 0;
  monitor_data.total_cooling_hours = 0;

  // system alarms struct
  system_alarms.controller_ac = AlarmCode::NORMAL;
  system_alarms.monitor_ac = AlarmCode::NORMAL;

  return;
}

// data loader...
void clio_fsdata_setup()
{
  initialize_vars();
  load_hourmeter_from_fs();
  load_operation_state_from_fs(); // on-off setting
  load_operation_mode_from_fs();  // function mode (cool, auto, fan)
  load_system_config_from_fs();   // system config protections and settings
  load_wifi_data_from_fs();       // load wifi data from filesystem
  return;
}