#include "clio_globals.h"
#include <WiFi.h>
#include <WebServer.h>
#include <esp_wifi.h>
//-
static const char *TAG = "CLIO-PROVISIONING";
static const int CONFIG_BUTTON_PIN = 0;
static const unsigned long TIMEOUT_MS = 120000; // 2 minutos
static const int RSSI_THRESHOLD = -70;
static unsigned long lastActivityTime = 0;
static bool inConfigMode = false;

WebServer server(80); // Puerto del server.

String getWiFiOptions()
{
    ESP_LOGD(TAG, "Escaneando redes...");
    String options = "[";
    int n = WiFi.scanNetworks(); // escaneo sincrono
    int count = 0;

    for (int i = 0; i < n && count < 5; ++i)
    {
        String currentSSID = WiFi.SSID(i);
        // Validamos longitud estándar y señal
        if (currentSSID.length() > 0 && currentSSID.length() <= 32 && WiFi.RSSI(i) >= RSSI_THRESHOLD)
        {
            if (count > 0)
                options += ",";
            options += "\"" + currentSSID + "\"";
            count++;
        }
    }
    options += "]";
    WiFi.scanDelete(); // Liberar memoria del escaneo
    return options;
}

// Función para refrescar el temporizador
void refreshTimeout()
{
    lastActivityTime = millis();
}

