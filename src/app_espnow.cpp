#include "clio_globals.h"
#include <esp_now.h>

static const char *TAG = "CLIO-ESPNOW";
static unsigned long lastEspnowPost = 0;
static const unsigned long espnowPostingInterval = 3000UL; // 3 seconds for espnow message post.
//-
static esp_now_peer_info_t slaveTemplate;
static pairing_data_struct pairing_data;
static const int MAX_NOT_RSPND_TO_OFFLINE = 10; // 10 messages not received.
static int monitor_not_rspnd_count = 0;
static int controller_not_rspnd_count = 0;
static bool postToPeers = false;

// ### ESPNOW FUNCTIONS ###
PeerRoleID get_peer_role_from_fs(const char *target_mac_address)
{
    //-
    const char *peer_list = load_data_from_fs("/Peer.txt");
    JsonDocument json_doc;
    DeserializationError error = deserializeJson(json_doc, peer_list);

    if (error)
    {
        ESP_LOGE(TAG, "[JSON] Deserialization error %s", error.c_str());
        return UNSET;
    }

    const char *controller_mac_saved = json_doc["controller"] | "null";
    if (strcmp(controller_mac_saved, target_mac_address) == 0)
    {
        ESP_LOGI(TAG, "device found as controller.");
        return CONTROLLER;
    }

    const char *monitor_saved = json_doc["monitor"] | "null";
    if (strcmp(monitor_saved, target_mac_address) == 0)
    {
        ESP_LOGI(TAG, "device found as monitor");
        return MONITOR;
    }

    ESP_LOGI(TAG, "device not found in SPIFFS!");
    return UNSET;
}

esp_err_t add_peer_to_plist(const uint8_t *peer_addr)
{ // add pairing
    ESP_LOGI(TAG, "adding new peer to peer list");

    // reset slaveTemplate variable
    memset(&slaveTemplate, 0, sizeof(slaveTemplate));
    // create reference to slaveTemplate memory loc.
    const esp_now_peer_info_t *peer = &slaveTemplate;

    // set values in peer template
    memcpy(slaveTemplate.peer_addr, peer_addr, 6);
    slaveTemplate.channel = 0; // pick a channel.. 0 means it take the current STA channel
    slaveTemplate.encrypt = 0; // no encryption

    // check if the peer exists and remove it from peerlist
    if (esp_now_is_peer_exist(peer_addr) == true)
    {
        // Slave already paired.
        ESP_LOGI(TAG, "peer already exists, deleting existing data.");
        esp_err_t deleteStatus = esp_now_del_peer(peer_addr);
        if (deleteStatus == ESP_OK)
        {
            ESP_LOGI(TAG, "peer deleted!");
        }
        else
        {
            ESP_LOGE(TAG, "error deleting peer!");
            return ESP_FAIL;
        }
    }

    ESP_LOGI(TAG, "new peer to be saved in the peer list..");
    // s<ve peer in peerlist
    esp_err_t result = esp_now_add_peer(peer);
    switch (result)
    {
    case ESP_OK:
        ESP_LOGI(TAG, "peer added successfully");
        return ESP_OK;

    default:
        ESP_LOGE(TAG, "Error trying to add new peer to p.list");
        ESP_LOGE(TAG, "%s", esp_err_to_name(result));
        return ESP_FAIL;
    }
}

void OnDataSent(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
    char device_serial[13]; // 12 chars + null
    format_device_serial(tx_info->des_addr, device_serial);

    switch (tx_info->tx_status)
    {
    case ESP_NOW_SEND_SUCCESS:
        ESP_LOGI(TAG, "esp-now packet sent to: %s", device_serial);
        break;

    default:
        ESP_LOGE(TAG, "esp-now packet not sent");
        break;
    }
}

