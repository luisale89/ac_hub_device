#include "clio_globals.h"

static const char *TAG = "CLIO-HOURMETER";
static unsigned long lastSecondTick = 0;
static int systemRunningSeconds = 0;


void update_hourmeter_in_fs() {
    // call this function every minute.
    ESP_LOGI(TAG,"updating hourmeter in fs.");
    const char * target_file = "/Hourmeter.txt";
    JsonDocument json;
    JsonDocument new_json;
    char new_hourmeter[256];
    const char* hourmeter_data = load_data_from_fs(target_file);

    DeserializationError error = deserializeJson(json, hourmeter_data);
    if (error)
    {
        ESP_LOGE(TAG, "hourmeter Deserialization error raised with code: %s", error.c_str());
        return;
    }

    const int current_h = json["hours"] | 0;
    const int current_m = json["minutes"] | 0;

    if (current_m < 59) {
        new_json["minutes"] = current_m + 1;
        new_json["hours"] = current_h;
    } else {
        new_json["minutes"] = 0;
        new_json["hours"] = current_h + 1;
    }

    //update global value.
    system_hourmeter = new_json["hours"];

    serializeJson(new_json, new_hourmeter);
    save_data_in_fs(new_hourmeter, target_file);
}


void update_time_counter() {
    //-
    const unsigned long currentMillis = millis();

    if (SysState != SYSTEM_ON) {
        //nothing to count when the system state is not on.
        lastSecondTick = currentMillis;
        return;
    }
    
    if (currentMillis - lastSecondTick >= 1000) {
        //1 second count
        lastSecondTick = currentMillis;

        systemRunningSeconds ++;
        if (systemRunningSeconds >= 60) { // every minute.
        systemRunningSeconds = 0;
        update_hourmeter_in_fs();
        }
    }

    return;
}