#include "clio_globals.h"
#include "mqtt_credentials.h"
#include "credentials.h"
#include "esp_sntp.h"
#include <LittleFS.h>

#define TOPIC_STR_SIZE 64

// ## MQTT FUNCTIONS
static const char *TAG = "CLIO-MQTT";
static const int16_t CLIO_MQTT_BUFF_SIZE = 1024; // bufferSize in bytes
static const char *mqtt_broker = MQTT_BROKER;
static SysStateEnum PrevSysState = UNKN;
static bool prevCoolingRelayState = false;
// Topics
static char post_data_topic[TOPIC_STR_SIZE] = "";
static char incident_topic[TOPIC_STR_SIZE] = ""; //- topic para publicar los incidentes detectados al broker mqtt.
static char lwill_topic[TOPIC_STR_SIZE] = "";
static char cmd_ack_topic[TOPIC_STR_SIZE] = ""; //- topic para publicar los ack de los comandos recibidos al broker mqtt.
static char settings_topic[TOPIC_STR_SIZE] = "";
static char config_topic[TOPIC_STR_SIZE] = "";
static char cmd_topic[TOPIC_STR_SIZE] = "";
static char peerlist_update_topic[TOPIC_STR_SIZE] = "";
static const char *mqtt_username = MQTT_USERNAME;
static const char *mqtt_password = MQTT_PASSWORD;
static const int mqtt_port = 8883;
static bool post_mqttconn_ntf = false;
static bool publish_cmd_update = false;
static int last_mqttconnection_err = -1;

// time consts
static const unsigned long SaluteInterval = 1 * 30000UL;           // Tiempo para enviar que el dispositivo esta conectado,
static const unsigned long block_rapid_updates_interval = 10000UL; // Tiempo mínimo entre actualizaciones consecutivas (10 segundos)
static const unsigned long healthPublishInterval = 10UL * 60000UL; // Intervalo para publicar estado de salud del dispositivo (10 minutos)
// Time vars
static unsigned long mqttReconnectInterval = 1 * 10000UL; // 10 segundos para intentar reconectar al broker mqtt.
static unsigned long sensorPubInterval = 1 * 60000UL;     // Intervalo para publicar lecturas del sensor (1 minuto)
static unsigned long sensorLastPub = 0;                   // last time a message was sent to the broker, in milliseconds
static unsigned long lastSaluteTime = 0;
static unsigned long lastMqttReconnect = 0;
static unsigned long lastHealthPub = 0;
static unsigned long last_mqtt_message_event = 0;

struct SystemHealth
{
  uint32_t freeHeap;
  uint32_t minFreeHeap;
  float cpuTemp;
  uint8_t resetReason;
  float fsUsage;
};

esp_err_t clio_serialize_json(JsonDocument &doc, char *buffer, size_t buffer_size)
{
  // inject common metadata to the document before serialization.
  doc["metadata"]["did"] = hub_device_serial;
  doc["metadata"]["firmw"] = FIRMWARE_VERSION;
  doc["metadata"]["time"] = DS3231_RTC.now().unixtime(); // timestamp and identifier of the message..
  //--serialize
  size_t doc_len = measureJson(doc); // for debugging, to check the size of the message before serialization.
  if (doc_len >= buffer_size)
  {
    ESP_LOGE(TAG, "JSON document is too large for buffer. Document not serialized.");
    return ESP_ERR_INVALID_SIZE;
  }

  size_t output_len = serializeJson(doc, buffer, buffer_size);
  if (output_len >= buffer_size)
  {
    ESP_LOGE(TAG, "Serialized JSON document is too large for buffer. Message may be truncated.");
    return ESP_ERR_INVALID_SIZE;
  }

  return ESP_OK;
}

bool is_wifi_connected()
{
  if (WiFi.status() != WL_CONNECTED && WiFi.localIP() == IPAddress(0, 0, 0, 0))
  {
    return false;
  }
  return true;
}

