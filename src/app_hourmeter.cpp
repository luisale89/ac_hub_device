#include "clio_globals.h"

static const char *TAG = "CLIO-HOURMETER";
static int systemRunningSeconds = 0;
static const char *target_file = "/Minutes.txt";
static const int UPDATE_INTERVAL_SECONDS = 900; // 15 minutes

void update_minutes_in_fs()
{
    ESP_LOGI(TAG, "Iniciando actualización de minutos en FS...");
    const int minutes_to_update = systemRunningSeconds / 60;
    // Restart running seconds..
    systemRunningSeconds -= (minutes_to_update * 60);

    // Update the system minutes in a thread-safe manner
    xSemaphoreTake(xMutex, portMAX_DELAY);
    system_minutes += minutes_to_update;
    uint32_t minutes_to_save = system_minutes; // Guardamos el valor en una variable local
    xSemaphoreGive(xMutex);

    ESP_LOGI(TAG, "Actualizando horómetro en LittleFS. Nuevos minutos totales: %lu", minutes_to_save);

    // Convertimos el entero a string para guardarlo de forma simple
    char str_payload[32];
    snprintf(str_payload, sizeof(str_payload), "%lu", minutes_to_save);

    esp_err_t err = save_data_in_fs(str_payload, target_file);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error crítico al guardar los minutos del horómetro en LittleFS.");
        return;
    }

    ESP_LOGI(TAG, "Minutos guardados exitosamente!");

    return;
}

void time_counter_loop()
{
    // this function is called once a second from the time_counter_task,
    // and is responsible for counting the system running time in seconds,
    // and updating the minutes in the filesystem every 15 minutes.

    if (SysState != SYSTEM_ON)
    {
        return;
    }

    systemRunningSeconds++;

    // Controlar la ejecución en FS desde aquí optimiza recursos de CPU y Stack
    if (systemRunningSeconds >= UPDATE_INTERVAL_SECONDS)
    {
        update_minutes_in_fs();
    }
}