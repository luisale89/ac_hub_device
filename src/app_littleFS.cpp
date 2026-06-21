#include "clio_globals.h"
#include <LittleFS.h>

static const char *TAG = "CLIO-SPIFFS";

// ### FILESYSTEM functions. ###

esp_err_t clio_spiffs_setup()
{
  if (!LittleFS.begin(true))
  {
    ESP_LOGE(TAG, "Ocurrió un error al ejecutar LittleFS..");
    return ESP_FAIL;
  }
  return ESP_OK;
}

char *load_data_from_fs(const char *target_file)
{
  static char buffer[768]; // Buffer estático para evitar heap
  //-
  File f = LittleFS.open(target_file);
  if (!f)
  {
    ESP_LOGE(TAG, "Archivo solicitado no existe..");
    strcpy(buffer, "");
    return buffer;
  }

  size_t len = f.size();
  if (len >= sizeof(buffer))
  {
    ESP_LOGE(TAG, "Archivo demasiado grande para el buffer.");
    f.close();
    strcpy(buffer, "");
    return buffer;
  }

  f.readBytes(buffer, len);
  buffer[len] = '\0';
  f.close();

  ESP_LOGI(TAG, "data loaded from SPIFFS correctly");
  ESP_LOGD(TAG, "data: %s", buffer);
  return buffer;
}

esp_err_t save_data_in_fs(const char *data_to_save, const char *target_file)
{
  // savin data in filesystem.
  ESP_LOGI(TAG, "saving data in LittleFS..");
  ESP_LOGD(TAG, "target_file: %s", target_file);
  ESP_LOGD(TAG, "data: %s", data_to_save);

  File f = LittleFS.open(target_file, "w");
  if (!f)
  {
    ESP_LOGE(TAG, "Error al abrir el archivo solicitado.");
    return ESP_FAIL;
  }

  f.print(data_to_save);
  f.close();
  ESP_LOGI(TAG, "data saved correctly in LittleFS.");
  return ESP_OK;
}

void save_wifi_data_in_fs()
{
  JsonDocument doc;
  char jsonData[512]; // buffer for wifi data to save in fs.

  doc["ssid"] = WiFi.SSID();
  doc["pass"] = WiFi.psk();

  serializeJson(doc, jsonData, sizeof(jsonData));
  save_data_in_fs(jsonData, "/WiFi.txt");
  return;
}

void save_operation_state_in_fs() //[OK] [OK]
{

  char op_state_buffer[512];
  JsonDocument doc;

  switch (SysState)
  {
  case SYSTEM_ON:
    doc["sys_state"] = "on";
    break;

  case SYSTEM_OFF:
    doc["sys_state"] = "off";
    break;

  case SYSTEM_SLEEP:
    doc["sys_state"] = "sleep";
    break;

  case SYSTEM_ERROR:
    doc["sys_state"] = "error";
    break;

  default:
    doc["sys_state"] = "off";
    break;
  }

  switch (SysFaultState)
  {
  case STATUS_OK:
    doc["err_state"] = "ok";
    break;

  case STATUS_ERROR:
    doc["err_state"] = "error";
    break;

  default:
    doc["err_state"] = "ok";
    break;
  }

  serializeJson(doc, op_state_buffer, sizeof(op_state_buffer));
  save_data_in_fs(op_state_buffer, "/Estado.txt");
  return;
}

void save_config_in_fs() //[OK]
{
  // read global variables that controls the config and store them in the fs.
  char settings_buffer[512];
  JsonDocument doc;

  doc["sleep_control_en"] = system_config.sleep_control_en;
  doc["room_temp_control_en"] = system_config.room_temp_control_en;
  doc["comp_nominal_amp"] = system_config.comp_nominal_amp;
  doc["comp_amp_threshold"] = system_config.comp_amp_threshold;
  doc["discharge_max_t"] = system_config.discharge_max_temp;
  doc["liquid_max_t"] = system_config.liquid_max_temp;
  doc["vapor_line_min_t"] = system_config.vapor_line_min_temp;
  doc["max_recovery_attempts"] = system_config.max_recovery_attempts;
  doc["recovery_window"] = system_config.recovery_window;
  doc["user_setpoint"] = system_config.user_setpoint;
  doc["auto_setpoint"] = system_config.auto_setpoint;
  doc["auto_wait_time"] = system_config.auto_wait_time;

  serializeJson(doc, settings_buffer, sizeof(settings_buffer));
  save_data_in_fs(settings_buffer, "/Settings.txt");
  return;
}

void save_operation_mode_in_fs()
{
  char op_mode_buffer[128];
  JsonDocument doc;

  switch (SysMode)
  {
  case SysModeEnum::AUTO_MODE:
    doc["sys_mode"] = "auto";
    break;

  case SysModeEnum::COOL_MODE:
    doc["sys_mode"] = "cool";
    break;

  case SysModeEnum::FAN_MODE:
    doc["sys_mode"] = "fan";
    break;

  default:
    break;
  }

  serializeJson(doc, op_mode_buffer, sizeof(op_mode_buffer));
  save_data_in_fs(op_mode_buffer, "/Modo.txt");
  return;
}