void build_topic(char *buffer, const char *resource) // build topic with format: clio/{resource}/{device_serial}
{
  snprintf(buffer, TOPIC_STR_SIZE, "clio/v1/%s/%s", resource, hub_device_serial);
  ESP_LOGD(TAG, "topic built: %s", buffer);
  return;
}

void mqtt_reconnect_interval_backoff()
{
  mqttReconnectInterval = mqttReconnectInterval * 2;
  if (mqttReconnectInterval > 60000UL)
  {
    mqttReconnectInterval = 60000UL;
  }
  return;
}

esp_err_t publish_ack_to_broker(esp_err_t &handler_result, const char *session_id)
{
  JsonDocument ack_doc;
  ack_doc["variable"] = "validation";
  char value_msg[512];
  if (handler_result == ESP_OK)
  {
    strcpy(value_msg, "Se ha actualizado correctamente");
    ack_doc["metadata"]["type"] = "success";
  }
  else if (handler_result == ESP_ERR_INVALID_STATE)
  {
    // rate-limit error: calculate remaining time until next allowed message
    unsigned long now = millis();
    unsigned long remaining_ms = 0;
    if (last_mqtt_message_event + block_rapid_updates_interval > now)
    {
      remaining_ms = (last_mqtt_message_event + block_rapid_updates_interval) - now;
    }
    // convert to seconds (round up)
    unsigned long remaining_s = (remaining_ms + 999) / 1000;
    snprintf(value_msg, sizeof(value_msg), "Rate limit: espere %lus antes de reintentar", remaining_s);
    ack_doc["metadata"]["type"] = "warning";
    ack_doc["metadata"]["retry_s"] = remaining_s;
  }
  else
  {
    strcpy(value_msg, "Error al actualizar");
    ack_doc["metadata"]["type"] = "danger";
  }
  ack_doc["value"] = value_msg;
  ack_doc["metadata"]["esp_err_t"] = handler_result;
  ack_doc["metadata"]["session_id"] = session_id;

  //-- serialize ack message
  char ack_message[CLIO_MQTT_BUFF_SIZE];
  esp_err_t err = clio_serialize_json(ack_doc, ack_message, sizeof(ack_message));
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to serialize JSON for command ack message.");
    return err;
  }
  bool msg_sent = mqtt_client.publish(cmd_ack_topic, ack_message);
  ESP_LOGI(TAG, "MQTT ack message publish result: %s", msg_sent ? "message sent!" : "fail");
  return msg_sent ? ESP_OK : ESP_FAIL;
}

