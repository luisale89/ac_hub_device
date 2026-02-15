#include "clio_globals.h"
#include <esp_wifi.h>
#include <esp_system.h>
// #include <Arduino.h>
static const char* TAG = "CLIO-MAIN";

// Handler de tareas de lectura de sensores para nucleo 0 o 1
TaskHandle_t Task1;

// flags
// TODO: Declare volatile type for global variables
static bool lastRadarState = false;
static bool cooling_relay_state = false;
static bool fan_relay_state = false;
static bool monitor_compressor_state = false;
static bool post_alarm_flag = false;

// Fault recovery variables
static int fault_recovery_attempts = 0;
static unsigned long first_fault_time = 0; // Time of first recovery attempt in the current window
static unsigned long last_fault_time = 0; // Time of last fault event

// Time Variables
static const unsigned long radarDebounceTime = 1L * 30000L;     // 30 seconds rebound for radar sensor.
static const unsigned long buttonTimeOut = 3L * 1000L;          // button pressed for 3seconds
static const unsigned long controllerInterval = 1L * 5000L;     // delay between sensor updates, 5 seconds
static unsigned long lastControllerTime = 0;
static unsigned long lastButtonPress = 0;
static unsigned long lastRadarChange = 0;
static unsigned long radarStateTime = 0;

// ### OPERATIONAL FUNCTIONS ###

void save_operation_state_in_fs() //[OK] [OK]
{

  char op_state_buffer[128];
  JsonDocument doc;

  switch (SysState)
  {
  case SYSTEM_ON:
    doc["sys_state"] = "on";
    // save_data_in_fs("on", fileName);
    break;
  
  case SYSTEM_OFF:
    doc["sys_state"] = "off";
    // save_data_in_fs("off", fileName);
    break;

  case SYSTEM_SLEEP:
    doc["sys_state"] = "sleep";
    // save_data_in_fs("sleep", fileName);
    break;
  }

  serializeJson(doc, op_state_buffer, sizeof(op_state_buffer));
  save_data_in_fs(op_state_buffer, "/Estado.txt");
  return;
}

void temp_setpoint_controller() // [OK]
{
  const unsigned long current = millis();
  const unsigned long AutoTimeOut = (unsigned long)system_config.auto_wait_time * 60000L;
  const int auto_setpoint = system_config.auto_setpoint;
  const int user_setpoint = system_config.user_setpoint;

  switch (SysMode)
  {
  case AUTO_MODE:
    if (radarState) { // restart the counter if the radar state is true (movement detection)
      radarStateTime = current;
    }
    if (current - radarStateTime > AutoTimeOut) {
      activeSetpoint = auto_setpoint;
      peersMode = AUTO_MODE;
      ESP_LOGI(TAG, "system Temp. adjust = 'AutoTemp'");
    } else {
      activeSetpoint = user_setpoint;
      peersMode = COOL_MODE;
      ESP_LOGI(TAG, "system Temp. adjust = 'UserTemp'");
    }
    break;

  case COOL_MODE:
    activeSetpoint = user_setpoint;
    peersMode = COOL_MODE;
    ESP_LOGI(TAG, "system Temp. adjust = 'UserTemp'");
    break;

  case FAN_MODE:
    activeSetpoint = user_setpoint;
    peersMode = FAN_MODE;
    ESP_LOGI(TAG, "system Temp. adjust = 'UserTemp'");
    break;

  }
}

