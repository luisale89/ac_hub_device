#include "clio_globals.h"
#include <esp_wifi.h>
#include <esp_log.h>
// #include <Arduino.h>
static const char *TAG = "CLIO-MAIN";
// cierre implementación de funciones

// Handler de tareas de lectura de sensores para nucleo 0 o 1
TaskHandle_t Task1;

// flags
static bool lastRadarState = false;
static bool publish_incident_flag = false;
static SysModeEnum lastPeersMode = COOL_MODE; // Para detectar el flanco de cambio
static FlowFlag autoModeFlag = FLAG_UNSET;

// Fault recovery variables
static int fault_recovery_attempts = 0;
static unsigned long first_fault_time = 0;       // Time of first recovery attempt in the current window
static unsigned long last_fault_time = 0;        // Time of last fault event
static unsigned long lastIncidentPubAttempt = 0; // Time when the last fault was posted to the broker

// Time Variables
static const unsigned long incidentPublishInterval = 60000UL;     // minimum time between posting consecutive faults to the broker, 1 minute.
static const unsigned long radarDebounceTime = 1000UL;            // 1 second rebound for radar sensor.
static const unsigned long buttonTimeOut = 3000UL;                // button pressed for 3seconds
static const unsigned long controllerInterval = 5000UL;           // delay between sensor updates, 5 seconds
static const unsigned long PRECENSE_RATE_CALC_INTERVAL = 60000UL; // interval to calculate presence rate, 1 minute.
static const unsigned long AUTO_MIN_DURATION = 600000UL;          // 10 minutos
static unsigned long lastControllerTime = 0;
static unsigned long lastButtonPress = 0;
static unsigned long lastRadarChange = 0;
static unsigned long emptyRoomTime = 0;
static unsigned long lastSampleTime = 0;
static unsigned long presenceDuration = 0;
static unsigned long lastPresenceRateCalculation = 0;
static unsigned long timeEnteredAuto = 0; // Time when the system entered AUTO_MODE, used to determine when to switch to COOL_MODE based on presence and time in auto mode.

// ### OPERATIONAL FUNCTIONS ###

void temp_setpoint_controller() // [OK]
{
  const unsigned long current = millis();
  const unsigned long AutoTimeOut = (unsigned long)system_config.auto_wait_time * 60000UL;
  const int auto_setpoint = system_config.auto_setpoint;
  const int user_setpoint = system_config.user_setpoint;

  switch (SysMode)
  {
  case AUTO_MODE:
  {
    // Actualización constante de tiempos de presencia/ausencia
    if (presence_rate > 0.30)
    {
      emptyRoomTime = current; // Reset si hay actividad
    }

    bool lowPresenceTimeElapsed = (current - emptyRoomTime >= AutoTimeOut);
    bool highPresence = (presence_rate > 0.50);

    // Máquina de estados interna basada en tu FlowFlag
    switch (autoModeFlag)
    {
    case FLAG_UNSET:
      // ¡CONFORT INMEDIATO AL ARRANQUE! No hay esperas
      activeSetpoint = user_setpoint;
      peersMode = COOL_MODE;
      autoModeFlag = FLAG_UP;
      ESP_LOGI(TAG, "system Temp. adjust = 'UserTemp' (Initial Confort Bypass)");
      break;

    case FLAG_UP: // confort mode
      // Evaluamos salida a ahorro por inactividad
      if (lowPresenceTimeElapsed)
      {
        activeSetpoint = auto_setpoint;
        peersMode = AUTO_MODE;
        autoModeFlag = FLAG_DOWN;
        ESP_LOGI(TAG, "system Temp. adjust = 'AutoTemp' (Inactivity Timeout -> Entering Grace Period)");

        // CAPTURAMOS EL MOMENTO EXACTO DE ENTRADA A AHORRO
        timeEnteredAuto = current;
      }
      else
      {
        activeSetpoint = user_setpoint;
        peersMode = COOL_MODE;
      }
      break;

    case FLAG_DOWN: // energy saving mode
      bool autoTimeoutElapsed = (current - timeEnteredAuto >= AUTO_MIN_DURATION);
      if (!autoTimeoutElapsed)
      {
        // Durante los 10 min de gracia, ignoramos presencia y forzamos ahorro
        activeSetpoint = auto_setpoint;
        peersMode = AUTO_MODE;
        // Quitamos el log de aquí para no inundar el puerto serie en cada loop,
        // o puedes usar un flag para imprimirlo una sola vez.
      }
      else
      {
        // Ya pasó el tiempo de gracia, ahora sí validamos si hay que volver a confort
        if (highPresence)
        {
          activeSetpoint = user_setpoint;
          peersMode = COOL_MODE;
          autoModeFlag = FLAG_UP; // Volvemos a confort
          ESP_LOGI(TAG, "system Temp. adjust = 'UserTemp' (High Presence Detected post-grace)");
        }
        else
        {
          // Mantenemos ahorro por defecto
          activeSetpoint = auto_setpoint;
          peersMode = AUTO_MODE;
        }
      }
      break;
    }
  }
  break;

  case COOL_MODE:
    activeSetpoint = user_setpoint;
    peersMode = COOL_MODE;
    autoModeFlag = FLAG_UP;
    ESP_LOGI(TAG, "system Temp. adjust = 'UserTemp'");
    break;

  case FAN_MODE:
    activeSetpoint = user_setpoint;
    peersMode = FAN_MODE;
    autoModeFlag = FLAG_UNSET;
    ESP_LOGI(TAG, "system Temp. adjust = 'UserTemp'");
    break;

  case ECO_MODE:
    activeSetpoint = auto_setpoint;
    peersMode = AUTO_MODE;
    autoModeFlag = FLAG_DOWN;
    ESP_LOGI(TAG, "system Temp. adjust = 'AutoTemp'");
    break;
  }
}