void OnDataRecv(const esp_now_recv_info_t *rcv_info, const uint8_t *incomingData, int len)
{
    // no response on IDLE state
    if (espnow_connection_state == ESPNOW_IDLE)
    {
        ESP_LOGI(TAG, "Waiting to finish AP connection. Ignoring data...");
        return;
    }
    char sender_mac_str[18]; // 17 chars + null
    char sender_serial[13];  // 12 chars + null
    format_device_mac(rcv_info->src_addr, sender_mac_str);
    format_device_serial(rcv_info->src_addr, sender_serial);
    ESP_LOGI(TAG, "%d bytes of data received from: %s", len, sender_serial);
    //-get role ID
    PeerRoleID device_role = get_peer_role_from_fs(sender_mac_str);
    // to enable devices to communicate with the server, the user must send a mqtt message with the mac address and the role
    // of the device. Only then, the server can process messages from that mac address.
    if (device_role == UNSET)
    {
        ESP_LOGI(TAG, "device is unset in fs. Ignoring data...");
        return;
    }

    uint8_t message_type = incomingData[0]; // first message byte is the type of message
    JsonDocument doc;
    char respnd_buffer[512]; // v1.0 espnow msg size

    switch (message_type)
    {
    case DATA:
        // the message is data type
        ESP_LOGI(TAG, "message of type DATA arrived.");

        switch (device_role)
        {
        case CONTROLLER:
            ESP_LOGI(TAG, "message received from CONTROLLER device.");
            controller_peer_online = true;  // set to true when receives a message from the device.
            controller_not_rspnd_count = 0; // resets the counter.
            //- sets global variable.
            memcpy(&controller_data, incomingData, sizeof(controller_data));
            //-breaks

            system_alarms.controller_ac = controller_data.alarm_code; // update system alarms struct with the new alarm code received from the controller device.
            if (controller_data.alarm_code != NORMAL)
            {
                ESP_LOGI(TAG, "ALARM detected in CONTROLLER device! code: %d", controller_data.alarm_code);
            }

            break;

        case MONITOR:
            ESP_LOGI(TAG, "message received from MONITOR device.");
            monitor_peer_online = true;
            monitor_not_rspnd_count = 0;
            memcpy(&monitor_data, incomingData, sizeof(monitor_data));
            //-breaks

            system_alarms.monitor_ac = monitor_data.alarm_code; // update system alarms struct
            if (monitor_data.alarm_code != NORMAL)
            {
                ESP_LOGI(TAG, "ALARM detected in MONITOR device! code: %d", monitor_data.alarm_code);
            }

            break;

        default:
            ESP_LOGI(TAG, "unknown sender role. ignoring message.");
            break;
        }

        break;

    case PAIRING:
        // the message is a pairing request
        ESP_LOGI(TAG, "message of type PAIRING arrived.");
        memcpy(&pairing_data, incomingData, sizeof(pairing_data));
        //-
        if (pairing_data.sender_role == SERVER)
        {
            ESP_LOGE(TAG, "message from SERVER device received. wtf!");
            return; // do not replay to server itself.
        }

        pairing_data.sender_role = SERVER;          // server sending a response.
        pairing_data.device_new_role = device_role; // role of the receiver device, stored in filesystem.
        pairing_data.channel = WiFi.channel();      // current WiFi channel.

        doc["msg"] = MessageTypeEnum::PAIRING;
        doc["rol"] = pairing_data.sender_role;
        doc["n_rol"] = pairing_data.device_new_role;
        doc["chan"] = pairing_data.channel;

        serializeJson(doc, respnd_buffer, sizeof(respnd_buffer));

        if (add_peer_to_plist(rcv_info->src_addr) == ESP_OK)
        {
            ESP_LOGI(TAG, "sending response to peer with PAIRING data.");
            esp_err_t result = esp_now_send(rcv_info->src_addr, (uint8_t *)respnd_buffer, strlen(respnd_buffer));

            if (result == ESP_OK)
            {
                ESP_LOGI(TAG, "pairing response sent.");
            }
            else
            {
                ESP_LOGE(TAG, "Error sending pairing response, reason: %s", esp_err_to_name(result));
            };
        }
        else
        {
            ESP_LOGE(TAG, "Error adding peer to peer list. Pairing failed.");
        }

        break;

    default:
        ESP_LOGE(TAG, "Invalid data TYPE received...");
        break;
    }
}