void startProvisioning()
{
    // wifi credentials provision
    ESP_LOGI(TAG, "\t ## INICIANDO PROVISIONAMIENTO DE CREDENCIALES WIFI ##");
    // disconnect active connection.
    WiFi.disconnect();
    ESP_LOGD(TAG, "Desconectando STA de la red WiFi existente.");
    delay(500);
    // start ap.
    WiFi.softAP(AP_SSID, AP_PROVISI_PW); // visible ap network.
    ESP_LOGD(TAG, "Punto de acceso iniciado con SSID: %s", AP_SSID);
    // start server.
    refreshTimeout();
    ntw_led_style = BLINK_2X;

    server.on("/", HTTP_GET, []()
              {
        ESP_LOGD(TAG, "GET request on root route");
        //-
        refreshTimeout();
        server.send(200, "application/json", "{ \"status\": true, \"message\": \"TagoIO Device Wifi Setup - Server Running\" }"); });

    server.on("/ping", HTTP_GET, []()
              {
        ESP_LOGD(TAG, "GET request on /ping route");
        //-
        refreshTimeout();
        server.send(200, "application/json", "{ \"pong\": true }"); });

    server.on("/setup/params", HTTP_GET, []()
              {
        ESP_LOGD(TAG, "GET request on /setup/params route");
        //-
        refreshTimeout();
        char output[1024];
        JsonDocument doc;
        JsonDocument available_networks;
        JsonArray response_body = doc.to<JsonArray>();

        DeserializationError err = deserializeJson(available_networks, getWiFiOptions());
        if (err) {
            ESP_LOGE(TAG, "Error decodificando json de opciones de WiFi. - %s", err.c_str());
            available_networks[0] = "";
            return;
        }
        // wifi fields
        JsonObject wifi_data = response_body.add<JsonObject>();
        wifi_data["name"] = "wifi_ssd";
        wifi_data["required"] = true;
        wifi_data["default"] = "public";
        wifi_data["type"] = "autocomplete";
        wifi_data["placeholder"] = "Selecciona tu red WiFi";
        wifi_data["options"] = available_networks;
        
        // password fields
        JsonObject passw_data = response_body.add<JsonObject>();
        passw_data["name"] = "wifi_password";
        passw_data["required"] = true;
        passw_data["type"] = "password";
        passw_data["placeholder"] = "Contraseña de la red";

        serializeJson(doc, output, sizeof(output));
        ESP_LOGD(TAG, "Sending 200 response with the body: %s", output);
        server.send(200, "application/json", String(output)); });

    server.on("/setup", HTTP_POST, []()
              {
        ESP_LOGD(TAG, "POST request on /setup route");
        //-
        refreshTimeout();
        if (server.hasArg("plain")) {
            String body = server.arg("plain");
            body += "\n";
            ESP_LOGD(TAG, "Recibido de la app: %s", body.c_str());
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, body);

            if (error) {
                ESP_LOGE(TAG, "Error decodificando json de la request - %s", error.c_str());
                server.send(400, "application/json", "{ \"status\": false, \"message\": \"invalid json\" }");
                return;
            }

            // Estructura de Tago: un array "params" con objetos {name, value}
            JsonArray params = doc["params"];
            String ssid, pass;

            for (JsonObject p : params) {
                String field = p["name"].as<String>();
                String value = p["value"].as<String>();
                ESP_LOGD(TAG, "field: %s, value: %s", field.c_str(), value.c_str());

                if (field == "wifi_ssd") ssid = value;
                else if (field == "wifi_password") pass = value;
            }

            // --- VALIDACIONES CRÍTICAS ---
            ssid.trim(); // Limpiar espacios accidentales
            if (ssid.length() == 0 || ssid.length() > 32) {
                ESP_LOGE(TAG, "SSID inválido (vacío o > 32 caracteres)");
                server.send(400, "application/json", "{ \"status\": false, \"message\": \"SSID fuera de rango (1-32)\" }");
                return;
            }
            pass.trim(); // Limpiar espacios accidentales
            if (pass.length() == 0) {
                ESP_LOGE(TAG, "Password vacío");
                server.send(400, "application/json", "{ \"status\": false, \"message\": \"Contraseña requerida\" }");
                return;
            }

            Serial.printf("Intentando conectar a: %s\n", ssid.c_str());
            
            // Validación de credenciales intentando conexión real
            WiFi.begin(ssid.c_str(), pass.c_str());
            ntw_led_style = BLINK;
            
            int retry = 0;
            // Esperamos hasta 10 segundos (20 * 500ms) para validar la red
            while (WiFi.status() != WL_CONNECTED && retry < 20) {
                delay(500);
                retry++;
                Serial.print(".");
            }

            if (WiFi.status() == WL_CONNECTED) {
                server.send(200, "application/json", "{\"status\": true}");
                save_wifi_data_in_fs(); // Asegúrate que esta función sea atómica
                ESP_LOGI(TAG, "Provisionamiento exitoso");
                delay(1000); // Dar tiempo al buffer de red
                esp_restart();

            } else {
                Serial.println("\nFallo de conexión: Credenciales incorrectas o señal insuficiente.");
                // Si falla, notificamos al cliente para que pueda corregir los datos
                server.send(400, "application/json", "{ \"status\": false, \"message\": \"No se pudo conectar a la red WiFi\" }");
                ESP_LOGE(TAG, "Fallo de conexión a WiFI.. esperando nuevas credenciales");
                ntw_led_style = BLINK_2X;
            }
        } else {
            server.send(400, "application/json", "{ \"status\": false, \"message\": \"invalid body\" }");
        } });

    server.begin();
    inConfigMode = true;

    // Bucle de gestión con Timeout
    while (inConfigMode)
    {
        server.handleClient();
        // Verificar si el tiempo de inactividad superó los 2 minutos
        if (millis() - lastActivityTime > TIMEOUT_MS)
        {
            ESP_LOGI(TAG, "Timeout alcanzado. Regresando a modo normal.");
            inConfigMode = false;
        }
        delay(10);
    }
    server.stop();
    ESP_LOGI(TAG, "Servidor de configuración detenido.");
    // reboot device
    esp_wifi_disconnect(); // asegurar desconexión antes de intentar reconectar en modo normal.
    esp_wifi_restore();
    delay(500); // Dar tiempo al buffer de red para enviar la respuesta antes de reiniciar
    esp_restart();
}