void mqtt_message_callback(char *message_topic, byte *payload, unsigned int length) //[OK]
{
  //--- pong
  esp_err_t handler_result = ESP_OK;

  // Validate payload length before copying
  if (length >= CLIO_MQTT_BUFF_SIZE)
  {
    ESP_LOGE(TAG, "MQTT payload too large: %d bytes (max: %d)", length, CLIO_MQTT_BUFF_SIZE - 1);
    handler_result = ESP_ERR_INVALID_SIZE;
    publish_ack_to_broker(handler_result, "no-sid");
    return;
  }

  // copy payload to a char array and add null terminator for JSON deserialization
  char Mensaje[CLIO_MQTT_BUFF_SIZE];
  ESP_LOGI(TAG, "MQTT Message of lenght: %d arrived on topic: %s", length, message_topic);

  //-- copy in char array and add null terminator
  memcpy(Mensaje, payload, length);
  Mensaje[length] = '\0';
  ESP_LOGI(TAG, "+ Message: %s", Mensaje);

  JsonDocument json_payload;
  DeserializationError error = deserializeJson(json_payload, Mensaje);
  if (error)
  {
    ESP_LOGE(TAG, "JSON Deserialization error raised with code: %s", error.c_str());
    handler_result = ESP_ERR_INVALID_RESPONSE;
    // send ack to broker with error result
    publish_ack_to_broker(handler_result, "no-sid");
    return;
  }

  //--- process messages
  const char *session_id = json_payload["metadata"]["SID"] | "no-sid";
  const unsigned long current_millis = millis();
  ESP_LOGI(TAG, "Processing MQTT message with session ID: %s", session_id);

  // allow rapid processing for command topic (cmd_topic); block others if too frequent
  if (last_mqtt_message_event + block_rapid_updates_interval > current_millis && strcmp(message_topic, cmd_topic) != 0)
  {
    ESP_LOGW(TAG, "A MQTT message was processed recently. Ignoring this message to prevent rapid consecutive processing");
    handler_result = ESP_ERR_INVALID_STATE;
    publish_ack_to_broker(handler_result, session_id);
    return;
  }
  last_mqtt_message_event = current_millis;

  if (strcmp(message_topic, settings_topic) == 0)
  {
    // update system settings
    ESP_LOGI(TAG, "handling settings received from broker");
    handler_result = handle_system_settings_from_broker(json_payload);
  }

  else if (strcmp(message_topic, config_topic) == 0)
  {
    // update system config
    ESP_LOGI(TAG, "handling system config received from broker");
    handler_result = handle_system_config_from_broker(json_payload);
  }

  else if (strcmp(message_topic, cmd_topic) == 0)
  {
    // cmd from broker
    ESP_LOGI(TAG, "handling operation command received from broker");
    handler_result = handle_cmd_from_broker(json_payload);
    publish_cmd_update = handler_result == ESP_OK ? true : false;
    return; // this topic does not sends ack to the broker.
  }

  else if (strcmp(message_topic, peerlist_update_topic) == 0)
  {
    // update peerlist from broker
    ESP_LOGI(TAG, "handling peerlist update received from broker");
    handler_result = handle_peerlist_update(json_payload);
  }

  else
  {
    ESP_LOGW(TAG, "Received MQTT message on unrecognized topic: %s", message_topic);
    handler_result = ESP_ERR_NOT_SUPPORTED;
  }

  publish_ack_to_broker(handler_result, session_id);
  return;
}

esp_err_t publish_health_update()
{
  SystemHealth health;
  health.freeHeap = esp_get_free_heap_size();
  health.minFreeHeap = esp_get_minimum_free_heap_size();
  health.cpuTemp = temperatureRead();
  health.resetReason = (uint8_t)esp_reset_reason();

  size_t total = 0, used = 0;
  total = LittleFS.totalBytes();
  used = LittleFS.usedBytes();
  health.fsUsage = (total > 0) ? ((float)used / total) * 100.0f : 0.0f;

  JsonDocument doc;
  doc["variable"] = "health_update";
  doc["value"] = health.freeHeap; // example of sending one health metric as the main value, the rest is in metadata.
  //-- WiFi data.
  doc["metadata"]["ssid"] = WiFi.SSID();
  doc["metadata"]["rssi"] = WiFi.RSSI();
  doc["metadata"]["channel"] = WiFi.channel();
  doc["metadata"]["local_ip"] = WiFi.localIP().toString();
  doc["metadata"]["last_ntp_update"] = last_ntp_update;
  doc["metadata"]["cpu_temp"] = health.cpuTemp;
  doc["metadata"]["free_heap"] = health.minFreeHeap;
  doc["metadata"]["fs_usage"] = health.fsUsage;
  doc["metadata"]["reset_reason"] = health.resetReason;

  char message[CLIO_MQTT_BUFF_SIZE];
  esp_err_t err = clio_serialize_json(doc, message, sizeof(message));
  if (err != ESP_OK)
  {
    return err;
  }

  bool message_sent = mqtt_client.publish(post_data_topic, message);
  ESP_LOGI(TAG, "MQTT publish result: %s", message_sent ? "message sent!" : "fail");
  return message_sent ? ESP_OK : ESP_FAIL;
}

