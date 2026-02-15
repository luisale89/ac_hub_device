#include "clio_globals.h"
#include <esp_sntp.h>

static const char *TAG = "CLIO-HELPERS";
static int led_brightness = 0;
static int led_fade_amount = 0;
static bool led_state = false;
static unsigned long lastNetworkLedBlink = 0;
// NTP
static const char *ntpServer = "pool.ntp.org";
static const long gmtOffset_sec = -4 * 3600;
static const int daylightOffset_sec = 0;

// functions

void network_led_animation(LedAnimationStyle animation_style) {
  //-
  const unsigned long current = millis();
  switch (animation_style)
  {
    case PULSE:
      if (current - lastNetworkLedBlink > 50L) { //tick, 50ms
        lastNetworkLedBlink = current;
        analogWrite(NETWORK_LED, led_brightness);
        led_brightness = led_brightness + led_fade_amount;
        //-validations
        if (led_brightness > 125) {led_brightness = 125;}
        if (led_brightness < 0) {led_brightness = 0;}
        //-
        if (led_brightness <= 0 || led_brightness >= 125) {
          led_fade_amount = -led_fade_amount;
        }
      }
      break;

    case ALLWAYS_OFF:
      analogWrite(NETWORK_LED, 0);
      break;

    case ALLWAYS_ON:
      analogWrite(NETWORK_LED, 255);
      break;

    case BLINK_05X:
      if (current - lastNetworkLedBlink >= 1000) {
        lastNetworkLedBlink = current;
        if (led_state == false) {
          led_state = true;
          led_brightness = 255;
        } else {
          led_state = false;
          led_brightness = 0;
        }
      }
      analogWrite(NETWORK_LED, led_brightness);
      break;

    case BLINK:
      if (current - lastNetworkLedBlink >= 500) {
        lastNetworkLedBlink = current;
        if (led_state == false) {
          led_state = true;
          led_brightness = 255;
        } else {
          led_state = false;
          led_brightness = 0;
        }
      }
      analogWrite(NETWORK_LED, led_brightness);
      break;
  
    case BLINK_2X:
      if (current - lastNetworkLedBlink >= 250) {
        if (led_state == false) {
          led_state = true;
          led_brightness = 255;
        } else {
          led_state = false;
          led_brightness = 0;
        }
      }
      analogWrite(NETWORK_LED, led_brightness);
      break;
  
    default:
      analogWrite(NETWORK_LED, 0);
      break;
  }
  return;
}

void update_rtc_from_ntp() // [OK] [OK]
{
  struct tm timeinfo;

  if (getLocalTime(&timeinfo))
  {
    int dia = timeinfo.tm_mday;
    int mes = timeinfo.tm_mon + 1;
    int ano = timeinfo.tm_year + 1900;
    int hora = timeinfo.tm_hour;
    int minuto = timeinfo.tm_min;
    int segundo = timeinfo.tm_sec;

    DS3231_RTC.adjust(DateTime(ano, mes, dia, hora, minuto, segundo));
    ESP_LOGI(TAG, "[RTC] DateTime updated!");
    snprintf(last_ntp_update, sizeof(last_ntp_update), "%02d:%02d:%02d", hora, minuto, segundo);

    ESP_LOGI(TAG, "[RTC] ajustado");
    ESP_LOGD(TAG, "%s", last_ntp_update);

  }
  else
  {
    ESP_LOGE(TAG, "[RTC] Could not obtain time info from NTP Server, Skiping RTC update");
  }
  return;
}

void timeavailable(struct timeval *tml) // [OK] [OK] 
{
  // this should be called every hour automatically..
  ESP_LOGI(TAG, "[RTC] Got time adjustment from NTP! latest datetime is now available");
  update_rtc_from_ntp(); // update RTC with latest time from NTP server.
  return;
}

void clio_sntp_setup() {
  //-- SNTP Setup
  ESP_LOGI(TAG, "Creating SNTP server configuration.");
  sntp_set_time_sync_notification_cb(timeavailable);        // sntp sync interval is 1 hour.
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer); // configura tiempo desde el servidor NTP
  return;
}

esp_err_t clio_rtc_setup() {
  //-- RTC setup
  if (!DS3231_RTC.begin())
  {
    ESP_LOGE(TAG,"Couldn't find RTC.");
    return ESP_FAIL;
  }
  if (DS3231_RTC.lostPower())
  {
    ESP_LOGI(TAG, "RTC is NOT running!, setting up on sketch compiled datetime.");
    // following line sets the RTC to the date & time this sketch was compiled
    DS3231_RTC.adjust(DateTime(F(__DATE__), F(__TIME__)));
  } 
  return ESP_OK;
}

char* print_device_mac(const uint8_t * mac_addr) {
    static char mac_str[18]; // 17 chars + null
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
            mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    return mac_str;
}

void parse_mac_address(const char* str, char sep, byte* bytes, int maxBytes, int base) {
    for (int i = 0; i < maxBytes; i++) {
        bytes[i] = strtoul(str, NULL, base);  // Convert byte
        str = strchr(str, sep);               // Find next separator
        if (str == NULL || *str == '\0') {
            break;                            // No more separators, exit
        }
        str++;                                // Point to next character after separator
    }
}

char* print_device_serial(const uint8_t * mac_addr) {
  static char mac_str[13]; // 12 chars + null
  snprintf(mac_str, sizeof(mac_str), "%02x%02x%02x%02x%02x%02x",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  return mac_str;
}
