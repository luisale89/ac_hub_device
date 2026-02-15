#include "clio_globals.h"
#include "mqtt_credentials.h"
#include "credentials.h"
#include <esp_system.h>

// ## MQTT FUNCTIONS
static const char *TAG = "CLIO-MQTT";
static const int16_t CLIO_MQTT_BUFF_SIZE = 1024; //bufferSize in bytes
static const char *mqtt_broker = MQTT_BROKER;
// Topics
static char post_data_topic[64] = "";
static char settings_topic[64] = "";
static char config_topic[64] = "";
static char opstate_topic[64] = "";
static char opsetpoint_topic[64] = "";
static char peer_list_topic[64] = "";
static char lwill_topic[64] = "";
static char incident_topic[64] = "";
static const char *mqtt_username = MQTT_USERNAME;
static const char *mqtt_password = MQTT_PASSWORD;
static const int mqtt_port = 8883;
static bool post_mqttconn_ntf = false;
static int last_mqttconnection_err = -5;

//time consts
static const unsigned long SaluteInterval = 1L * 30000L;           // Tiempo para enviar que el dispositivo esta conectado,
static const unsigned long mqttReconnectInterval = 1L * 10000L; // 10 segundos para intentar reconectar al broker mqtt.
// Time vars
static unsigned long lastMqttMessagePost = 0; // last time a message was sent to the broker, in milliseconds
static unsigned long lastSaluteTime = 0;
static unsigned long lastMqttReconnect = 0;
static unsigned long mqttPostingInterval = 1L * 60000L;   //


void mqtt_message_callback(char *message_topic, byte *payload, unsigned int length) //[OK]
{
  char Mensaje[CLIO_MQTT_BUFF_SIZE];
  //begin.
  ESP_LOGI(TAG, "$ MQTT Message of lenght: %s arrived on topic: %s", length ,message_topic);
  for (int i = 0; i < length && i < sizeof(Mensaje) - 1; i++)
  {
    Mensaje[i] = (char)payload[i];
  }
  Mensaje[length] = '\0';
  //print message in logger.
  ESP_LOGI(TAG, "+ Message: %s", Mensaje);

  // Selecciona la funcion acorde al topico al cual llego el mensaje
  if (strcmp(message_topic, settings_topic) == 0)
  {
    // Cambia la configuracion del sistema
    ESP_LOGI(TAG, "processing settings received from broker");
    handle_system_settings_from_broker(Mensaje);
  }
  else if (strcmp(message_topic, config_topic) == 0)
  {
    // Cambia la configuracion de las protecciones del sistema
    ESP_LOGI(TAG, "processing system config received from broker");
    handle_system_config_from_broker(Mensaje);
  }
  else if (strcmp(message_topic, opstate_topic) == 0)
  {
    // Apaga o enciende el sistema
    ESP_LOGI(TAG, "processing operation state received from broker");
    handle_op_state_from_broker(Mensaje);
  }
  else if (strcmp(message_topic, opsetpoint_topic) == 0)
  {
    // Ajusta la temperatura del ambiente
    ESP_LOGI(TAG, "processing temp. setpoint received from broker");
    handle_temp_sp_from_broker(Mensaje);
  }
  else if (strcmp(message_topic, peer_list_topic) == 0)
  {
    // actualiza la lista de pares en el spiffs.
    ESP_LOGI(TAG, "processing peer update.");
    handle_peerlist_update(Mensaje);
  }
  else {
    ESP_LOGE(TAG, "mqtt topic not implemented.");
  }
  //--- pong
  postVariablesToBroker = true; // response on any msg received
  delay(100);
}

// sys_err_t notify_state_update_to_broker(state_update_struct data)
void notify_state_update_to_broker()
{ 
  if (!postMqttStateUpdate){return;}

  char message[512];
  JsonDocument doc;
  doc["variable"] = "system_update";
  doc["value"] = "state_change";
  doc["metadata"]["system_prev_state"] = SysStateBuffer;
  doc["metadata"]["system_new_state"] = SysState;
  doc["metadata"]["did"] = hub_device_serial;
  serializeJson(doc, message, sizeof(message));

  ESP_LOGI(TAG, "Notifying MQTT broker on System State update..");
  // sending message.
  bool message_sent = mqtt_client.publish(post_data_topic, message);
  ESP_LOGI(TAG, "MQTT publish result: %s", message_sent ? "message sent!" : "fail");

  postMqttStateUpdate = false;
  return;
}