// ext call
esp_err_t publish_compressor_startup()
{
  JsonDocument doc;
  doc["variable"] = "compressor_startup";
  doc["value"] = monitor_data.compressor_startup_ms; // time in ms it took for the compressor to start (from 0 amp, peak, < threshold_current_value).
  doc["metadata"]["ambient_t"] = monitor_data.ambient_temp;
  doc["metadata"]["discharge_t"] = monitor_data.discharge_temp;
  doc["metadata"]["liquid_t"] = monitor_data.liquid_temp;
  doc["metadata"]["vapor_t"] = monitor_data.vapor_temp;
  doc["metadata"]["ac_mains"] = monitor_data.ac_mains_voltage;
  doc["metadata"]["rest_time"] = 0; // time in seconds since last compressor startup, this will be calculated in the monitor device, so we send 0 and let the monitor fill this field with the correct value.

  char message[CLIO_MQTT_BUFF_SIZE];
  esp_err_t err = clio_serialize_json(doc, message, sizeof(message));
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to serialize JSON for compressor startup message.");
    return err;
  }

  ESP_LOGI(TAG, "Notifying MQTT broker on compressor startup..");
  // sending message.
  bool message_sent = mqtt_client.publish(post_data_topic, message);
  ESP_LOGI(TAG, "MQTT publish result: %s", message_sent ? "message sent!" : "fail");
  return message_sent ? ESP_OK : ESP_FAIL;
}

// ext call
esp_err_t publish_new_incident(incident_struct *incident)
{

  const unsigned long currentMillis = millis();
  const unsigned long time_since_first = currentMillis - incident->first_fault_time;
  const int fault_recovery_attempts = incident->fault_recovery_attempts;
  const int tmbf_divider = (fault_recovery_attempts - 1) > 0 ? (fault_recovery_attempts - 1) : 1; // to avoid division by zero and to give a more accurate tmbf in the first recovery attempt.
  JsonDocument doc;

  doc["variable"] = "incident";
  doc["value"] = (int)SysFaultState;
  doc["metadata"]["controller_ac"] = system_alarms.controller_ac;
  doc["metadata"]["monitor_ac"] = system_alarms.monitor_ac;
  doc["metadata"]["ms_since_first"] = time_since_first;
  doc["metadata"]["tmbf_ms"] = time_since_first / tmbf_divider; //
  doc["metadata"]["recov_attempts"] = fault_recovery_attempts;
  doc["metadata"]["attempts_left"] = system_config.max_recovery_attempts - fault_recovery_attempts;

  char message[CLIO_MQTT_BUFF_SIZE];
  esp_err_t err = clio_serialize_json(doc, message, sizeof(message));
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to serialize JSON for incident message.");
    return err;
  }
  //- post message
  const bool msg_sent = mqtt_client.publish(incident_topic, message);
  return msg_sent ? ESP_OK : ESP_FAIL;
}

