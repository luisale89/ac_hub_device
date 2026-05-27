#include "clio_globals.h"

static const char *TAG = "CLIO-HANDLERS";

// app hanlders
esp_err_t handle_peerlist_update(JsonDocument &received_json)
{
  ESP_LOGI(TAG, "Actualizando lista de peers desde JSON...");

  // 1. Validación de tamaño de buffer
  const int max_buffer_size = 512;
  if (measureJson(received_json) >= max_buffer_size)
  {
    ESP_LOGE(TAG, "Datos recibidos exceden el buffer de %d", max_buffer_size);
    return ESP_ERR_INVALID_SIZE;
  }

  JsonDocument updated_json;
  char output_data[max_buffer_size];

  const char *roles[] = {"controller", "monitor"};
  uint8_t mac_buffer[6];
  bool change_detected = false;

  for (const char *role : roles)
  {
    if (received_json[role].isNull())
    {
      ESP_LOGW(TAG, "No se encontró el campo '%s' en el JSON recibido", role);
      continue;
    };
    {
      const char *mac_str = received_json[role];

      // 3. Validación y Parsing de MAC
      if (is_valid_mac_str(mac_str))
      {
        char separator = strchr(mac_str, ':') ? ':' : (strchr(mac_str, '-') ? '-' : '.');
        parse_mac_address(mac_str, separator, mac_buffer, 16); // Base 16 para Hex

        char mac_str_copy[18];
        format_device_mac(mac_buffer, mac_str_copy); // Formatea a mayúsculas con ':'

        // 4. IMPORTANTE: .set() asegura que ArduinoJson copie el contenido del buffer local
        updated_json[role].set(mac_str_copy);

        ESP_LOGI(TAG, "MAC válida para %s: %s", role, mac_str_copy);
        change_detected = true;
      }
      else
      {
        ESP_LOGE(TAG, "Formato de MAC inválido para %s: %s", role, mac_str);
      }
    }
  }

  // 5. Persistencia
  if (change_detected)
  {
    serializeJson(updated_json, output_data, sizeof(output_data));
    esp_err_t save_result = save_data_in_fs(output_data, "/Peer.txt");
    if (save_result == ESP_OK)
    {
      ESP_LOGI(TAG, "Lista de peers guardada exitosamente");
      return ESP_OK;
    }
    else
    {
      ESP_LOGE(TAG, "Error al escribir en el sistema de archivos");
      return save_result;
    }
  }

  ESP_LOGW(TAG, "No se procesaron cambios válidos en la lista de peers");
  return ESP_ERR_NOT_FOUND;
}

esp_err_t handle_system_config_from_broker(JsonDocument &json) //[OK]
{
  const char *variable = json["variable"] | "null"; // "system_config"

  if (strcmp(variable, "system_config") == 0)
  {
    ESP_LOGI(TAG, "system configuration settings.");
    // Cambia la configuracion general del sistema
    // read settings values from json.
    system_config.sleep_control_en = json["sleep_control_en"] | false;
    system_config.room_temp_control_en = json["room_temp_control_en"] | false;
    system_config.comp_nominal_amp = json["comp_nominal_amp"] | 24;
    system_config.comp_amp_threshold = json["comp_amp_threshold"] | 32;
    system_config.discharge_max_temp = json["discharge_max_t"] | 107;
    system_config.liquid_max_temp = json["liquid_max_t"] | 60;
    system_config.vapor_line_min_temp = json["vapor_line_min_t"] | -5;
    system_config.max_recovery_attempts = json["max_recovery_attempts"] | 3;
    system_config.recovery_window = json["recovery_window"] | 120;

    // save settings in filesystem.
    save_config_in_fs();
    //- successfully processed the system configuration update, return ESP_OK.
    return ESP_OK;
  }

  if (strcmp(variable, "mode_config") == 0)
  {
    ESP_LOGI(TAG, "auto mode configuration settings.");
    // Cambia la configuracion del modo
    const char *value = json["value"];  // "auto"
    const int wait = json["wait"] | 15; // 15
    const int temp = json["temp"] | 28; // 28

    if (strcmp(value, "auto") != 0)
    {
      ESP_LOGE(TAG, "invalid value in json, expected: 'auto'");
      return ESP_ERR_INVALID_ARG;
    }

    if (wait <= 0 || wait > 60)
    {
      ESP_LOGE(TAG, "invalid range for wait value");
      return ESP_ERR_INVALID_ARG;
    }

    if (temp < 16 || temp > 28)
    {
      ESP_LOGE(TAG, "invalid range for temp value");
      return ESP_ERR_INVALID_ARG;
    }

    system_config.auto_setpoint = temp;
    system_config.auto_wait_time = wait;
    save_config_in_fs();
    //- successfully processed the auto mode configuration, return ESP_OK.
    return ESP_OK;
  }

  // invalid variable received in json document.
  ESP_LOGE(TAG, "Invalid variable value in json document");
  return ESP_ERR_INVALID_ARG;
}

