#include "WifiConfig.h"
#include "MqttConfig.h"
#include <WiFiManager.h>

bool setupWiFiManager(WiFiManager& wm, bool isEnablePortal) {
    char portStr[6];
    itoa(activeMqttConfig.port, portStr, 10);

    WiFiManagerParameter custom_room_id("room_id", "Ma phong (VD: DTD201)", activeMqttConfig.roomId.c_str(), 20);
    WiFiManagerParameter custom_mqtt_host("mqtt_host", "MQTT Host", activeMqttConfig.host.c_str(), 100);
    WiFiManagerParameter custom_mqtt_port("mqtt_port", "MQTT Port", portStr, 6);
    WiFiManagerParameter custom_mqtt_user("mqtt_user", "MQTT Username", activeMqttConfig.username.c_str(), 40);
    WiFiManagerParameter custom_mqtt_pass("mqtt_pass", "MQTT Password", activeMqttConfig.password.c_str(), 40);

    wm.addParameter(&custom_room_id);
    wm.addParameter(&custom_mqtt_host);
    wm.addParameter(&custom_mqtt_port);
    wm.addParameter(&custom_mqtt_user);
    wm.addParameter(&custom_mqtt_pass);

    wm.setConfigPortalTimeout(360);

    bool res;
    if (isEnablePortal) {
        WiFi.disconnect(false, false); 
        WiFi.mode(WIFI_AP_STA);
        delay(200); 
        res = wm.startConfigPortal("ClassHub-Setup", "classhub123");
    } else {
        res = wm.autoConnect("ClassHub-Setup", "classhub123");
    }

    if (res) {
        activeMqttConfig.roomId = custom_room_id.getValue();
        activeMqttConfig.host = custom_mqtt_host.getValue();
        activeMqttConfig.port = atoi(custom_mqtt_port.getValue());
        activeMqttConfig.username = custom_mqtt_user.getValue();
        activeMqttConfig.password = custom_mqtt_pass.getValue();

        activeMqttConfig.updateTopics();
        saveMqttConfig(activeMqttConfig);
    }
    return res;
}

bool connectConfiguredWifi() {
    WiFiManager wm;
    return setupWiFiManager(wm, false);
}

bool openWifiSetup() {
    WiFiManager wm;
    return setupWiFiManager(wm, true);
}