esp_err_t publish_sensor_readings() //[ok]
{

  // json todas las variables.
  char output[CLIO_MQTT_BUFF_SIZE];
  JsonDocument doc;

  doc["variable"] = "ac_hub";
  doc["value"] = (int)SysState;

  //-- hub data.
  doc["metadata"]["hub"][0] = (int)SysFaultState;
  doc["metadata"]["hub"][1] = (int)SysMode;
  doc["metadata"]["hub"][2] = (int)radarState;
  doc["metadata"]["hub"][3] = (int)daySleepControl;
  doc["metadata"]["hub"][4] = system_config.user_setpoint;
  doc["metadata"]["hub"][5] = activeSetpoint;
  doc["metadata"]["hub"][6] = system_hourmeter;
  doc["metadata"]["hub"][7] = room_temperature;
  doc["metadata"]["hub"][8] = (int)system_config.room_temp_control_en; // indicar si el control por temperatura ambiente esta habilitado o no,
  doc["metadata"]["ctrl"][0] = (int)controller_peer_online;
  doc["metadata"]["moni"][0] = (int)monitor_peer_online;

  if (controller_peer_online)
  {
    doc["metadata"]["ctrl"][1] = controller_data.air_supply_temp;
    doc["metadata"]["ctrl"][2] = controller_data.air_return_temp;
    doc["metadata"]["ctrl"][3] = (int)controller_data.cooling_relay;
    doc["metadata"]["ctrl"][4] = (int)controller_data.fan_relay;
    doc["metadata"]["ctrl"][5] = (int)controller_data.drain_switch;
    doc["metadata"]["ctrl"][6] = controller_data.seconds_since_last_cooling_rq;
    doc["metadata"]["ctrl"][7] = controller_data.alarm_code;
  }

  if (monitor_peer_online)
  {
    doc["metadata"]["moni"][1] = monitor_data.ambient_temp;
    doc["metadata"]["moni"][2] = monitor_data.discharge_temp;
    doc["metadata"]["moni"][3] = monitor_data.liquid_temp;
    doc["metadata"]["moni"][4] = monitor_data.vapor_temp;
    doc["metadata"]["moni"][5] = monitor_data.low_pressure;
    doc["metadata"]["moni"][6] = monitor_data.high_pressure;
    doc["metadata"]["moni"][7] = monitor_data.ac_mains_voltage;
    doc["metadata"]["moni"][8] = (int)monitor_data.compressor_state;
    doc["metadata"]["moni"][9] = monitor_data.compressor_current;
    doc["metadata"]["moni"][10] = monitor_data.seconds_since_last_cooling_rq;
    doc["metadata"]["moni"][11] = monitor_data.total_cooling_hours;
    doc["metadata"]["moni"][12] = monitor_data.alarm_code;
  }

  esp_err_t err = clio_serialize_json(doc, output, sizeof(output)); // minified json

  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to serialize JSON for variable update message.");
    return err;
  }

  ESP_LOGI(TAG, "-> Publishing variables to broker");
  ESP_LOGD(TAG, "[payload size: %d] [topic: %s]", measureJson(doc), post_data_topic);
  //-
  bool mqtt_msg_sent = mqtt_client.publish(post_data_topic, output);
  ESP_LOGI(TAG, "MQTT publish result: %s", mqtt_msg_sent ? "message sent!" : "fail");
  return mqtt_msg_sent ? ESP_OK : ESP_FAIL;
}

esp_err_t publish_new_connection()
{
  const unsigned long currentMillis = millis();

  if (!post_mqttconn_ntf)
  // if the notification was already sent
  {
    return ESP_OK;
  }

  if (currentMillis - lastSaluteTime < SaluteInterval)
  {
    return ESP_OK;
  }

  ESP_LOGD(TAG, "Publishing MQTT connection notification to the broker..");

  post_mqttconn_ntf = false;
  lastSaluteTime = currentMillis;

  JsonDocument doc;
  doc["variable"] = "device_connection";
  doc["value"] = "connected";
  doc["metadata"]["mqttconn_err"] = last_mqttconnection_err;

  char message[CLIO_MQTT_BUFF_SIZE];
  esp_err_t err = clio_serialize_json(doc, message, sizeof(message));
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to serialize JSON for MQTT connection notification.");
    return err;
  }
  //-post message
  const bool msg_sent = mqtt_client.publish(lwill_topic, message, true); // retained message.
  return msg_sent ? ESP_OK : ESP_FAIL;
}