esp_err_t handle_system_settings_from_broker(JsonDocument &doc) //[OK]
{
  const char *variable = doc["variable"] | "null";

  if (strcmp(variable, "timectrl") == 0)
  {
    ESP_LOGI(TAG, "timectrl settings adjustment.");
    // Aqui hay que guardar la configuracion del control de apagado encendido
    // Cambia el horario de encendido o apagado
    system_config.sleep_control_en = doc["enabled"] | false;
    save_config_in_fs();

    SleepWakeCondition wake_condition = WAKE_ON_TIME; // default values
    SleepWakeCondition sleep_condition = SLEEP_ON_TIME;
    const char *on_condition = doc["on_condition"] | "on_time";
    const char *off_condition = doc["off_condition"] | "on_time";

    // on_condition y off_condition pueden ser "on_time" u "on_presence",
    // se valida el valor recibido y se asigna la condicion correspondiente para cada caso.
    if (strcmp(on_condition, "on_time") == 0)
    {
      wake_condition = WAKE_ON_TIME;
    }
    else
    {
      wake_condition = WAKE_ON_PRESENCE;
    }

    if (strcmp(off_condition, "on_time") == 0)
    {
      sleep_condition = SLEEP_ON_TIME;
    }
    else
    {
      sleep_condition = SLEEP_ON_ABSENCE;
    }

    for (JsonPair schedule_item : doc["schedule"].as<JsonObject>())
    {
      // get day to be configured...
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

      if (wake_time >= 2400 || wake_time < 0)
      {
        ESP_LOGE(TAG, "invalid 'wake_time' value received.. out of range");
        return ESP_ERR_INVALID_ARG;
      }

      if (sleep_time >= 2400 || sleep_time < 0)
      {
        ESP_LOGE(TAG, "invalid 'sleep_time' value received.. out of range.");
        return ESP_ERR_INVALID_ARG;
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
    //-successfully processed the schedule configuration for all days, return ESP_OK.
    return ESP_OK;
  }

  // invalid variable received in json document.
  ESP_LOGE(TAG, "Invalid variable value in json document");
  return ESP_ERR_INVALID_ARG;
}

esp_err_t handle_cmd_from_broker(JsonDocument &doc) //[OK, OK]
{
  const char *variable = doc["variable"] | "unkonw"; // "system_state"

  if (strcmp(variable, "system_state") == 0)
  {
    const char *value = doc["value"] | "invalid";
    ESP_LOGI(TAG, "system_state variable received");
    if (strcmp(value, "on") == 0)
    {
      SysState = SYSTEM_ON;
    }
    else if (strcmp(value, "off") == 0)
    {
      SysState = SYSTEM_OFF;
    }
    else if (strcmp(value, "sleep") == 0)
    {
      SysState = SYSTEM_SLEEP;
    }
    else
    {
      ESP_LOGI(TAG, "Error: invalid value received from broker on -system_state-");
      return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
  }

  if (strcmp(variable, "temp_sp") == 0)
  {
    const int value = doc["value"] | 24;
    ESP_LOGI(TAG, "temp_sp variable received");
    if (value < 16 || value > 28)
    {
      ESP_LOGI(TAG, "Error: invalid value received from broker on -temp_sp- variable");
      return ESP_ERR_INVALID_ARG;
    }
    system_config.user_setpoint = value;
    save_config_in_fs();
    return ESP_OK;
  }

  if (strcmp(variable, "system_mode") == 0)
  {
    const char *value = doc["value"] | "invalid";

    ESP_LOGI(TAG, "system operation mode cmd received");
    // Cambia el modo de operacion

    if (strcmp(value, "cool") == 0)
    {
      SysMode = COOL_MODE;
    }
    else if (strcmp(value, "fan") == 0)
    {
      SysMode = FAN_MODE;
    }
    else if (strcmp(value, "auto") == 0)
    {
      SysMode = AUTO_MODE;
    }
    else
    {
      ESP_LOGE(TAG, "Invalid value mode in json");
      return ESP_ERR_INVALID_ARG;
    }

    // save in filesystem.
    save_operation_mode_in_fs();
    //- successfully processed the system mode configuration,
    return ESP_OK;
  }

  if (strcmp(variable, "fault_restart") == 0)
  {
    const char *value = doc["value"] | "invalid";
    ESP_LOGI(TAG, "fault_restart variable received -> updating flag value");
    // set settings variable to be sent to the monitor device
    // this will restart the fault in the monitor device
    if (strcmp(value, "now") == 0)
    {
      fault_restart_attempt_flag = true;
    }
    else
    {
      ESP_LOGI(TAG, "Invalue value received from broker on -fault_restart- endpoint");
      return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
  }

  ESP_LOGE(TAG, "invalid variable received in json payload..");
  return ESP_ERR_INVALID_ARG;
}