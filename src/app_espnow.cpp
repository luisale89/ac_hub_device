#include "clio_globals.h"
#include <esp_now.h>

static const char *TAG = "CLIO-ESPNOW";
static unsigned long lastEspnowPost = 0;
static const unsigned long espnowPostingInterval = 5000L; // 5 seconds for espnow message post.
//-
static esp_now_peer_info_t slaveTemplate;
static pairing_data_struct pairing_data;
static const int MAX_NOT_RSPND_TO_OFFLINE = 10; // 10 messages not received.
static int monitor_not_rspnd_count = 0;
static int controller_not_rspnd_count = 0;
static bool postToPeers = false;
static espnow_settings_struct espnow_msg_config = { //initial data
  DATA, SERVER, FAN_MODE, UNKN, 24, STATUS_OK
};

//### ESPNOW FUNCTIONS ###
PeerRoleID get_peer_role_from_fs(const char * target_mac_address){
    //-
    const char* peer_list = load_data_from_fs("/Peer.txt");
    JsonDocument json_doc;
    DeserializationError error = deserializeJson(json_doc, peer_list);

    if (error){
        ESP_LOGE(TAG, "[JSON] Deserialization error %s", error.c_str());
        return UNSET;
    }

    const char * controller_mac_saved = json_doc["controller"] | "null";
    if (strcmp(controller_mac_saved, target_mac_address) == 0){
        ESP_LOGI(TAG, "device found as controller.");
        return CONTROLLER;
    }

    const char * monitor_saved = json_doc["monitor"] | "null";
    if (strcmp(monitor_saved, target_mac_address) == 0){
        ESP_LOGI(TAG, "device found as monitor");
        return MONITOR;
    }

    ESP_LOGI(TAG, "device not found in SPIFFS!");
    return UNSET;
}