esp_err_t connectToMQTT()
{
  if (mqtt_client.connected()) // already connected.
  {
    //-publish 'connected' message to lwill topic.
    // has a delay of 30 seconds after connecting to the broker, and will be sent only once after connection
    publish_new_connection();
    ntw_led_style = PULSE; // solid led indicates wifi connection but disconnected from the mqtt broker.
    return ESP_OK;
  };

  if (!is_wifi_connected()) // check if wifi connection is completed before trying mqtt connection.
  {
    return ESP_FAIL;
  };

  // try new connection.
  const unsigned long currentMillis = millis();
  ntw_led_style = ALLWAYS_ON; // solid led indicates wifi connection but disconnected from the mqtt broker.
  //-

  if (currentMillis - lastMqttReconnect < mqttReconnectInterval)
  {
    return ESP_FAIL;
  }

  lastMqttReconnect = currentMillis;

  if (!is_time_synchronized())
  // check if time is synchronized before trying mqtt connection
  {
    // X.509 validation requires synchronization time
    ESP_LOGW(TAG, "Tiempo no sincronizado. No se puede conectar al broker MQTT aun.");
    return ESP_FAIL;
  };

  // connecting to a mqtt broker
  ESP_LOGI(TAG, "MQTT connection attempt..");
  //-
  char client_id[50];
  sprintf(client_id, "CLIO-%s", hub_device_serial);
  ESP_LOGI(TAG, "client id: %s", client_id);

  //- lastWill
  char lwill_msg[CLIO_MQTT_BUFF_SIZE];
  JsonDocument doc;
  doc["variable"] = "device_connection";
  doc["value"] = "disconnected";
  esp_err_t err = clio_serialize_json(doc, lwill_msg, sizeof(lwill_msg));

  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to serialize JSON for MQTT last will message.");
    return err;
  }

  const uint8_t lwill_qos = 0;
  const bool lwill_retain = true;
  const bool mqtt_connected = mqtt_client.connect(client_id, mqtt_username, mqtt_password, lwill_topic, lwill_qos, lwill_retain, lwill_msg, true);
  // Try mqtt connection to the broker.
  if (!mqtt_connected)
  {
    ESP_LOGE(TAG, "Failed to connect to MQTT broker.");
    last_mqttconnection_err = mqtt_client.state();
    ESP_LOGE(TAG, "Fail MQTT Connection with state: %d", last_mqttconnection_err);

    mqtt_reconnect_interval_backoff();
    ESP_LOGW(TAG, "Fallo. Reintento en %lu ms", mqttReconnectInterval);

    return ESP_FAIL;
  }
  // restart mqttReconnectInterval to initial value after a successful connection.
  mqttReconnectInterval = 1 * 10000UL;
  //
  ESP_LOGI(TAG, "Connected to MQTT broker!");
  ESP_LOGI(TAG, "Subscribing to mqtt topics:");
  //-
  mqtt_client.subscribe(settings_topic);
  ESP_LOGI(TAG, "settings topic ok");
  //-
  mqtt_client.subscribe(config_topic);
  ESP_LOGI(TAG, "config topic ok");
  //-
  mqtt_client.subscribe(cmd_topic);
  ESP_LOGI(TAG, "cmd topic ok");
  //-
  mqtt_client.subscribe(peerlist_update_topic);
  ESP_LOGI(TAG, "peerlist update topic ok");
  //-
  lastSaluteTime = millis();
  post_mqttconn_ntf = true; // flag to post connection message.
  ESP_LOGI(TAG, "MQTT connection done. **");

  return ESP_OK;
}

