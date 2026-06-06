#include "clio_globals.h"
#include <esp_sntp.h>
#include <time.h>
#include <ctype.h> // Para isxdigit

static const char *TAG = "CLIO-HELPERS";
static int led_brightness = 0;
static int led_fade_amount = 5; // how much to fade the LED by each cycle, 5 for a cycle of 2.5 seconds with a tick of 50ms.
static bool led_state = false;
static bool sntp_sync_done = false; // flag to indicate if SNTP time synchronization has been completed at least once since boot, allowing other processes that depend on time sync to proceed after the first successful synchronization.
static unsigned long lastNetworkLedBlink = 0;
// NTP
static const char *ntpServer = "pool.ntp.org";
static const char *ntpServer2 = "time.google.com";
static const char *ntpServer3 = "time.nist.gov";
static const long gmtOffset_sec = 0; // UTC time, no offset
static const int daylightOffset_sec = 0;

// functions

void network_led_animation(LedAnimationStyle animation_style)
{
  //-
  const unsigned long current = millis();
  switch (animation_style)
  {
  case PULSE:
    if (current - lastNetworkLedBlink > 50UL)
    { // tick, 50ms
      lastNetworkLedBlink = current;
      analogWrite(NETWORK_LED, led_brightness);
      led_brightness = led_brightness + led_fade_amount;
      //-validations
      if (led_brightness > 125)
      {
        led_brightness = 125;
      }
      if (led_brightness < 0)
      {
        led_brightness = 0;
      }
      //-
      if (led_brightness <= 0 || led_brightness >= 125)
      {
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
    if (current - lastNetworkLedBlink >= 1000)
    {
      lastNetworkLedBlink = current;
      if (led_state == false)
      {
        led_state = true;
        led_brightness = 255;
      }
      else
      {
        led_state = false;
        led_brightness = 0;
      }
    }
    analogWrite(NETWORK_LED, led_brightness);
    break;

  case BLINK:
    if (current - lastNetworkLedBlink >= 500)
    {
      lastNetworkLedBlink = current;
      if (led_state == false)
      {
        led_state = true;
        led_brightness = 255;
      }
      else
      {
        led_state = false;
        led_brightness = 0;
      }
    }
    analogWrite(NETWORK_LED, led_brightness);
    break;

  case BLINK_2X:
    if (current - lastNetworkLedBlink >= 250)
    {
      if (led_state == false)
      {
        led_state = true;
        led_brightness = 255;
      }
      else
      {
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
  sntp_sync_done = true; // set flag to indicate that time is now synchronized, allowing other processes that depend on time sync to proceed.
  return;
}

bool is_time_synchronized()
{
  return sntp_sync_done;
}

void clio_sntp_setup()
{
  //-- SNTP Setup
  ESP_LOGI(TAG, "Creating SNTP server configuration.");
  sntp_set_sync_interval(3600000UL); // 1 hour in milliseconds
  sntp_set_time_sync_notification_cb(timeavailable);
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer, ntpServer2, ntpServer3); // configura tiempo desde el servidor NTP
  return;
}

esp_err_t clio_rtc_setup()
{
  //-- RTC setup
  if (!DS3231_RTC.begin())
  {
    ESP_LOGE(TAG, "Couldn't find RTC.");
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

bool is_valid_mac_str(const char *mac)
{
  // Una MAC estándar tiene exactamente 17 caracteres (12 hex + 5 separadores)
  if (strlen(mac) != 17)
  {
    return false;
  }

  for (int i = 0; i < 17; i++)
  {
    if (i % 3 == 2)
    {
      // Validar separadores en las posiciones 2, 5, 8, 11 y 14
      if (mac[i] != ':' && mac[i] != '-' && mac[i] != '.')
      {
        return false;
      }
    }
    else
    {
      // Validar que el resto sean dígitos hexadecimales (0-9, A-F, a-f)
      if (!isxdigit(mac[i]))
      {
        return false;
      }
    }
  }
  return true;
}

void parse_mac_address(const char *str, char sep, byte *bytes, int base)
{
  const int max_bytes = 6; // MAC addresses have 6 bytes
  for (int i = 0; i < max_bytes; i++)
  {
    bytes[i] = strtoul(str, NULL, base); // Convert byte
    str = strchr(str, sep);              // Find next separator
    if (str == NULL || *str == '\0')
    {
      break; // No more separators, exit
    }
    str++; // Point to next character after separator
  }
}

// Pasamos el buffer destino para evitar problemas de memoria
void format_device_mac(const uint8_t *mac_addr, char *out_str)
{
  sprintf(out_str, "%02X:%02X:%02X:%02X:%02X:%02X",
          mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
}

void format_device_serial(const uint8_t *mac_addr, char *out_str)
{
  sprintf(out_str, "%02X%02X%02X%02X%02X%02X",
          mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
}

void format_ap_ssid(const uint8_t *mac_addr, char *out_str)
{
  char device_serial[13]; // MAC address without colons, 12 chars + null

  snprintf(out_str, 32, "CLIO-%02X%02X%02X%02X", mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  return;
}