void sleep_state_controller() //[OK]
{
  if (!system_config.sleep_control_enabled)
  {
    ESP_LOGI(TAG, "- Time Control (timectrl) disabled by settings");
    sleep_flag = FLAG_UNSET;
    daySleepControl = false;
    return;
  }

  DateTime now = DS3231_RTC.now();
  const char* Day = Week_days[now.dayOfTheWeek()];
  char target_file[20];
  sprintf(target_file, "/%s.txt", Day);
  JsonDocument doc;

  const char* day_schedule = load_data_from_fs(target_file);
  DeserializationError error = deserializeJson(doc, day_schedule);

  if (error)
  {
    ESP_LOGE(TAG, "JSON Deserialization error code: %s", error.c_str());
    return;
  }

  const int WAKE_TIME = doc["wake_at"] | 759;   // 730
  const int SLEEP_TIME = doc["sleep_at"] | 1559; // 2130
  const SleepWakeCondition WAKE_CONDITION = doc["wake_condition"] | WAKE_ON_TIME;
  const SleepWakeCondition SLEEP_CONDITION = doc["sleep_condition"] | SLEEP_ON_ABSENCE;
  daySleepControl = doc["enabled"] | false;
  
  if (!daySleepControl)
  {
    sleep_flag = FLAG_UNSET;
    ESP_LOGD(TAG, "%s", "time control disabled for today");
    return;
  }

  char hora[3];
  sprintf(hora, "%d", now.hour());
  int Min = int(now.minute());
  char minuto[3];
  if (Min < 10)
  {
    sprintf(minuto, "0%d", Min);
  }
  else
  {
    sprintf(minuto, "%d", Min);
  }
  char tiempo[5];
  sprintf(tiempo, "%s%s", hora, minuto);
  int tiempo_int = atoi(tiempo);
  ESP_LOGD(TAG, "Current Time: %s", tiempo);

  if (SLEEP_TIME > tiempo_int && WAKE_TIME < tiempo_int) // horario de encendido...
  {
    // Si el sleep está activado, o si es la primera verificación después del arranque..
    if (sleep_flag == FLAG_UP || sleep_flag == FLAG_UNSET)
    {
      switch (WAKE_CONDITION)
      {
      case WAKE_ON_PRESENCE:
        if (radarState) {
          sleep_flag = FLAG_DOWN;
          SysState = SYSTEM_ON;
        }
        break;
      
      case WAKE_ON_TIME:
        sleep_flag = FLAG_DOWN;
        SysState = SYSTEM_ON;
        break;
      }
    }
  }
  else
  { // horario del sleep.
    // si el sleep está apagado, o si es la primera verificación después del arranque..
    if (sleep_flag == FLAG_DOWN || sleep_flag == FLAG_UNSET)
    {
      switch (SLEEP_CONDITION)
      {
      case SLEEP_ON_ABSENCE:
        if(!radarState){
          sleep_flag = FLAG_UP;
          SysState = SYSTEM_SLEEP;
        }
        break;
      
      case SLEEP_ON_TIME:
        sleep_flag = FLAG_UP;
        SysState = SYSTEM_SLEEP;
        break;
      }
    }
  }
  return;
}

void update_IO() //[ok]
{
  const unsigned long current_millis = millis();
  const bool currentRadarReading = digitalRead(RADAR);
  const bool manuBtnPressed = digitalRead(MANUAL_BTN) ? false : true; // input = 0 means button pressed

  if (currentRadarReading != lastRadarState) {
    lastRadarChange = millis();
    lastRadarState = currentRadarReading;
  }

  // Lectura de sensor de movimiento.
  if (current_millis - lastRadarChange > radarDebounceTime) {
    radarState = lastRadarState;
    // radarState has been updated after radarDebounceTime period. this prevents false presence/absence readings.
  }

  //manual button
  if (!manuBtnPressed) { // button not pressed.
    lastButtonPress = current_millis;
  }

  // Press the button for 'buttonTimeOut' value. like a key stroke.
  if (manuBtnPressed && current_millis - lastButtonPress > buttonTimeOut)
  {
    ESP_LOGI(TAG, "Manual button has been pressed..");
    lastButtonPress = current_millis;

    if (SysFaultState == STATUS_ERROR) {
      ESP_LOGI(TAG, "System in FAULT state. Restarting fault condition first.");
      fault_reset_flag = true;
      return;
    }

    if (SysFaultState == STATUS_WARNING) {
      ESP_LOGI(TAG, "System in WARNING state. Ignoring manual button press.");
      return;
    }

    switch (SysState)
    {
    case SYSTEM_ON:
      ESP_LOGI(TAG, "- Turning off the system.");
      SysState = SYSTEM_OFF;
      break;

    case SYSTEM_OFF:
      ESP_LOGI(TAG, "Turning on the system.");
      SysState = SYSTEM_ON;
      break;
    
    case SYSTEM_SLEEP:
      ESP_LOGI(TAG, "imposible to turn on the system on sleep mode.");
      break;
    }
  }
}