bool is_system_state_changed()
{
  const unsigned long currentMillis = millis();
  // detect changes in the system state or in the controller cooling relay state,
  // to trigger immediate mqtt updates and save the new state in the filesystem.
  bool publish_update = false;
  bool state_changed = false;

  if (SysState != PrevSysState)
  {
    // if there is a change in the system state, we want to save the new state in the filesystem immediately,
    // and trigger an immediate mqtt update to notify the change.
    ESP_LOGI(TAG, "SysState change detected!");
    PrevSysState = SysState;
    state_changed = true;
    publish_update = true;
  }

  if (publish_cmd_update && currentMillis - sensorLastPub > block_rapid_updates_interval)
  {
    // if there is an update triggered by a command received from the broker, we want to trigger an
    // immediate mqtt update to notify the change, but we also want to rate-limit these updates to prevent
    // flooding the broker with updates if multiple commands are received in a short period of time.
    ESP_LOGI(TAG, "Command update detected!");
    publish_cmd_update = false;
    publish_update = true;
  }

  if (controller_data.cooling_relay != prevCoolingRelayState && currentMillis - sensorLastPub > block_rapid_updates_interval)
  {
    // if there is a change in the cooling relay state, we want to trigger an immediate mqtt update to notify the change,
    // we also want to rate-limit the updates triggered by changes in the cooling relay state, since it's a variable that can change frequently and we don't want to flood the broker with updates.
    ESP_LOGI(TAG, "Cooling relay state change detected!");
    prevCoolingRelayState = controller_data.cooling_relay;
    publish_update = true;
  }

  if (state_changed)
  {
    // save in fs if there is a change in the system state or in the system fault state
    ESP_LOGI(TAG, "System state changed. Updated state saved in filesystem.");
    save_operation_state_in_fs();
  }

  // return true to publish an update to the broker.
  return publish_update;
}

void clio_mqtt_loop()
{
  //-
  // connect to the broker
  if (connectToMQTT() != ESP_OK)
  {
    return;
  }
  // mqtt loop
  mqtt_client.loop();
  const unsigned long currentMillis = millis();
  const unsigned long INTERVAL_ACTIVE = 1 * 60000UL; // 1 minute
  const unsigned long INTERVAL_IDLE = 5 * 60000UL;   // 5 minutes
  const bool publish_update = is_system_state_changed();
  // update mqtt posting interval based on system state
  sensorPubInterval = (SysState == SYSTEM_ON) ? INTERVAL_ACTIVE : INTERVAL_IDLE;

  // check if its time to post a message.
  if ((currentMillis - sensorLastPub > sensorPubInterval) || publish_update)
  {
    // send all the sensor data to the mqtt broker
    sensorLastPub = currentMillis;
    publish_sensor_readings();
  }

  // post health update
  if (currentMillis - lastHealthPub > healthPublishInterval)
  {
    publish_health_update();
    lastHealthPub = currentMillis;
  }
}

esp_err_t clio_mqtt_setup()
{

  mqttWiFiClient.setCACert(ca_cert); // mqtt broker ca-cert.
  bool buffer_set = mqtt_client.setBufferSize(CLIO_MQTT_BUFF_SIZE);
  if (!buffer_set)
  {
    ESP_LOGE(TAG, "could not set mqtt buffer size.");
    return ESP_ERR_NOT_SUPPORTED;
  }

  mqtt_client
      .setServer(mqtt_broker, mqtt_port)
      .setCallback(mqtt_message_callback);

  //---------------------------------------- update mqtt topics
  ESP_LOGI(TAG, "building up the topics");

  build_topic(cmd_topic, "cmd");               //. -> para recibir comandos varios, como por ejemplo: reiniciar el dispositivo, iniciar un ciclo de desescarche, etc.
  build_topic(config_topic, "config");         //. -> para recibir los ajustes de las protecciones del sistema
  build_topic(settings_topic, "settings");     //. -> para recibir los ajustes de operación del sistema (ej: setpoint, horarios, etc.)
  build_topic(peerlist_update_topic, "peers"); //. -> para recibir actualizaciones de la lista de pares.
  build_topic(lwill_topic, "connection");      //. -> para publicar mensaje de conexión o desconexión del dispositivo, con retención en el broker (last will).
  build_topic(post_data_topic, "sensor_data"); //. -> para publicar las variables del sistema al broker mqtt.
  build_topic(cmd_ack_topic, "cmd_ack");       //. -> para publicar los ack de los comandos recibidos al broker mqtt. --- IGNORE ---
  build_topic(incident_topic, "incident");     //. -> para publicar los incidentes detect

  ESP_LOGI(TAG, "Mqtt config done.");

  return ESP_OK;
}