void post_variables_to_broker() //[ok]
{
  const unsigned long currentMillis = millis();
  // check if its time to post a message.
  if (currentMillis - lastMqttMessagePost > mqttPostingInterval) {postVariablesToBroker = true;}
  // check if there is a pending post request.
  if (!postVariablesToBroker){return;}

  // json todas las variables.
  char output[CLIO_MQTT_BUFF_SIZE];
  JsonDocument doc;

  doc["variable"] = "ac_hub";
  doc["value"] = SysState;
  doc["metadata"]["firmware"] = FIRMWARE_VERSION;
  doc["metadata"]["did"] = hub_device_serial;
  //-- WiFi data.
  doc["metadata"]["health"][0] = WiFi.SSID();
  doc["metadata"]["health"][1] = WiFi.RSSI();
  doc["metadata"]["health"][2] = WiFi.channel();
  doc["metadata"]["health"][3] = WiFi.localIP().toString();
  doc["metadata"]["health"][4] = last_ntp_update;
  doc["metadata"]["health"][5] = case_pcb_temperature;
  doc["metadata"]["health"][6] = esp_get_free_heap_size();

  //-- sensors data.
  doc["metadata"]["fault_state"] = SysFaultState; // fault status (OK-WARNING-ERROR)
  doc["metadata"]["sys_mode"] = SysMode;
  doc["metadata"]["presence"] = radarState;
  doc["metadata"]["timectrl"] = daySleepControl;
  doc["metadata"]["user_sp"] = system_config.user_setpoint;
  doc["metadata"]["active_sp"] = activeSetpoint;
  doc["metadata"]["hourmeter"] = system_hourmeter;
  //-- espnow peers data.
  doc["metadata"]["controller"][0] = controller_peer_online;
  doc["metadata"]["monitor"][0] = monitor_peer_online;

  if (controller_peer_online) {
    doc["metadata"]["controller"][1] = controller_data.air_supply_temp;
    doc["metadata"]["controller"][2] = controller_data.air_return_temp;
    doc["metadata"]["controller"][3] = controller_data.cooling_relay;
    doc["metadata"]["controller"][4] = controller_data.fan_relay;
    doc["metadata"]["controller"][5] = controller_data.drain_switch;
    doc["metadata"]["controller"][6] = controller_data.seconds_since_last_cooling_rq;
    doc["metadata"]["controller"][7] = controller_data.total_fan_hours;
  }

  if (monitor_peer_online) {
    doc["metadata"]["monitor"][1] = monitor_data.ambient_temp;
    doc["metadata"]["monitor"][2] = monitor_data.discharge_temp;
    doc["metadata"]["monitor"][3] = monitor_data.liquid_temp;
    doc["metadata"]["monitor"][4] = monitor_data.vapor_temp;
    doc["metadata"]["monitor"][5] = monitor_data.low_pressure;
    doc["metadata"]["monitor"][6] = monitor_data.high_pressure;
    doc["metadata"]["monitor"][7] = monitor_data.ac_mains_voltage;
    doc["metadata"]["monitor"][8] = monitor_data.compressor_current;
    doc["metadata"]["monitor"][9] = monitor_data.compressor_state;
    doc["metadata"]["monitor"][10] = monitor_data.alarm_code;
    doc["metadata"]["monitor"][11] = monitor_data.seconds_since_last_cooling_rq;
    doc["metadata"]["monitor"][12] = monitor_data.total_cooling_hours;
  }

  serializeJson(doc, output, sizeof(output));
  ESP_LOGI(TAG, "-> Publishing variables to broker");
  ESP_LOGD(TAG, "[payload size: %s] [topic: %s]", sizeof(output), post_data_topic);
  bool mqtt_msg_sent = mqtt_client.publish(post_data_topic, output);
  ESP_LOGI(TAG, "MQTT publish result: %s", mqtt_msg_sent ? "message sent!" : "fail");

  bool short_post_interval = false;
  if (SysState == SYSTEM_ON) {
    short_post_interval = true;
  }
  // } else if (controller_peer_online) {
  //   short_post_interval |= controller_data.cooling_relay || controller_data.fan_relay;

  // } else if (monitor_peer_online) {
  //   short_post_interval |= monitor_data.compressor_state;
  // }

  //- set posting interval based on the system state value.
  if (short_post_interval) {
    mqttPostingInterval = 1L * 60000L; // 1 minuto
  } else {
    mqttPostingInterval = 5L * 60000L; // 5 minutos si ninguna condición se cumple.
  }

  postVariablesToBroker = false;
  lastMqttMessagePost = currentMillis;
}