void print_vars_in_serial() {

  JsonDocument root;
  char doc[256];
  const unsigned long current_millis = millis();
  
  // logging
  root["sys_mode"] = SysMode;
  root["sys_state"] = SysState;
  root["presence"] = radarState;
  root["timectrl"] = system_config.sleep_control_enabled;
  root["user_sp"] = system_config.user_setpoint;
  root["active_sp"] = activeSetpoint;
  root["controller"] = controller_peer_online;
  root["monitor"] = monitor_peer_online;
  root["sleep_flag"] = sleep_flag;
  root["board_temp"] = case_pcb_temperature;
  root["heap"] = esp_get_free_heap_size();
  root["min_heap"] = esp_get_minimum_free_heap_size();
  //- output
  serializeJson(root, doc);
  ESP_LOGD(TAG, "%s", doc);

  return;
}

void check_for_updates() {
  // send variables when important changes happen.
  if (controller_peer_online) {
    if (cooling_relay_state != controller_data.cooling_relay) {
      cooling_relay_state = controller_data.cooling_relay;
      postVariablesToBroker = true; // set flag to post variables.
    }
  }

  if (monitor_peer_online) {
    // check compressor state change.
    if (monitor_compressor_state != monitor_data.compressor_state) {
      monitor_compressor_state = monitor_data.compressor_state;
      postVariablesToBroker = true;
    }
  }

  // if there is a sysState change
  if (PrevSysState != SysState)
  {
    ESP_LOGI(TAG, "System state has changed. sending updates.");
    SysStateBuffer = PrevSysState;
    PrevSysState = SysState; // assign PrevSysState the current SysState
    save_operation_state_in_fs();
    postMqttStateUpdate = true; // post state update.
    postVariablesToBroker = true; // update variables to broker.
  }
 
  return;
}

AlarmCode calculate_alarm_code() //[OK]
{
  // Revisa las variables del monitor y devuelve el código de falla correspondiente.

  // COMPRESSOR STALL CHECK
  if (controller_peer_online == true && monitor_peer_online == true) {
    if (controller_data.cooling_relay 
      && controller_data.seconds_since_last_cooling_rq > 360 
      && monitor_data.compressor_state != true) {
      // the compressor should be running after 6 minutes of cooling request but is not.
      return COMPRESSOR_STALL;
    }
  }

  // MONITOR FAULT CODES CHECK SEQUENCE
  if (monitor_peer_online == true) {
    if (monitor_data.compressor_current > system_config.comp_nominal_amp + system_config.comp_amp_threshold) {
      return HIGH_COMP_CURRENT;
    }
  
    if (monitor_data.discharge_temp > system_config.discharge_max_t) {
      return HIGH_DISCHARGE_TEMP;
    }
  
    if (monitor_data.high_pressure < 1) { // high pressure threshold (0,5V = open switch)
      return HIGH_PRESSURE_SWITCH;
    }

    if (monitor_data.liquid_temp > system_config.liquid_max_t) {
      return HIGH_LIQUID_TEMP;
    }
    
    if (monitor_data.low_pressure < 1) { // low pressure threshold (0,5V = open switch)
      return LOW_PRESSURE_SWITCH;
    }

    if (monitor_data.vapor_temp < system_config.vapor_line_min_t) {
      return LOW_VAPOR_TEMP;
    }

    if (monitor_data.ambient_temp > system_config.exterior_max_t) {
      return HIGH_EXTERIOR_TEMP;
    }
  }

  // CONTROLLER FAULT CODES CHECK SEQUENCE
  if (controller_peer_online == true) {
    if (controller_data.drain_switch == false) { // drain switch open
      return DRAIN_SWITCH_OPEN;
    }
  }

  return NORMAL;
}

void reset_system_fault() {

  if (!fault_reset_flag) {
    return;
  }

  fault_reset_flag = false;
  SysFaultState = STATUS_OK;
  fault_recovery_attempts = 0;
  first_fault_time = 0;
  ESP_LOGI(TAG, "System reset from fault state. Resuming normal operation.");
}

