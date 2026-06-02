#include "clio_globals.h"
#include <esp_wifi.h>
#include <dtf_ota.h>

static const char *TAG = "CLIO-WIFI";
static bool last_ap_btn_state = false;
static bool ap_btn_state = false;
static bool ap_btn_handled = false;
static bool wiFiReconnectFlag = false;
static bool first_ota_check_done = false;
static int wiFiReconnectAttempt = 0;
static const int MAX_WIFI_RECONNECT_ATT = 33;
static unsigned long lastWifiDisconnect = 0;
static const unsigned long WIFI_RECONNECT_BACKOFF = 5000UL; // 5 seconds backoff between reconnect attempts
static unsigned long lastApBtnChange = 0;
static unsigned long lastWifiReconnect = 0;
static unsigned long lastOtaCheck = 0;
static const unsigned long wifiReconnectInterval = 5 * 60000UL; // 5 minutos para intentar reconectar al wifi.
static const unsigned long BTN_DEBOUNCE_TIME = 250UL;           // 250ms rebound time constant;
static const unsigned long AP_BTN_LONG_PRESS_TIME = 3000UL;     // 3 segundos para considerar una pulsación larga en el botón de AP.
static const unsigned long OTA_CHECK_INTERVAL = 30 * 60000UL;   // 30 minutos entre cada verificación de OTA.
static const int8_t VZLA_OFFSET = -4;
static const int8_t OTA_WINDOW_START = 2; // 2 AM
static const int8_t OTA_WINDOW_END = 4;   // 4 AM
static int OTA_CHECK_COUNT = 0;

bool is_ota_permitted()
{
  if (!first_ota_check_done)
  {
    first_ota_check_done = true; // Marcar que ya se hizo el primer chequeo
    ESP_LOGI(TAG, "Realizando primer chequeo de OTA sin esperar intervalo");
    return true; // Permitir el primer chequeo inmediatamente después del arranque
  }

  // 1. Prioridad: Seguridad del sistema.
  if (SysState == SYSTEM_ON)
  {
    return false;
  }

  const unsigned long currentMillis = millis();
  bool time_completed = (currentMillis - lastOtaCheck >= OTA_CHECK_INTERVAL);
  if (!time_completed)
  {
    return false; // No es momento de chequear OTA
  }
  lastOtaCheck = currentMillis;

  //- check window time for OTA, only allow OTA check if we are in the desired time window.
  const bool rtc_lost_power = DS3231_RTC.lostPower();
  if (rtc_lost_power)
  {
    ESP_LOGW(TAG, "[OTA] RTC lost power, skipping OTA check.");
    return false;
  }

  DateTime now = DS3231_RTC.now();
  int8_t utc_hour = now.hour();
  int8_t local_hour = utc_hour + VZLA_OFFSET; // hora local en Venezuela (UTC - 4)

  local_hour = (local_hour + 24) % 24; // Asegura que local_hour esté entre 0 y 23

  // Ventana deseada en VZLA: 02:00 a 04:00
  // Esto equivale en UTC a: 06:00 a 8:00
  if (local_hour >= OTA_WINDOW_START && local_hour <= OTA_WINDOW_END)
  {
    OTA_CHECK_COUNT++;
    return true;
  }

  OTA_CHECK_COUNT = 0; // Reiniciar contador si no estamos en la ventana
  return false;
}

void check_for_ota_update()
{
  //- check if ota rq is available based on time window and system state.
  if (!is_ota_permitted())
  {
    return;
  }

  if (OTA_CHECK_COUNT >= 2)
  {
    ESP_LOGI(TAG, "Ya se ha verificado OTA %d veces en esta ventana, esperando la próxima ventana...", OTA_CHECK_COUNT);
    return; // Evitar múltiples verificaciones dentro de la misma ventana
  }

  ESP_LOGI(TAG, "Iniciando OTA Update con Deploy the Fleet...");

  //-- Configuración para la actualización OTA utilizando DTF
  const dtf_ota_cfg_t cfg = {
      .product_id = CLIO_DTF_PID,
      .custom_version = FIRMWARE_VERSION,
      .reboot_option = DTF_NO_REBOOT, // No reiniciar automáticamente después de la actualización
  };

  DTF_OtaResponse ret = dtf_get_firmware_update(&cfg);

  switch (ret)
  {
  case DTFOTA_NewFirmwareFlashed:
    ESP_LOGI(TAG, "New firmware flashed successfully! Rebooting...");
    delay(500);    // Esperar un momento para asegurar que el mensaje se envíe antes de reiniciar
    esp_restart(); // Reiniciar el dispositivo para cargar el nuevo firmware
    break;
  case DTFOTA_InvalidFirmwareImage:
    ESP_LOGW(TAG, "No valid firmware image found on the server.");
    break;
  case DTFOTA_NotEnoughMemory:
    ESP_LOGE(TAG, "Not enough memory to perform OTA update.");
    break;
  case DTFOTA_FirmwareWriteFailed:
    ESP_LOGE(TAG, "Firmware write failed during OTA update.");
    break;
  case DTFOTA_NoUpdatesAvailable:
    ESP_LOGI(TAG, "No new firmware updates available.");
    break;
  case DTFOTA_CertValidationFailed:
    ESP_LOGE(TAG, "Certificate validation failed during OTA update.");
    break;
  default:
    ESP_LOGE(TAG, "Unknown error occurred during OTA update: %d", ret);
    break;
  }
}