void post_mqttconnection_ntf() {
  const unsigned long currentMillis = millis();
  const esp_reset_reason_t boot_reason = esp_reset_reason();

if (post_mqttconn_ntf && currentMillis - lastSaluteTime > SaluteInterval)
  {
    post_mqttconn_ntf = false;
    lastSaluteTime = currentMillis;

    char _msg[256];
    JsonDocument doc;
    doc["variable"] = "device_connection";
    doc["value"] = "connected";
    doc["metadata"]["current_sys_state"] = SysState;
    doc["metadata"]["firmware"] = FIRMWARE_VERSION;
    doc["metadata"]["mqttconn_err"] = last_mqttconnection_err;
    switch (boot_reason)
    {
    case ESP_RST_POWERON: doc["metadata"]["boot_rs"] = "Power On"; break;
    case ESP_RST_SW: doc["metadata"]["boot_rs"] = "Software Reset"; break;
    case ESP_RST_WDT: doc["metadata"]["boot_rs"] = "Watchdog Timer"; break;
    default: doc["metadata"]["boot_rs"] = "Other"; break;
    }

    serializeJson(doc, _msg, sizeof(_msg));
    //-post message
    const bool msg_sent = mqtt_client.publish(lwill_topic, _msg, true); //retained message.

    if(msg_sent) {
      ESP_LOGI(TAG, "'boot message pub sent");
    }else{
      ESP_LOGE(TAG, "boot message pub error");
    }
  }
  return;
}

esp_err_t post_incident_to_broker(incident_struct *args) {

  if (!mqtt_client.connected()) {return ESP_FAIL;}

  const unsigned long currentMillis = millis();
  char _msg[256];
  JsonDocument doc;

  doc["variable"] = "incident";
  doc["value"] = SysFaultState; //WARNING OR ERROR
  doc["metadata"]["fault_code"] = args->fault_code;
  doc["metadata"]["runtime"] = currentMillis;
  doc["metadata"]["ms_since_first"] = currentMillis - args->first_fault_time;
  doc["metadata"]["ms_since_last"] = currentMillis - args->last_fault_time;
  doc["metadata"]["recovery_attempts"] = args->fault_recovery_attempts;

  serializeJson(doc, _msg, sizeof(_msg));
  //- post message
  const bool msg_sent = mqtt_client.publish(incident_topic, _msg);
  if (msg_sent) {
    ESP_LOGD(TAG, "incident pub success");
    return ESP_OK;
  }

  ESP_LOGE(TAG, "mqtt pub error");
  return ESP_FAIL;
}