esp_err_t add_peer_to_plist(const uint8_t *peer_addr) {      // add pairing
    ESP_LOGI(TAG, "adding new peer to peer list");

    //reset slaveTemplate variable
    memset(&slaveTemplate, 0, sizeof(slaveTemplate));
    //create reference to slaveTemplate memory loc.
    const esp_now_peer_info_t *peer = &slaveTemplate;

    //set values in peer template
    memcpy(slaveTemplate.peer_addr, peer_addr, 6);
    slaveTemplate.channel = 0; // pick a channel.. 0 means it take the current STA channel
    slaveTemplate.encrypt = 0; // no encryption

    // check if the peer exists and remove it from peerlist
    if (esp_now_is_peer_exist(peer_addr) == true) {
        // Slave already paired.
        ESP_LOGI(TAG, "peer already exists, deleting existing data.");
        esp_err_t deleteStatus = esp_now_del_peer(peer_addr);
        if (deleteStatus == ESP_OK) {
            ESP_LOGI(TAG, "peer deleted!");

        } else {
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

void OnDataSent(const esp_now_send_info_t *tx_info, esp_now_send_status_t status) {
    const char* device_serial = print_device_serial(tx_info->des_addr);

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

void OnDataRecv(const esp_now_recv_info_t *rcv_info, const uint8_t * incomingData, int len) { 
    //no response on IDLE state
    if (espnow_connection_state == ESPNOW_IDLE) {
        ESP_LOGI(TAG, "Waiting to finish AP connection. Ignoring data...");
        return;
    }
    const char* mac_str = print_device_mac(rcv_info->src_addr);
    const char* sender_serial = print_device_serial(rcv_info->src_addr);
    ESP_LOGI(TAG, "%d bytes of data received from: %s", len, sender_serial);
    //-get role ID
    PeerRoleID device_role = get_peer_role_from_fs(mac_str);
    // to enable devices to communicate with the server, the user must send a mqtt message with the mac address and the role
    // of the device. Only then, the server can process messages from that mac address.
    // check 'handle_peerlist_update()' function for more information.
    if (device_role == UNSET) {
        ESP_LOGI(TAG, "device is unset in fs. Ignoring data...");
        return;
    }

    uint8_t message_type = incomingData[0];       // first message byte is the type of message 

    switch (message_type) {
    case DATA:
    // the message is data type
        ESP_LOGI(TAG, "message of type DATA arrived.");

        // to.do:
        // create a condition to check if the device's role has been updated. if so, respond the message calling for a new
        // pairing process.

        switch (device_role) {
            case CONTROLLER:
            ESP_LOGI(TAG, "message received from CONTROLLER device.");
            controller_peer_online = true; // set to true when receives a message from the device.
            controller_not_rspnd_count = 0; //resets the counter.
            //- sets global variable.
            memcpy(&controller_data, incomingData, sizeof(controller_data));
            //-breaks
            break;

        case MONITOR:
            ESP_LOGI(TAG, "message received from MONITOR device.");
            monitor_peer_online = true;
            monitor_not_rspnd_count = 0;
            memcpy(&monitor_data, incomingData, sizeof(monitor_data));
            //-breaks
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
        if (pairing_data.sender_role == SERVER) {
            ESP_LOGE(TAG, "message from SERVER device received. wtf!");
            return; // do not replay to server itself.
        }

        pairing_data.sender_role = SERVER;    // server sending a response.
        pairing_data.device_new_role = device_role; // role of the receiver device, stored in filesystem.
        pairing_data.channel = WiFi.channel();  // current WiFi channel.

        if (add_peer_to_plist(rcv_info->src_addr) == ESP_OK){
            ESP_LOGI(TAG, "sending response to peer with PAIRING data.");
            esp_err_t result = esp_now_send(rcv_info->src_addr, (uint8_t *) &pairing_data, sizeof(pairing_data));

            if (result == ESP_OK) {
                ESP_LOGI(TAG, "pairing response sent.");
            } else {
                ESP_LOGE(TAG, "Error sending pairing response, reason: %s",  esp_err_to_name(result));
            };
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
    if (espnow_connection_state == ESPNOW_IDLE) {return;}
    //- check if its time to send a message to the peers
    if (currentMillis - lastEspnowPost > espnowPostingInterval) {postToPeers = true;}
    // check if there is a pending post request.
    if (!postToPeers){return;}

    ESP_LOGI(TAG, "sending message to esp-now peers.");
    esp_now_peer_num number_of_peers;
    esp_now_get_peer_num(&number_of_peers);
    if (number_of_peers.total_num == 0)
    {
        ESP_LOGI(TAG, "peer list is empty. no message sent!");
        postToPeers = false;
        lastEspnowPost = currentMillis;
        return;
    }

    // update config variables.
    espnow_msg_config.msg_type = DATA;
    espnow_msg_config.sender_role = SERVER;
    espnow_msg_config.peers_mode = peersMode;
    espnow_msg_config.system_state = SysState;
    espnow_msg_config.system_temp_sp = activeSetpoint;
    espnow_msg_config.alarm_status = SysFaultState;

    // send data to peers.
    esp_err_t result = esp_now_send(NULL, (uint8_t *) &espnow_msg_config, sizeof(espnow_msg_config));

    if (result == ESP_OK) {
        ESP_LOGI(TAG, "config message sent.");
    } else {
        ESP_LOGE(TAG, "Error sending config msg, reason: %s",  esp_err_to_name(result));
    }

    postToPeers = false;
    lastEspnowPost = currentMillis;

    if (controller_peer_online) {
        controller_not_rspnd_count ++; //increase by one
        //- check if the max is reached.
        if (controller_not_rspnd_count > MAX_NOT_RSPND_TO_OFFLINE) {
            ESP_LOGD(TAG, "%s", "CONTROLLER device is offline.");
            controller_peer_online = false; // after not receive max_number, the device is offline.
        }
    }

    if (monitor_peer_online) {
        monitor_not_rspnd_count ++;
        if (monitor_not_rspnd_count > MAX_NOT_RSPND_TO_OFFLINE) {
            ESP_LOGD(TAG, "%s", "MONITOR device is offline.");
            monitor_peer_online = false;
        }
    }

    //-
    return;
}

esp_err_t clio_espnow_cnf() {
    // esp-now config.

    if (esp_now_init() != ESP_OK) {
        ESP_LOGE(TAG, "-- Error initializing ESP-NOW --");
        return ESP_FAIL;
    }
    // register esp_callbacks.
    esp_now_register_send_cb(OnDataSent);
    esp_now_register_recv_cb(OnDataRecv);
    return ESP_OK;
}