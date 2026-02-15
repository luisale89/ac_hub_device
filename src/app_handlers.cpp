#include "clio_globals.h"

static const char *TAG = "CLIO-HANDLERS";

// app hanlders
void save_config_in_fs(system_config_struct *cnf) //[OK]
{
  // read global variables that controls the config and store them in the fs.
  char settings_buffer[256];
  JsonDocument doc;

  doc["sleep_control_enabled"] = cnf->sleep_control_enabled;
  doc["comp_nominal_amp"] = cnf->comp_nominal_amp;
  doc["comp_amp_threshold"] = cnf->comp_amp_threshold;
  doc["discharge_max_t"] = cnf->discharge_max_t;
  doc["liquid_max_t"] = cnf->liquid_max_t;
  doc["exterior_max_t"] = cnf->exterior_max_t;
  doc["vapor_line_min_t"] = cnf->vapor_line_min_t;
  doc["fault_auto_recovery_en"] = cnf->fault_auto_recovery_en;
  doc["max_recovery_attempts"] = cnf->max_recovery_attempts;
  doc["recovery_window"] = cnf->recovery_window;
  doc["user_setpoint"] = cnf->user_setpoint;
  doc["auto_setpoint"] = cnf->auto_setpoint;
  doc["auto_wait_time"] = cnf->auto_wait_time;

  serializeJson(doc, settings_buffer, sizeof(settings_buffer));
  save_data_in_fs(settings_buffer, "/Settings.txt");
  return;
}

void save_operation_mode_in_fs(){
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

void handle_peerlist_update(const char *received_data) {

    ESP_LOGI(TAG, "New data for espnow peers received..");
    JsonDocument updated_json;
    JsonDocument received_json;
    char output_data[256];

    DeserializationError error = deserializeJson(received_json, received_data);

    if (error)
    {   
        ESP_LOGE(TAG, "Deserialization error with code: %s", error.c_str());
        return;
    } 
    //get data
    const char *controller_address = received_json["controller"] | "null";  //FF.FF.FF.FF.FF.FF
    const char *monitor_address = received_json["monitor"] | "null";        //
    uint8_t mac_address_buffer[6];

    if (strcmp(controller_address, "null") != 0) //don't match
    {
        ESP_LOGI(TAG, "New serial for CONTROLLER device received: %s", controller_address);
        parse_mac_address(controller_address, '.', mac_address_buffer, 6, 16);
        updated_json["controller"] = print_device_mac(mac_address_buffer);
    }

    if (strcmp(monitor_address, "null") != 0) //don't match
    {
        ESP_LOGI(TAG, "New serial for MONITOR device received: %s", monitor_address);
        parse_mac_address(monitor_address, '.', mac_address_buffer, 6, 16);
        updated_json["monitor"] = print_device_mac(mac_address_buffer);
    }

    //save data.
    serializeJson(updated_json, output_data, sizeof(output_data));
    save_data_in_fs(output_data, "/Peer.txt");

    return;
}

void handle_temp_sp_from_broker(const char *json) //[OK]
{
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, json);

  if (error)
  {
    ESP_LOGE(TAG, "JSON Deserialization error raised with code: %s", error.c_str());
    return;
  }

  const char *variable = doc["variable"] | "null"; // "user_setpoint"
  const int temp_value = doc["value"] | 24;                    // 24
  ESP_LOGI(TAG, "Temp. sp received: %d °C", temp_value);

  if (strcmp(variable, "user_setpoint") != 0) {
    ESP_LOGE(TAG, "Invalid json variable, expected: 'user_setpoint'");
    return;
  }

  if (temp_value < 16 || temp_value > 28) {
    ESP_LOGE(TAG, "Invalid temperature range for 'user_setpoint' variable");
    return;
  }
  system_config.user_setpoint = temp_value;
  save_config_in_fs(&system_config);
  //save data in filesystem
}

void handle_system_config_from_broker(const char* json) //[OK]
{
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, json);

  if (error)
  {
    ESP_LOGE(TAG, "JSON Deserialization error raised with code: %s", error.c_str());
    return;
  }

  const char *variable = doc["variable"] | "null"; // "system_config"

  if (strcmp(variable, "system_config") != 0) {
    ESP_LOGE(TAG, "Invalid json variable, expected: 'system_config'");
    return;
  }

  // read settings values from json.
  system_config.comp_nominal_amp = doc["comp_nominal_amp"] | system_config.comp_nominal_amp;
  system_config.comp_amp_threshold = doc["comp_amp_threshold"] | system_config.comp_amp_threshold;
  system_config.discharge_max_t = doc["discharge_max_t"] | system_config.discharge_max_t;
  system_config.liquid_max_t = doc["liquid_max_t"] | system_config.liquid_max_t;
  system_config.exterior_max_t = doc["exterior_max_t"] | system_config.exterior_max_t;
  system_config.vapor_line_min_t = doc["vapor_line_min_t"] | system_config.vapor_line_min_t;
  system_config.fault_auto_recovery_en = doc["fault_auto_recovery_en"] | system_config.fault_auto_recovery_en;
  system_config.max_recovery_attempts = doc["max_recovery_attempts"] | system_config.max_recovery_attempts;
  system_config.recovery_window = doc["recovery_window"] | system_config.recovery_window;

  // save settings in filesystem.
  save_config_in_fs(&system_config);
}