void connectToMQTT() {

  if (WiFi.status() != WL_CONNECTED) {return;}  // check it there is an WiFi active connection
  if (mqtt_client.connected()) {return;}   // already connected to the broker.
  // try new connection.
  const unsigned long currentMillis = millis();
  ntw_led_style = ALLWAYS_ON; //solid led indicates wifi connection but disconnected from the mqtt broker.
  //-
  if (currentMillis - lastMqttReconnect >= mqttReconnectInterval)
  {
    lastMqttReconnect = currentMillis;
    // check if time is updated.
    if (time(nullptr) < 8 * 3600 * 2){
      // X.509 validation requires synchronization time
      ESP_LOGI(TAG, "Waiting time sync from ntp server for mqtt connection..");
      return;
    }
    // time is ok.
    // connecting to a mqtt broker
    ESP_LOGI(TAG, "MQTT broker connection attempt..");
    //-
    char client_id[50];
    sprintf(client_id, "ACHUB-%s", hub_device_serial);
    ESP_LOGI(TAG, "client id: %s", client_id);
    //- lastWill
    char lwill_msg[256];
    JsonDocument doc;
    doc["variable"] = "device_connection";
    doc["value"] = "disconnected";
    serializeJson(doc, lwill_msg, sizeof(lwill_msg));
    const uint8_t lwill_qos = 0;
    const bool lwill_retain = true;
    // Try mqtt connection to the broker.
    if (mqtt_client.connect(client_id, mqtt_username, mqtt_password, lwill_topic, lwill_qos, lwill_retain, lwill_msg, true))
    {
      ESP_LOGI(TAG, "Connected to MQTT broker!");
      ESP_LOGI(TAG, "Subscribing to mqtt topics:");
      //-
      mqtt_client.subscribe(settings_topic);
      ESP_LOGI(TAG, "settings topic ok");
      //-
      mqtt_client.subscribe(config_topic);
      ESP_LOGI(TAG, "config topic ok");
      //-
      mqtt_client.subscribe(opstate_topic);
      ESP_LOGI(TAG, "op-state topic ok");
      //-
      mqtt_client.subscribe(opsetpoint_topic);
      ESP_LOGI(TAG, "op-setpoint topic ok");
      //-
      mqtt_client.subscribe(peer_list_topic);
      ESP_LOGI(TAG, "peer-list topic ok");
      //-
      lastSaluteTime = millis();
      post_mqttconn_ntf = true; // flag to post connection message.
      ESP_LOGI(TAG, "MQTT connection done. **");

      return;
    }
    else
    {
      last_mqttconnection_err = mqtt_client.state();
      ESP_LOGE(TAG, "Fail MQTT Connection with state: %d", last_mqttconnection_err);
      lastMqttReconnect = currentMillis;
      ESP_LOGI(TAG, "new mqtt reconnect attempt in 10 seconds");
      return;
    }
  }
  return;
}

void clio_mqtt_loop(){
  //-
  //connect to the broker
  connectToMQTT();
  if (!mqtt_client.connected()) {return;}

  ntw_led_style = PULSE;  // pulse animation on mqtt broker connection.
  //mqtt loop 
  mqtt_client.loop();
  //-publish 'connected' message to lwill topic.
  post_mqttconnection_ntf();
  // send all the sensor data to the mqtt broker
  post_variables_to_broker();
  //sys update
  notify_state_update_to_broker();
}

esp_err_t clio_mqtt_setup() {

    mqttWiFiClient.setCACert(ca_cert); // mqtt broker ca-cert.
    bool buffer_set = mqtt_client.setBufferSize(CLIO_MQTT_BUFF_SIZE);
    if (!buffer_set) {
        ESP_LOGE(TAG, "could not set mqtt buffer size.");
        return ESP_ERR_NOT_SUPPORTED;
    }

    mqtt_client
        .setServer(mqtt_broker, mqtt_port)
        .setCallback(mqtt_message_callback);
        
    //---------------------------------------- update mqtt topics
    ESP_LOGI(TAG, "setting up the topics");
    sprintf(config_topic, "achub/operation/config/%s", hub_device_serial); // subscribe
    sprintf(settings_topic, "achub/operation/settings/%s", hub_device_serial); //subscribe
    sprintf(opstate_topic, "achub/operation/state/%s", hub_device_serial); // subscribe
    sprintf(opsetpoint_topic, "achub/operation/setpoint/%s", hub_device_serial); // subscribe
    sprintf(peer_list_topic, "achub/espnow/peer/%s", hub_device_serial); // subscribe
    sprintf(lwill_topic, "achub/connection/%s", hub_device_serial); //publish
    sprintf(post_data_topic, "achub/data/post/%s", hub_device_serial); //publish
    sprintf(incident_topic, "achub/incident/%s", hub_device_serial); //publish
    ESP_LOGI(TAG, "Mqtt config done.");

    return ESP_OK;
}