// ### WIFI EVENT HANDLER ###
void set_station_for_espnow_offline_mode()
{
  // this function allows offline esp-now communication, in case of getting disconnected from the WiFi network.
  ntw_led_style = BLINK_05X; // LED slow blink
  ESP_LOGI(TAG, "[wifi] Setting up to communicate over esp-now disconnected from the router.");
  const int currentChannel = WiFi.channel();
  ESP_LOGI(TAG, "[wifi] channel: %d", currentChannel);
  espnow_connection_state = ESPNOW_OFFLINE;

  // Stop automatic station reconnection and keep the current WiFi channel fixed.
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, false);
  esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);

  lastWifiReconnect = millis(); // set timer for router reconnect logic
  wiFiReconnectFlag = false;
  ESP_LOGI(TAG, "[wifi] ESP-NOW offline mode set. Will attempt to reconnect to router every %d minutes.", wifiReconnectInterval / 60000);
  return;
}

void WiFiEvent(arduino_event_t *wifi_event)
{
  ESP_LOGI(TAG, "[wifi] event code: %d", wifi_event->event_id);
  switch (wifi_event->event_id)
  {
  case ARDUINO_EVENT_WIFI_READY:
    ESP_LOGI(TAG, "[wifi] interface ready");
    break;
  case ARDUINO_EVENT_WIFI_SCAN_DONE:
    ESP_LOGI(TAG, "[wifi] Completed scan for access points");
    break;
  case ARDUINO_EVENT_WIFI_STA_START:
    ESP_LOGI(TAG, "[wifi] Client started");
    ntw_led_style = BLINK; // start blinking when the station starts, will be turned off when connected to the router.
    break;
  case ARDUINO_EVENT_WIFI_STA_STOP:
    ESP_LOGI(TAG, "[wifi] Clients stopped");
    break;
  case ARDUINO_EVENT_WIFI_STA_CONNECTED:
    ESP_LOGI(TAG, "[wifi] Connected to the AP");
    break;

  case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
    if (wiFiReconnectAttempt >= MAX_WIFI_RECONNECT_ATT)
    {
      ESP_LOGI(TAG, "[wifi] reached max_reconnect_attempts.");
      set_station_for_espnow_offline_mode();
      break;
    }

    if (millis() - lastWifiDisconnect < WIFI_RECONNECT_BACKOFF)
    {
      break;
    }

    lastWifiDisconnect = millis();
    ntw_led_style = BLINK;
    wiFiReconnectAttempt++;
    ESP_LOGI(TAG, "[wifi] Disconnected from WiFi Access Point");
    ESP_LOGI(TAG, "lost connection. Reason code: %d", wifi_event->event_info.wifi_sta_disconnected.reason);
    ESP_LOGI(TAG, "Reconnect attempt # %d..", wiFiReconnectAttempt);
    espnow_connection_state = ESPNOW_IDLE;
    WiFi.reconnect();
    break;

  case ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE:
    ESP_LOGI(TAG, "[wifi] Authentication mode of access point has changed");
    break;
  case ARDUINO_EVENT_WIFI_STA_GOT_IP:
    WiFi.setAutoReconnect(true);
    espnow_connection_state = ESPNOW_ONLINE;
    wiFiReconnectAttempt = 0;
    ESP_LOGI(TAG, "Router connection completed!! -> channel: %d", WiFi.channel());
    ESP_LOGI(TAG, "GOT IP Address: %s", WiFi.localIP().toString());
    ESP_LOGI(TAG, "RSSI: %d", WiFi.RSSI());
    ESP_LOGI(TAG, "STA MAC Address: %s", WiFi.macAddress().c_str());
    ESP_LOGI(TAG, "SOFT AP MAC Address: %s", WiFi.softAPmacAddress().c_str());
    break;

  case ARDUINO_EVENT_WIFI_STA_LOST_IP:
    ESP_LOGI(TAG, "[wifi] Lost IP address and IP address is reset to 0");
    break;
  case ARDUINO_EVENT_WPS_ER_SUCCESS:
    ESP_LOGI(TAG, "[wifi] Protected Setup (WPS): succeeded in enrollee mode");
    break;
  case ARDUINO_EVENT_WPS_ER_FAILED:
    ESP_LOGI(TAG, "[wifi] Protected Setup (WPS): failed in enrollee mode");
    break;
  case ARDUINO_EVENT_WPS_ER_TIMEOUT:
    ESP_LOGI(TAG, "[wifi] Protected Setup (WPS): timeout in enrollee mode");
    break;
  case ARDUINO_EVENT_WPS_ER_PIN:
    ESP_LOGI(TAG, "[wifi] Protected Setup (WPS): pin code in enrollee mode");
    break;
  case ARDUINO_EVENT_WIFI_AP_START:
    ESP_LOGI(TAG, "[wifi] access point started");
    break;
  case ARDUINO_EVENT_WIFI_AP_STOP:
    ESP_LOGI(TAG, "[wifi] access point stopped");
    break;
  case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
    ESP_LOGI(TAG, "[wifi] Client connected");
    break;
  case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
    ESP_LOGI(TAG, "[wifi] Client disconnected");
    break;
  case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
    ESP_LOGI(TAG, "[wifi] Assigned IP address to client");
    break;
  case ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED:
    ESP_LOGI(TAG, "[wifi] Received probe request");
    break;
  case ARDUINO_EVENT_WIFI_AP_GOT_IP6:
    ESP_LOGI(TAG, "[wifi] AP IPv6 is preferred");
    break;
  case ARDUINO_EVENT_WIFI_STA_GOT_IP6:
    ESP_LOGI(TAG, "[wifi] STA IPv6 is preferred");
    break;
  default:
    break;
  }
}