void update_fault_state(AlarmCode fault_code) {

  const unsigned long WINDOW_1_HOUR = 3600000L; // 1 hour
  unsigned long current_time = millis();

  // Reset por tiempo (si pasó 1 hora sin bloquearse)
  if (fault_recovery_attempts > 0 
    && SysFaultState != STATUS_ERROR 
    && (current_time - first_fault_time >= WINDOW_1_HOUR)) {
    fault_recovery_attempts = 0;
    ESP_LOGI(TAG, "Recovery attempts counter reset after 1 hour of stable operation.");
  }
  
  if (SysFaultState != STATUS_OK) {
    // if the recovery of the fault not yet happens.
    return; // state already set
  }

  if (fault_code == NORMAL) {
    return; // no fault code detected, waiting for new alarm
  }

  if (fault_recovery_attempts == 0) {
    first_fault_time = current_time;
  }

  fault_recovery_attempts++;
  last_fault_time = current_time;

  if (fault_recovery_attempts >= system_config.max_recovery_attempts) {
    SysFaultState = STATUS_ERROR;
    ESP_LOGI(TAG, "!!! BLOQUEO PERMANENTE !!!");

  } else {
    SysFaultState = STATUS_WARNING;
    ESP_LOGI(TAG, "Sistema en modo WARNING, esperando para recuperar...");
  }
  // post alarm to mqtt
  post_alarm_flag = true;

  // log fault event
  ESP_LOG_LEVEL(ESP_LOG_WARN, TAG, "Fault triggered: Code %d, Attempts: %d", fault_code, fault_recovery_attempts);

}

void fault_recovery_controller() {

  unsigned long current_time = millis();
  const unsigned long RECOV_WINDOW = (unsigned long)system_config.recovery_window * 60000L;

  // fault code calculation
  AlarmCode fault_code = calculate_alarm_code();
  // new fault code handler
  update_fault_state(fault_code);

  if (post_alarm_flag) {
    incident_struct incident = {
      fault_code, first_fault_time, last_fault_time, fault_recovery_attempts
    };
    if (post_incident_to_broker(&incident) == ESP_OK) {
      post_alarm_flag = false;
      ESP_LOGI(TAG, "Incident posted to mqtt broker");
    }
  }

  // Manejo de estados
  switch (SysFaultState) {
    case STATUS_WARNING:
      if (current_time - last_fault_time >= RECOV_WINDOW) {
        SysFaultState = STATUS_OK; // try to recover from warning stt
        ESP_LOGI(TAG, "System recovered from WARNING state.");
      }
      break;

    case STATUS_ERROR:
      // wait for the reset signal.
      reset_system_fault();
      break;

    case STATUS_OK:
      break;
  }
}

// ### TASK FUNCTIONS ###

// Hace la lectura de los sensores y la actualización de la interfáz gráfica.
void sensors_and_interface_controller(void *pvParameters)
{
  //-
  ESP_LOGI(TAG, "task in second core...");
  //-
  const TickType_t xDelay = pdMS_TO_TICKS(50); // 50ms
  for (;;)
  { 
    //lee temperatura desde los sensores
    update_case_temperature();
    //network led animation
    network_led_animation(ntw_led_style);
    // task delay
    vTaskDelay(xDelay);
  }
}

