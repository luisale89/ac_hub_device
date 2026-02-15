#include "clio_globals.h"
#include <OneWire.h>
#include <DallasTemperature.h>

// ### DS18B20 TEMPERATURE SENSOR FUNCTIONS ###

static const char *TAG = "CLIO-DS18B20";
static const int tempSensorResolution = 12; //bits
static unsigned long lastTempRequest = 0;
static int tempRequestDelay = 0;
OneWire oneWireCaseT(CASE_TEMP); // Setup a oneWire instance to communicate with any OneWire devices
DallasTemperature case_temperature_sensor(&oneWireCaseT); // Pass our oneWire reference to DTS

void update_case_temperature()
{
    // update case temperature reading and assigns the value to the glob. var
    const unsigned long current_time = millis();
    if (current_time - lastTempRequest >= tempRequestDelay) {
        //-
        double caseTempBuffer = case_temperature_sensor.getTempCByIndex(0);
        if (caseTempBuffer == -127.0) {
            ESP_LOGE(TAG, "Error -127 leyendo el sensor en el pin %s", CASE_TEMP);
        } else if (caseTempBuffer == 85.0) {
            ESP_LOGE(TAG, "Error 85.0 leyendo el sensor en el pin %s", CASE_TEMP);
        } else {
            case_pcb_temperature = caseTempBuffer;
        }

        // request new temperature reading
        case_temperature_sensor.requestTemperatures();
        lastTempRequest = current_time;
    }
    return;
}

void clio_case_tempsensor_setup() {
    // sensor setup
    ESP_LOGI(TAG, "setting up temp. sensors");
    case_temperature_sensor.begin();
    case_temperature_sensor.setResolution(tempSensorResolution);
    case_temperature_sensor.setWaitForConversion(false);
    case_temperature_sensor.requestTemperatures();
    lastTempRequest = millis();
    tempRequestDelay = 750 / (1 << (12 - tempSensorResolution));
    ESP_LOGI(TAG, "case_temp_sensor setup completed");
}