void clio_wifi_loop() //[ok] - Mejorado con manejo de errores y reconexiones
{
  const unsigned long currentMillis = millis();
  // Solo esperar SmartConfig si el botón AP está presionado
  const bool current_ap_btn = digitalRead(AP_BTN) ? false : true;
  if (current_ap_btn != last_ap_btn_state)
  {
    last_ap_btn_state = current_ap_btn;
    lastApBtnChange = currentMillis;
    if (!current_ap_btn)
    {
      ap_btn_handled = false; // reset when button is released
    }
  }

  // button debouncer
  if (currentMillis - lastApBtnChange >= BTN_DEBOUNCE_TIME)
  {
    // btn change
    ap_btn_state = current_ap_btn;
  }

  // button pressed for >3 seconds.
  if (ap_btn_state && !ap_btn_handled && currentMillis - lastApBtnChange >= AP_BTN_LONG_PRESS_TIME)
  {
    ap_btn_handled = true;
    lastApBtnChange = currentMillis;
    ESP_LOGI(TAG, "AP Button pressed for provisioning...");
    startProvisioning();
  }

  switch (WiFi.status())
  {
  case WL_CONNECTED:
    // WiFi is connected to the router.
    check_for_ota_update(); // Check for OTA updates when connected to WiFi
    break;

  default:
    // WiFi.status() is other than WL_CONNECTED
    if (currentMillis - lastWifiReconnect >= wifiReconnectInterval && wiFiReconnectFlag == true)
    {
      ESP_LOGI(TAG, "[wifi] testing new connection to the router after working in disconnected mode");
      lastWifiReconnect = currentMillis;
      wiFiReconnectFlag = false;
      wiFiReconnectAttempt = 0;
      WiFi.reconnect();
    }
    break;
  }
}

void clio_wifi_setup()
{
  //- wifi settings.
  WiFi.onEvent(WiFiEvent);
  WiFi.mode(WIFI_AP_STA);
  //- begin wifi.
  WiFi.begin(esid, epass);
  return;
}