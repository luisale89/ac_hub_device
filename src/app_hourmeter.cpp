#include "clio_globals.h"

static const char *TAG = "CLIO-HOURMETER";
static unsigned long lastSecondTick = 0;
static int systemRunningSeconds = 0;
static const char *hourmeter_file = "/Hourmeter.txt";
static const int UPDATE_INTERVAL_MINUTES = 15; // update the hourmeter in fs every 15 minutes.

void update_hourmeter_in_fs()
{
    const int minutes_to_update = systemRunningSeconds / 60;
    if (minutes_to_update < UPDATE_INTERVAL_MINUTES)
    {
        // only update the hourmeter in fs if at least 15 minutes have passed since the last update.
        return;
    }
    systemRunningSeconds = 0; // reset seconds counter after calculating the minutes to update.

    ESP_LOGI(TAG, "updating hourmeter in fs.");
    ESP_LOGD(TAG, "minutes to update: %d", minutes_to_update);
    const char *target_file = "/Hourmeter.txt";
    JsonDocument json;
    JsonDocument new_json;
    char new_hourmeter[512]; // buffer for new hourmeter data to save in fs.
    const char *hourmeter_data = load_data_from_fs(target_file);

    DeserializationError error = deserializeJson(json, hourmeter_data);

    if (error)
    {
        ESP_LOGE(TAG, "hourmeter Deserialization error raised with code: %s", error.c_str());
        return;
    }

    const int prev_h = json["hours"] | 0;
    const int prev_m = json["minutes"] | 0;
    const int total_minutes = prev_m + minutes_to_update;
    // calculate new hourmeter values based on previous values and the total minutes to update.
    // If total minutes exceed 60, we will update the hours and reset the minutes.
    int new_h = prev_h;
    int new_m = total_minutes;

    if (total_minutes >= 60)
    {
        // if total minutes exceed 60, we will update the hours and reset the minutes.
        new_h += 1;
        new_m = 0;
    }

    ESP_LOGI(TAG, "current hourmeter: %d hours and %d minutes", prev_h, prev_m);
    ESP_LOGI(TAG, "new hourmeter: %d hours and %d minutes", new_h, new_m);

    // update global value.
    system_hourmeter = new_h; // update global hourmeter value in hours.

    new_json["hours"] = new_h;
    new_json["minutes"] = new_m;
    serializeJson(new_json, new_hourmeter, sizeof(new_hourmeter));
    esp_err_t err = save_data_in_fs(new_hourmeter, target_file);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error saving hourmeter data in fs.");
        return;
    }

    ESP_LOGI(TAG, "hourmeter updated in fs correctly.");
    return;
}

void time_counter_loop()
{
    //-
    const unsigned long currentMillis = millis();

    if (SysState != SYSTEM_ON)
    {
        // nothing to count when the system state is not on.
        return;
    }

    if (currentMillis - lastSecondTick >= 1000)
    {
        // 1 second count
        lastSecondTick = currentMillis;
        systemRunningSeconds++;
        update_hourmeter_in_fs();
    }

    return;
}