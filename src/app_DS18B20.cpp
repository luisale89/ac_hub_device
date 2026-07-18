#include "clio_globals.h"
#include <OneWire.h>
#include <DallasTemperature.h>

// ### DS18B20 TEMPERATURE SENSOR FUNCTIONS ###

static const char *TAG = "CLIO-DS18B20";
static const int tempSensorResolution = 12; // bits
static unsigned long lastTempRequest = 0;
static const float filter_factor = 0.05; // factor de filtrado para suavizar la lectura de temperatura
static bool first_reading = true;        // flag para indicar si es la primera lectura de temperatura
static int tempRequestDelay = 0;
OneWire oneWireRoomT(ROOM_TEMP_PIN);               // Setup a oneWire instance to communicate with any OneWire devices
DallasTemperature room_temp_sensor(&oneWireRoomT); // Pass our oneWire reference to DTS

void clio_temp_sensors_loop()
{
    // update room temperature reading and assigns the value to the glob. var
    const unsigned long current_time = millis();
    if (current_time - lastTempRequest >= tempRequestDelay)
    {
        //-
        float roomTempBuffer = room_temp_sensor.getTempCByIndex(0);
        if (roomTempBuffer == -127.0)
        {
            ESP_LOGE(TAG, "Error -127 leyendo el sensor en el pin %s", ROOM_TEMP_PIN);
        }
        else if (roomTempBuffer == 85.0)
        {
            ESP_LOGE(TAG, "Error 85.0 leyendo el sensor en el pin %s", ROOM_TEMP_PIN);
        }
        else
        {
            if (first_reading)
            {
                // if it's the first reading, just assign the value directly without filtering
                room_temperature = roomTempBuffer;
                first_reading = false;
            }
            else
            {
                // apply low-pass filter to smooth the temperature reading
                room_temperature = room_temperature * (1 - filter_factor) + roomTempBuffer * filter_factor;
            }
        }

        // request new temperature reading
        room_temp_sensor.requestTemperatures();
        lastTempRequest = current_time;
    }
    return;
}

void clio_temp_sensors_setup()
{
    // sensor setup
    ESP_LOGI(TAG, "setting up temp. sensors");
    room_temp_sensor.begin();
    room_temp_sensor.setResolution(tempSensorResolution);
    room_temp_sensor.setWaitForConversion(false);
    room_temp_sensor.requestTemperatures();
    lastTempRequest = millis();
    tempRequestDelay = 750 / (1 << (12 - tempSensorResolution));
    ESP_LOGI(TAG, "room_temp_sensor setup completed");
}