// -- Setup
void setup()
{
  Serial.begin(115200);
  esp_log_level_set("*", ESP_LOG_DEBUG); //set all TAGS on debug.
  ESP_LOGI(TAG, "** Hello!, System setup started... **");
  // pins definition
  // pinMode(BROKER_LED, OUTPUT);          // broker connection led.
  pinMode(NETWORK_LED, OUTPUT);         // network connection led. using analogWrite
  pinMode(MANUAL_BTN, INPUT); // remote board button.
  pinMode(RADAR, INPUT);  // sensor de presencia
  pinMode(AP_BTN, INPUT);            // Wifi Restart and configuration.

#ifndef ESP32
  while (!Serial)
    ; // wait for serial port to connect. Needed for native USB
#endif

  ESP_LOGI(TAG, "-> SETTING UP RTC SERVICE");
  if (clio_rtc_setup() != ESP_OK) {
    ESP_LOGE(TAG, "ERROR SETTING UP RTC SERVICE - stop");
    while(1){;}
  }
  ESP_LOGI(TAG, "RTC SERVICE OK");

  ESP_LOGI(TAG, "-> SETTING UP LittleFS SERVICE.");
  if (clio_spiffs_setup() != ESP_OK){
    ESP_LOGE(TAG, "ERROR SETTING UP LittleFS SERVICE - stop");
    while(1){;}
  } //-
  ESP_LOGI(TAG, "LittleFS SERVICE OK.");

  // Inicio Sensores de temperatura
  ESP_LOGI(TAG, "-> SETTING UP DS18B20 SENSORS");
  clio_case_tempsensor_setup();

  // load values from .txt files
  ESP_LOGI(TAG, "-> LOADING ALL DATA FROM FS AND INITIALIZING VARS");
  clio_fsdata_setup(); //-
  ESP_LOGI(TAG, "-> ALL DATA LOADED");

  // ------- datetime from ntp server
  ESP_LOGI(TAG, "-> SETTING UP SNTP SERVICE");
  clio_sntp_setup();
  //---------------------------------------- set device identifiers
  uint8_t client_mac_address[6];
  esp_err_t ret = esp_wifi_get_mac(WIFI_IF_STA, client_mac_address);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "ERROR MAC ADDRESS INVALID.. IMPOSIBLE TO READ MAC ADDRESS FROM WIFI_IF_STA");
    while(1){;}
  };
  strcpy(hub_device_serial, print_device_serial(client_mac_address));
  ESP_LOGD(TAG, "-->> DEVICE SERIAL: %s", hub_device_serial);
    //- set ap-ssid value
  snprintf(AP_SSID, sizeof(AP_SSID), "CLIO-%s", hub_device_serial);
  ESP_LOGD(TAG, "-->> AP SSID: %s", AP_SSID);
  //---------------------------------------- WifiSetup
  ESP_LOGI(TAG, "-> SETTING UP WIFI SERVICE");
  clio_wifi_setup();
  //---------------------------------------- esp_now settings
  ESP_LOGI(TAG, "-> SETTING UP ESP_NOW SERVICE");
  if (clio_espnow_cnf() != ESP_OK){
    ESP_LOGE(TAG, "ERROR SETTING UP ESP-NOW, STOP");
    while(1){;}
  };
  ESP_LOGI(TAG, "ESP_NOW SERVICE OK!");
  //---------------------------------------- mqtt settings
  ESP_LOGI(TAG, "-> SETTING UP MQTT SERVICE");
  if (clio_mqtt_setup() != ESP_OK) {
    ESP_LOGE(TAG, "ERROR INITIALIZING MQTT SERVICE - STOP");
    while(1){;}
  }
  ESP_LOGI(TAG, "MQTT SERVICE OK");

  // Crea la tarea de lectura de sensores en el segundo procesador.
  ESP_LOGI(TAG, "Creating FREERTOS task...");
  xTaskCreatePinnedToCore(
      sensors_and_interface_controller, /* Function to implement the task */
      "Task1",     /* Name of the task */
      10000,       /* Stack size in words */
      NULL,        /* Task input parameter */
      0,           /* Priority of the task */
      &Task1,      /* Task handle. */
      0);          /* Core */

  //---------------------------------------- end of setup ---
  ESP_LOGI(TAG, "** SETUP COMPLETED **");
}

void loop()
{
  //main loop.
  clio_wifi_loop();
  clio_espnow_loop();
  clio_mqtt_loop();
  check_for_updates();
  update_time_counter();
  update_IO();
  fault_recovery_controller();

  if (millis() - lastControllerTime > controllerInterval)
  {
    sleep_state_controller(); // Funcion que controla el apagado y encendido automatico (Sleep)
    temp_setpoint_controller(); // Funcion que regula latemperatura segun el modo (Cool, auto, fan)
    print_vars_in_serial(); // system log variables.
    lastControllerTime = millis(); // update time var
  }
  //delay
  delay(10);
}