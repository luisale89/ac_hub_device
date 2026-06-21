#include "clio_globals.h"

static const char *TAG = "CLIO-HOURMETER";
static unsigned long lastSecondTick = 0;
static int systemRunningSeconds = 0;
static const char *target_file = "/Hourmeter.txt";
static const int UPDATE_INTERVAL_SECONDS = 900;

void update_hourmeter_in_fs()
{

    ESP_LOGI(TAG, "Iniciando actualización de horómetro en FS...");

    // Leer datos actuales
    const char *hourmeter_data = load_data_from_fs(target_file);
    JsonDocument json;
    DeserializationError error = deserializeJson(json, hourmeter_data);

    if (error)
    {
        ESP_LOGE(TAG, "Error de deserialización: %s. Conservando valores en memoria activa.", error.c_str());
        // Aquí podrías intentar cargar un archivo ".bak" de respaldo si lo implementas
        return;
    }

    if (json["hours"].isNull() || json["minutes"].isNull())
    {
        ESP_LOGE(TAG, "Estructura JSON corrupta o incompleta detectada. ¡Operación abortada!");
        return;
    }

    const int minutes_to_update = systemRunningSeconds / 60;
    // 2. Reiniciar el contador de segundos de forma segura reteniendo el remanente si lo hubiera
    systemRunningSeconds -= (minutes_to_update * 60);

    const int prev_h = json["hours"];
    const int prev_m = json["minutes"];
    const int total_minutes = prev_m + minutes_to_update;

    // 3. MATEMÁTICA EXACTA: Evita pérdida de minutos residuales
    const int new_h = prev_h + (total_minutes / 60);
    const int new_m = total_minutes % 60;

    ESP_LOGI(TAG, "Horómetro actual: %d h y %d m. Nuevos valores: %d h y %d m", prev_h, prev_m, new_h, new_m);

    // Actualizar variable global de control
    system_hourmeter = new_h;

    // 4. Salvar datos con un buffer optimizado
    JsonDocument new_json;
    char new_hourmeter[128]; // 512 bytes es excesivo para dos enteros, 128 es más que seguro

    new_json["hours"] = new_h;
    new_json["minutes"] = new_m;

    serializeJson(new_json, new_hourmeter, sizeof(new_hourmeter));

    esp_err_t err = save_data_in_fs(new_hourmeter, target_file);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error crítico al escribir en FS.");
        // Re-sumamos los minutos para no perderlos en la próxima vuelta
        systemRunningSeconds += (minutes_to_update * 60);
        return;
    }

    ESP_LOGI(TAG, "Horómetro guardado exitosamente.");
}

void time_counter_loop()
{
    const unsigned long currentMillis = millis();

    if (SysState != SYSTEM_ON)
    {
        return;
    }

    if (currentMillis - lastSecondTick >= 1000)
    {
        lastSecondTick = currentMillis;
        systemRunningSeconds++;

        // Controlar la ejecución en FS desde aquí optimiza recursos de CPU y Stack
        if (systemRunningSeconds >= UPDATE_INTERVAL_SECONDS)
        {
            update_hourmeter_in_fs();
        }
    }
}