void handle_system_settings_from_broker(const char* json) //[OK]
{
  JsonDocument data_received;
  DeserializationError error = deserializeJson(data_received, json);

  if (error)
  {
    ESP_LOGE(TAG, "JSON Deserialization error raised with code: %s", error.c_str());
    return;
  }

  const char *variable = data_received["variable"] | "null";

  if (strcmp(variable, "timectrl") == 0)
  {
    ESP_LOGI(TAG, "timectrl settings adjustment.");
    // Aqui hay que guardar la configuracion del control de apagado encendido
    // Cambia el horario de encendido o apagado
    system_config.sleep_control_enabled = data_received["enabled"] | false;
    save_config_in_fs(&system_config);

    SleepWakeCondition wake_condition = WAKE_ON_TIME; // default values
    SleepWakeCondition sleep_condition = SLEEP_ON_TIME;
    const char *on_condition = data_received["on_condition"] | "on_time";
    const char *off_condition = data_received["off_condition"] | "on_time";
    
    if (strcmp(on_condition, "on_time") == 0) {
      wake_condition = WAKE_ON_TIME;} else {wake_condition = WAKE_ON_PRESENCE;
    }
    if (strcmp(off_condition, "on_time") == 0) {
      sleep_condition = SLEEP_ON_TIME;} else {sleep_condition = SLEEP_ON_ABSENCE;
    }

    for (JsonPair schedule_item : data_received["schedule"].as<JsonObject>())
    {
      //get day to be configured...
      const char *schedule_item_key = schedule_item.key().c_str(); // "1", "2", "3", "4", "5", "6", "7"
      int intDay = atoi(schedule_item_key);
      char target_file[20];
      sprintf(target_file, "/%s.txt", Week_days[intDay - 1]);
      char document[512];
      JsonDocument daily_schedule;

      int wake_time = schedule_item.value()["wake_at"] | 859;
      int sleep_time = schedule_item.value()["sleep_at"] | 1759;
      bool sch_enabled = schedule_item.value()["enabled"] | false; // boolean value

      // validations...

      if (wake_time >= 2400 || wake_time < 0) {
        ESP_LOGE(TAG, "invalid 'wake_time' value received.. out of range");
        return;
      }

      if (sleep_time >= 2400 || wake_time < 0) {
        ESP_LOGE(TAG, "invalid 'sleep_time' value received.. out of range.");
      }

      // output
      daily_schedule["wake_at"] = wake_time;
      daily_schedule["sleep_at"] = sleep_time;
      daily_schedule["enabled"] = sch_enabled;
      daily_schedule["wake_condition"] = wake_condition;
      daily_schedule["sleep_condition"] = sleep_condition;

      // serialize document.
      serializeJson(daily_schedule, document, sizeof(document));

      // save data in fs.
      save_data_in_fs(document, target_file);
    }
  }

  else if (strcmp(variable, "mode_config") == 0)
  {
    ESP_LOGI(TAG, "auto mode configuration settings.");
    // Cambia la configuracion del modo
    const char *value = data_received["value"];           // "auto"
    const int wait = data_received["wait"] | 15;           // 15
    const int temp = data_received["temp"] | 28;           // 28

    if (strcmp(value, "auto") != 0) {
      ESP_LOGE(TAG, "invalid value in json, expected: 'auto'");
      return;
    } 

    if (wait <= 0 || wait > 60) {
      ESP_LOGE(TAG, "invalid range for wait value");
      return;
    }

    if (temp < 16 || temp > 28) {
      ESP_LOGE(TAG, "invalid range for temp value");
      return;
    }

    system_config.auto_setpoint = temp;
    system_config.auto_wait_time = wait;
    save_config_in_fs(&system_config);
  }

  else if (strcmp(variable, "system_mode") == 0)
  {
    ESP_LOGI(TAG, "system operation mode settings");
    // Cambia el modo de operacion
    const char *value = data_received["value"]; // "cool"

    if (strcmp(value, "cool") == 0) {SysMode = COOL_MODE;}
    else if (strcmp(value, "fan") == 0) {SysMode = FAN_MODE;}
    else if (strcmp(value, "auto") == 0) {SysMode = AUTO_MODE;}
    else {ESP_LOGE(TAG, "Invalid value mode in json");}
    
    // save in filesystem.
    save_operation_mode_in_fs();
  }

  else
  {
    ESP_LOGE(TAG, "Invalid variable value in json document");
  }
}

void handle_op_state_from_broker(const char* json) //[OK, OK]
{
  // String input;
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, json);
  if (error)
  {
    ESP_LOGE(TAG, "JSON Deserialization error raised with code: %s", error.c_str());
    return;
  }
  const char *variable = doc["variable"] | "unkonw"; // "system_state"
  const char *value = doc["value"] | "invalid";

  if (strcmp(variable, "system_state") == 0) {
    ESP_LOGI(TAG, "system_state variable received");
    if (strcmp(value,"on") == 0) {SysState = SYSTEM_ON;}
    else if (strcmp(value, "off") == 0) {SysState = SYSTEM_OFF;}
    else if (strcmp(value, "sleep") == 0) {SysState = SYSTEM_SLEEP;}
    else {ESP_LOGI(TAG, "Error: invalid value received from broker on -system_state-");}
    return;
  }
  
  if (strcmp(variable, "fault_restart") == 0) {
    ESP_LOGI(TAG, "fault_restart variable received -> updating flag value");
    //set settings variable to be sent to the monitor device
    //this will restart the fault in the monitor device
    if (strcmp(value, "now") == 0) {
      fault_reset_flag = true;
    } else {
      ESP_LOGI(TAG, "Invalue value received from broker on -fault_restart- endpoint");
    }
    return;
  }

  ESP_LOGE(TAG, "invalid variable received in json payload..");
  return;
}