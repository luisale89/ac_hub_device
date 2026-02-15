#include "clio_globals.h"
#include <esp_wifi.h>

static const char* TAG = "CLIO-WIFI";
static bool last_ap_btn_state = false;
static bool ap_btn_state = false;
static bool wiFiReconnectFlag = false;
static int wiFiReconnectAttempt = 0;
static const int MAX_WIFI_RECONNECT_ATT = 33;
static unsigned long lastApBtnChange = 0;
static unsigned long lastWifiReconnect = 0;
static const unsigned long wifiReconnectInterval = 5L * 60000L;  // 5 minutos para intentar reconectar al wifi.
static const unsigned long BTN_DEBOUNCE_TIME = 250; // 250ms rebound time constant;

// ### WIFI EVENT HANDLER ###
void set_station_for_espnow_offline_mode() {
  // this function allows offline esp-now communication, in case of getting disconnected from the WiFi network.
  ntw_led_style = BLINK_05X; //LED slow blink
  ESP_LOGI(TAG, "[wifi] Setting up to communicate over esp-now disconnected from the router.");
  ESP_LOGI(TAG, "[wifi] channel: %d", WiFi.channel());
  espnow_connection_state = ESPNOW_OFFLINE;
  lastWifiReconnect = millis(); // set timer for reconnect to the router
  wiFiReconnectFlag = true;
  return;
}

void WiFiEvent(arduino_event_t *wifi_event) {
  ESP_LOGI(TAG, "[wifi] event code: %d", wifi_event->event_id);
  switch (wifi_event->event_id) {
    case ARDUINO_EVENT_WIFI_READY:               ESP_LOGI(TAG, "[wifi] interface ready"); break;
    case ARDUINO_EVENT_WIFI_SCAN_DONE:           ESP_LOGI(TAG, "[wifi] Completed scan for access points"); break;
    case ARDUINO_EVENT_WIFI_STA_START:           ESP_LOGI(TAG, "[wifi] Client started"); break;
    case ARDUINO_EVENT_WIFI_STA_STOP:            ESP_LOGI(TAG, "[wifi] Clients stopped"); break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:       ESP_LOGI(TAG, "[wifi] Connected to the AP"); break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      if (wiFiReconnectAttempt > MAX_WIFI_RECONNECT_ATT) {
        ESP_LOGI(TAG, "[wifi] reached max_reconnect_attempts.");
        set_station_for_espnow_offline_mode();
        break;
      }
      //-
      ntw_led_style = BLINK;
      wiFiReconnectAttempt ++;
      ESP_LOGI(TAG, "[wifi] Disconnected from WiFi Access Point"); 
      ESP_LOGI(TAG, "lost connection. Reason code: %d", wifi_event->event_info.wifi_sta_disconnected.reason);
      ESP_LOGI(TAG, "Reconnect attempt # %d..", wiFiReconnectAttempt);
      espnow_connection_state = ESPNOW_IDLE;
      WiFi.reconnect();
      break;

    case ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE: ESP_LOGI(TAG, "[wifi] Authentication mode of access point has changed"); break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      espnow_connection_state = ESPNOW_ONLINE;
      wiFiReconnectAttempt = 0;
      ESP_LOGI(TAG, "Router connection completed!! -> channel: %d", WiFi.channel());
      ESP_LOGI(TAG, "GOT IP Address: %s", WiFi.localIP().toString());
      ESP_LOGI(TAG, "RSSI: %d", WiFi.RSSI());
      ESP_LOGI(TAG, "STA MAC Address: %s", WiFi.macAddress().c_str());
      ESP_LOGI(TAG, "SOFT AP MAC Address: %s", WiFi.softAPmacAddress().c_str());
      break;

    case ARDUINO_EVENT_WIFI_STA_LOST_IP:        ESP_LOGI(TAG, "[wifi] Lost IP address and IP address is reset to 0"); break;
    case ARDUINO_EVENT_WPS_ER_SUCCESS:          ESP_LOGI(TAG, "[wifi] Protected Setup (WPS): succeeded in enrollee mode"); break;
    case ARDUINO_EVENT_WPS_ER_FAILED:           ESP_LOGI(TAG, "[wifi] Protected Setup (WPS): failed in enrollee mode"); break;
    case ARDUINO_EVENT_WPS_ER_TIMEOUT:          ESP_LOGI(TAG, "[wifi] Protected Setup (WPS): timeout in enrollee mode"); break;
    case ARDUINO_EVENT_WPS_ER_PIN:              ESP_LOGI(TAG, "[wifi] Protected Setup (WPS): pin code in enrollee mode"); break;
    case ARDUINO_EVENT_WIFI_AP_START:           ESP_LOGI(TAG, "[wifi] access point started"); break;
    case ARDUINO_EVENT_WIFI_AP_STOP:            ESP_LOGI(TAG, "[wifi] access point stopped"); break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:    ESP_LOGI(TAG, "[wifi] Client connected"); break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED: ESP_LOGI(TAG, "[wifi] Client disconnected"); break;
    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:   ESP_LOGI(TAG, "[wifi] Assigned IP address to client"); break;
    case ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED:  ESP_LOGI(TAG, "[wifi] Received probe request"); break;
    case ARDUINO_EVENT_WIFI_AP_GOT_IP6:         ESP_LOGI(TAG, "[wifi] AP IPv6 is preferred"); break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP6:        ESP_LOGI(TAG, "[wifi] STA IPv6 is preferred"); break;
    default:                                    break;
  }
}

void save_wifi_data_in_fs()
{
  JsonDocument doc;
  char jsonData[256];

  doc["ssid"] = WiFi.SSID();
  doc["pass"] = WiFi.psk();

  serializeJson(doc, jsonData, sizeof(jsonData));
  save_data_in_fs(jsonData, "/WiFi.txt");
  return;
}

void clio_wifi_loop() //[ok] - Mejorado con manejo de errores y reconexiones
{
  const unsigned long currentMillis = millis();
  // Solo esperar SmartConfig si el botón AP está presionado
  const bool current_ap_btn = digitalRead(AP_BTN) ? false : true;
  if (current_ap_btn != last_ap_btn_state) {
    last_ap_btn_state = current_ap_btn;
    lastApBtnChange = currentMillis;
  }

  // button debouncer
  if (currentMillis - lastApBtnChange >= BTN_DEBOUNCE_TIME) {
    // btn change
    ap_btn_state = current_ap_btn;
  }

  // button pressed for >3 seconds.
  if (ap_btn_state && currentMillis - lastApBtnChange >= 3000L) {
    lastApBtnChange = currentMillis;
    ESP_LOGI(TAG, "AP Button pressed for provisioning...");
    startProvisioning();
  }
  
  switch (WiFi.status())
  {
  case WL_CONNECTED:
    // WiFi is connected to the router.
    break;
  
  default:
    //WiFi.status() is other than WL_CONNECTED
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

void clio_wifi_setup() {
  //- wifi settings.
  WiFi.onEvent(WiFiEvent);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_DEFAULT_PW, 1, 1); // hidden network..
  WiFi.setAutoReconnect(false);
  //- begin wifi.
  WiFi.begin(esid, epass);
  return;
}