void clio_espnow_loop()
{
    //-
    const unsigned long currentMillis = millis();
    if (espnow_connection_state != ESPNOW_ONLINE)
    {
        return;
    }
    //- check if its time to send a message to the peers
    if (currentMillis - lastEspnowPost > espnowPostingInterval)
    {
        postToPeers = true;
    }
    // check if there is a pending post request.
    if (!postToPeers)
    {
        return;
    }

    ESP_LOGD(TAG, "sending message to esp-now peers.");
    esp_now_peer_num number_of_peers;
    esp_now_get_peer_num(&number_of_peers);
    if (number_of_peers.total_num == 0)
    {
        ESP_LOGD(TAG, "peer list is empty. no message sent!");
        postToPeers = false;
        lastEspnowPost = currentMillis;
        return;
    }

    JsonDocument doc;
    char msg_buffer[512]; // v1.0 espnow msg size

    doc["msg"] = MessageTypeEnum::DATA;
    doc["rol"] = PeerRoleID::SERVER;
    doc["glo"][0] = peersMode;
    doc["glo"][1] = SysState;
    doc["glo"][2] = SysFaultState;
    doc["glo"][3] = activeSetpoint;
    doc["glo"][4] = room_temperature;
    doc["glo"][5] = fault_restart_attempt_flag; // flag to reset faults in the controller and monitor devices, set by the user from the broker or automatically by the system after a fault recovery attempt.
    doc["cnf"][0] = system_config.room_temp_control_en;
    doc["cnf"][1] = system_config.comp_nominal_amp;
    doc["cnf"][2] = system_config.comp_amp_threshold;
    doc["cnf"][3] = system_config.discharge_max_temp;
    doc["cnf"][4] = system_config.liquid_max_temp;
    doc["cnf"][5] = system_config.vapor_line_min_temp;

    serializeJson(doc, msg_buffer, sizeof(msg_buffer));

    // send data to peers.
    ESP_LOGD(TAG, "sending: %s", msg_buffer);
    esp_err_t result = esp_now_send(NULL, (uint8_t *)msg_buffer, strlen(msg_buffer));

    if (result == ESP_OK)
    {
        ESP_LOGD(TAG, "config message sent.");
    }
    else
    {
        ESP_LOGE(TAG, "Error sending config msg, reason: %s", esp_err_to_name(result));
    }

    postToPeers = false;
    lastEspnowPost = currentMillis;

    if (controller_peer_online)
    {
        controller_not_rspnd_count++; // increase by one
        //- check if the max is reached.
        if (controller_not_rspnd_count > MAX_NOT_RSPND_TO_OFFLINE)
        {
            ESP_LOGD(TAG, "%s", "CONTROLLER device is offline.");
            controller_peer_online = false; // after not receive max_number, the device is offline.
            system_alarms.controller_ac = AlarmCode::NORMAL;
            // if the device is offline, we set the alarm code to NORMAL, because we cannot get the real alarm code from the device,
            // and we want to avoid false alarms in the system. The system will report NORMAL for the controller device until it receives a new message from it,
            // then it will update the alarm code with the real one received from the device.
        }
    }

    if (monitor_peer_online)
    {
        monitor_not_rspnd_count++;
        if (monitor_not_rspnd_count > MAX_NOT_RSPND_TO_OFFLINE)
        {
            ESP_LOGD(TAG, "%s", "MONITOR device is offline.");
            monitor_peer_online = false;
            system_alarms.monitor_ac = AlarmCode::NORMAL;
        }
    }

    //-
    return;
}

esp_err_t clio_espnow_cnf()
{
    // esp-now config.

    if (esp_now_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "-- Error initializing ESP-NOW --");
        return ESP_FAIL;
    }
    // register esp_callbacks.
    esp_now_register_send_cb(OnDataSent);
    esp_now_register_recv_cb(OnDataRecv);
    return ESP_OK;
}