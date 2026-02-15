#include "clio_globals.h"
#include <FS.h>
#include <LittleFS.h>

static const char *TAG = "CLIO-SPIFFS";

//### FILESYSTEM functions. ###

esp_err_t clio_spiffs_setup() {
  if(!LittleFS.begin(true)) {
    ESP_LOGE(TAG, "Ocurrió un error al ejecutar LittleFS..");
    return ESP_FAIL;
  }
  return ESP_OK;
}

char* load_data_from_fs(const char *target_file) {
  static char buffer[1024]; // Buffer estático para evitar heap
  //-
  File f = LittleFS.open(target_file);
  if (!f) {
    ESP_LOGE(TAG, "Error al abrir el archivo solicitado.");
    strcpy(buffer, "null");
    return buffer;
  }

  size_t len = f.size();
  if (len >= sizeof(buffer)) {
    ESP_LOGE(TAG, "Archivo demasiado grande para el buffer.");
    f.close();
    strcpy(buffer, "null");
    return buffer;
  }

  f.readBytes(buffer, len);
  buffer[len] = '\0';
  f.close();

  ESP_LOGI(TAG, "data loaded from SPIFFS correctly");
  ESP_LOGD(TAG, "data: %s", buffer);
  return buffer;
}

void save_data_in_fs(const char* data_to_save, const char* target_file) {
  //savin data in filesystem.
  ESP_LOGI(TAG, "saving data in SPIFFS");
  ESP_LOGD(TAG, "target_file: %s", target_file);
  ESP_LOGD(TAG, "data: %s", data_to_save);

  File f = LittleFS.open(target_file, "w");
  if (!f){
    ESP_LOGE(TAG, "Error al abrir el archivo solicitado.");
    return;
  }

  f.print(data_to_save);
  f.close();
  ESP_LOGI(TAG, "data saved correctly in SPIFFS.");
  return;
}