void system_sleep_controller() //[OK]
{
  if (!system_config.sleep_control_en)
  {
    ESP_LOGI(TAG, "- Time Control (timectrl) disabled by settings");
    sleep_flag = FLAG_UNSET;
    daySleepControl = false;
    return;
  }

  // -- utc to local time conversion --
  DateTime utc_now = DS3231_RTC.now();
  uint32_t vzla_epoch = utc_now.unixtime() - (4 * 3600); // Convert UTC to local time (UTC-4)
  DateTime now(vzla_epoch);

  const char *Day = Week_days[now.dayOfTheWeek()];
  char target_file[20];
  sprintf(target_file, "/%s.txt", Day);
  JsonDocument doc;

  const char *day_schedule = load_data_from_fs(target_file);
  DeserializationError error = deserializeJson(doc, day_schedule);

  if (error)
  {
    ESP_LOGE(TAG, "JSON Deserialization error code: %s", error.c_str());
    sleep_flag = FLAG_UNSET;
    daySleepControl = false;
    return;
  }

  const int WAKE_TIME = doc["wake_at"] | 759;    // 730
  const int SLEEP_TIME = doc["sleep_at"] | 1559; // 2130
  const SleepWakeCondition WAKE_CONDITION = doc["wake_condition"] | WAKE_ON_TIME;
  const SleepWakeCondition SLEEP_CONDITION = doc["sleep_condition"] | SLEEP_ON_ABSENCE;
  daySleepControl = doc["enabled"] | false;

  if (!daySleepControl)
  {
    sleep_flag = FLAG_UNSET;
    ESP_LOGI(TAG, "%s", "time control disabled for today");
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
        if (presence_rate > 0.30)
        {
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
        if (presence_rate <= 0.30)
        {
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

void calculate_presence_rate()
{
  unsigned long current_millis = millis();
  unsigned long elapsed_time = current_millis - lastSampleTime;
  lastSampleTime = current_millis;

  if (radarState)
  {
    presenceDuration += elapsed_time;
  }

  if (current_millis - lastPresenceRateCalculation >= PRECENSE_RATE_CALC_INTERVAL)
  {
    unsigned long actual_window_duration = current_millis - lastPresenceRateCalculation;
    // Evitamos división por cero en escenarios extremos de reinicio o desbordamiento
    if (actual_window_duration > 0)
    {
      // Calculate the presence rate as a float between 0.0 and 1.0
      presence_rate = (float)presenceDuration / (float)actual_window_duration;
    }
    else
    {
      presence_rate = 0.0;
    }

    // Cap the presence rate at 1.0 (100%)
    if (presence_rate > 1.0)
    {
      presence_rate = 1.0;
    }

    // Reset the presence duration and update the last calculation time
    presenceDuration = 0;
    lastPresenceRateCalculation = current_millis;
  }
}

void update_IO() //[ok]
{
  const unsigned long current_millis = millis();
  const bool currentRadarReading = digitalRead(RADAR) ? true : false; // input = 1 means movement detected
  const bool manuBtnPressed = digitalRead(MANUAL_BTN) ? false : true; // input = 0 means button pressed

  if (currentRadarReading != lastRadarState)
  {
    lastRadarChange = current_millis;
    lastRadarState = currentRadarReading;
  }

  // Lectura de sensor de movimiento.
  if (current_millis - lastRadarChange > radarDebounceTime)
  {
    radarState = lastRadarState;
    // radarState has been updated after radarDebounceTime period. this prevents false presence/absence readings.
  }

  calculate_presence_rate(); // calculate the presence rate based on the radar readings over time.

  // manual button
  if (!manuBtnPressed)
  { // button not pressed.
    lastButtonPress = current_millis;
  }

  // Press the button for 'buttonTimeOut' value. like a key stroke.
  if (manuBtnPressed && current_millis - lastButtonPress > buttonTimeOut)
  {
    ESP_LOGI(TAG, "Manual button has been pressed..");
    lastButtonPress = current_millis;

    if (SysFaultState == STATUS_ERROR)
    {
      ESP_LOGI(TAG, "System in FAULT state. Restarting fault condition first.");
      fault_restart_attempt_flag = true;
      return;
    }

    if (SysFaultState == STATUS_WARNING)
    {
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

void console_log()
{

  JsonDocument root;
  char doc[512];
  const unsigned long current_millis = millis();

  // logging
  root["sys_mode"] = SysMode;
  root["sys_state"] = SysState;
  root["presence"] = radarState;
  root["timectrl"] = system_config.sleep_control_en;
  root["user_sp"] = system_config.user_setpoint;
  root["active_sp"] = activeSetpoint;
  root["controller"] = controller_peer_online;
  root["monitor"] = monitor_peer_online;
  root["sleep_flag"] = sleep_flag;
  root["room_temp"] = room_temperature;
  root["cfc"] = system_alarms.controller_ac;
  root["mfc"] = system_alarms.monitor_ac;
  root["sstt"] = SysFaultState;
  root["frcv"] = fault_recovery_attempts;
  root["presence_rate"] = presence_rate;
  //- output
  serializeJsonPretty(root, doc);
  ESP_LOGI(TAG, "%s", doc);

  return;
}

bool is_alarm_code_cleared()
{

  if (system_alarms.controller_ac == AlarmCode::NORMAL && system_alarms.monitor_ac == AlarmCode::NORMAL)
  {
    return true;
  }

  return false;
}

void reset_system_fault()
{

  if (!fault_restart_attempt_flag)
  {
    return;
  }

  if (is_alarm_code_cleared())
  {
    // if the system is in a recovery attempt, but both monitor and controller report NORMAL, we can assume the recovery was successful and reset the fault state.
    ESP_LOGI(TAG, "Recovery attempt successful. Restarting system fault state.");
    fault_restart_attempt_flag = false;
    SysFaultState = STATUS_OK;
    save_operation_state_in_fs(); // save the new fault state in the filesystem immediately after a successful recovery, to persist this critical information.
  }
  return;
}

void update_fault_state()
{

  unsigned long current_time = millis();
  const unsigned long WINDOW_1_HOUR = 3600000UL; // 1 hour

  // Reset por tiempo (si pasó 1 hora sin bloquearse en error a pesar de haber detectado una falla,
  // se resetea el contador de intentos de recuperación para permitir nuevos intentos de recuperación ante nuevas fallas)
  if (fault_recovery_attempts > 0 && SysFaultState != STATUS_ERROR && (current_time - last_fault_time >= WINDOW_1_HOUR))
  {
    fault_recovery_attempts = 0;
    ESP_LOGI(TAG, "Recovery attempts counter has been reset after 1 hour of stable operation.");
  }

  if (SysFaultState != STATUS_OK)
  {
    // si aun no se ha recuperado de la falla detectada, no se actualiza el estado del sistema ante nuevas fallas
    // detectadas para evitar que el sistema entre en un ciclo de WARNING-ERROR-WARNING ante fallas recurrentes.
    return; // state already set
  }

  if (is_alarm_code_cleared())
  {
    // if there is no alarm code active in neither the monitor nor the controller, we assume there is no fault condition to report.
    return;
  }

  // TODO_ discriminar según el tipo de falla si se ira directamente al STATUS_ERROR
  // p.ej: Falla de sobrecorriente debería tener menos oportunidades de rearme.. o ir directo al ERROR.
  //  modificar fault_recovery_attempts para cambiar la permisividad de los rearmes según el código de falla.

  if (fault_recovery_attempts == 0)
  {
    first_fault_time = current_time;
  }

  fault_recovery_attempts++;
  last_fault_time = current_time;

  if (fault_recovery_attempts >= system_config.max_recovery_attempts)
  {
    // se alcanzó el número máximo de intentos de recuperación permitido,
    SysFaultState = STATUS_ERROR;
    ESP_LOGI(TAG, "!!! BLOQUEO PERMANENTE !!!");
    save_operation_state_in_fs(); // save the new fault state in the filesystem immediately after reaching the error state, to persist this critical information.
  }
  else
  {
    SysFaultState = STATUS_WARNING;
    ESP_LOGI(TAG, "Sistema en modo WARNING, esperando para recuperar...");
  }

  // post alarm to mqtt
  publish_incident_flag = true;

  // log fault event
  ESP_LOG_LEVEL(ESP_LOG_WARN, TAG, "Fault triggered:, Attempts: %d", fault_recovery_attempts);
  ESP_LOGI(TAG, "Fault details: Controller AC: %d, Monitor AC: %d", system_alarms.controller_ac, system_alarms.monitor_ac);

  return;
}

void fault_recovery_loop()
{

  unsigned long current_time = millis();
  const unsigned long RECOV_WINDOW = (unsigned long)system_config.recovery_window * 1000UL; // recovery window time in milliseconds, calculated from the value set in the system configuration.

  // new fault code handler
  update_fault_state();
  reset_system_fault();

  // Manejo de estados
  switch (SysFaultState)
  {
  case STATUS_WARNING:
    if (current_time - last_fault_time >= RECOV_WINDOW)
    {
      fault_restart_attempt_flag = true; // set flag to attempt recovery in the next loop.
    }
    break;

  case STATUS_ERROR:
    // wait for the reset signal from the user.
    break;

  case STATUS_OK:
    break;
  }

  if (publish_incident_flag && (current_time - lastIncidentPubAttempt >= incidentPublishInterval))
  {
    lastIncidentPubAttempt = current_time;

    incident_struct incident = {
        first_fault_time, last_fault_time, fault_recovery_attempts};

    const esp_err_t ret = publish_new_incident(&incident);

    if (ret == ESP_OK)
    {
      ESP_LOGI(TAG, "Incident published correctly to the broker");
      publish_incident_flag = false;
    }
    else
    {
      ESP_LOGE(TAG, "Error posting mqtt incident msg... retrying in the next cycle");
    }
  }
}

// ### TASK FUNCTIONS ###

// Hace la lectura de los sensores y la actualización de la interfáz gráfica.
void interface_controller(void *pvParameters)
{
  //-
  ESP_LOGI(TAG, "interface controller task running on core: %d", xPortGetCoreID());
  //-
  const TickType_t xDelay = pdMS_TO_TICKS(100); // 100ms
  for (;;)
  {
    // network led animation
    network_led_animation();
    // task delay
    vTaskDelay(xDelay);
  }
}

// -- Setup
void setup()
{
  Serial.begin(115200);
  esp_log_level_set("*", ESP_LOG_DEBUG); // set all TAGS on debug.
  ESP_LOGI(TAG, "** Hello!, System setup started... **");
  // pins definition
  // pinMode(BROKER_LED, OUTPUT);          // broker connection led.
  pinMode(NETWORK_LED, OUTPUT); // network connection led. using analogWrite
  pinMode(MANUAL_BTN, INPUT);   // remote board button.
  pinMode(RADAR, INPUT);        // sensor de presencia
  pinMode(AP_BTN, INPUT);       // Wifi Restart and configuration.

#ifndef ESP32
  while (!Serial)
    ; // wait for serial port to connect. Needed for native USB
#endif

  ESP_LOGI(TAG, "-> SETTING UP RTC SERVICE");
  if (clio_rtc_setup() != ESP_OK)
  {
    ESP_LOGE(TAG, "ERROR SETTING UP RTC SERVICE - stop");
    while (1)
    {
      ;
    }
  }
  ESP_LOGI(TAG, "RTC SERVICE OK");

  ESP_LOGI(TAG, "-> SETTING UP LittleFS SERVICE.");
  if (clio_spiffs_setup() != ESP_OK)
  {
    ESP_LOGE(TAG, "ERROR SETTING UP LittleFS SERVICE - stop");
    while (1)
    {
      ;
    }
  } //-
  ESP_LOGI(TAG, "LittleFS SERVICE OK.");

  // Inicio Sensores de temperatura
  ESP_LOGI(TAG, "-> SETTING UP DS18B20 SENSORS");
  clio_temp_sensors_setup();

  // load values from .txt files
  ESP_LOGI(TAG, "-> LOADING ALL DATA FROM FS AND INITIALIZING VARS");
  clio_fsdata_setup(); //-
  ESP_LOGI(TAG, "-> ALL DATA LOADED");

  //---------------------------------------- WifiSetup
  ESP_LOGI(TAG, "-> SETTING UP WIFI SERVICE");
  clio_wifi_setup();

  // ------- datetime from ntp server
  ESP_LOGI(TAG, "-> SETTING UP SNTP SERVICE");
  clio_sntp_setup();
  //---------------------------------------- set device identifiers
  uint8_t client_mac_address[6];
  esp_err_t ret = esp_wifi_get_mac(WIFI_IF_STA, client_mac_address);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "ERROR MAC ADDRESS INVALID.. IMPOSIBLE TO READ MAC ADDRESS FROM WIFI_IF_STA");
    while (1)
    {
      ;
    }
  };
  format_device_serial(client_mac_address, hub_device_serial);
  ESP_LOGD(TAG, "-->> DEVICE SERIAL: %s", hub_device_serial);
  //- set ap-ssid value
  // snprintf(AP_SSID, sizeof(AP_SSID), "CLIO-%s", hub_device_serial);
  format_ap_ssid(client_mac_address, AP_SSID);
  ESP_LOGD(TAG, "-->> AP SSID: %s", AP_SSID);
  //---------------------------------------- esp_now settings
  ESP_LOGI(TAG, "-> SETTING UP ESP_NOW SERVICE");
  if (clio_espnow_cnf() != ESP_OK)
  {
    ESP_LOGE(TAG, "ERROR SETTING UP ESP-NOW, STOP");
    while (1)
    {
      ;
    }
  };
  ESP_LOGI(TAG, "ESP_NOW SERVICE OK!");
  //---------------------------------------- mqtt settings
  ESP_LOGI(TAG, "-> SETTING UP MQTT SERVICE");
  if (clio_mqtt_setup() != ESP_OK)
  {
    ESP_LOGE(TAG, "ERROR INITIALIZING MQTT SERVICE - STOP");
    while (1)
    {
      ;
    }
  }
  ESP_LOGI(TAG, "MQTT SERVICE OK");

  // Crea la tarea del controlador de interfaz.
  ESP_LOGI(TAG, "Creating FREERTOS task...");
  xTaskCreatePinnedToCore(
      interface_controller, /* Function to implement the task */
      "Task1",              /* Name of the task */
      10000,                /* Stack size in words */
      NULL,                 /* Task input parameter */
      0,                    /* Priority of the task */
      &Task1,               /* Task handle. */
      0);                   /* Core */

  //---------------------------------------- end of setup ---

  // Inicialización crítica de tiempos para las métricas
  lastSampleTime = millis();
  lastPresenceRateCalculation = millis();

  ESP_LOGI(TAG, "** SETUP COMPLETED **");
}

void loop()
{
  // main loop.
  clio_wifi_loop();
  clio_espnow_loop();
  clio_mqtt_loop();
  clio_temp_sensors_loop();
  time_counter_loop();
  fault_recovery_loop();
  update_IO();

  if (millis() - lastControllerTime > controllerInterval)
  {
    lastControllerTime = millis(); // update time var
    system_sleep_controller();     // Funcion que controla el apagado y encendido automatico (Sleep)
    temp_setpoint_controller();    // Funcion que regula latemperatura segun el modo (Cool, auto, fan)
    // console_log();                 // system log variables.
  }
  // delay
  delay(20);
}