#include "MqttConfig.h"
#include <ArduinoJson.h>
#include <Preferences.h>

MqttSettings activeMqttConfig;

bool saveMqttConfig(const MqttSettings& settings) {
    if (settings.host.isEmpty() || settings.port == 0 ||
        settings.username.isEmpty() || settings.password.isEmpty() ||
        settings.roomId.isEmpty()) {
        return false;
    }

    JsonDocument document;
    document["host"]     = settings.host;
    document["port"]     = settings.port;
    document["username"] = settings.username;
    document["password"] = settings.password;
    document["roomId"]   = settings.roomId;

    String json;
    serializeJson(document, json);

    Preferences preferences;
    if (!preferences.begin("classhub-mqtt", false)) {
        return false;
    }

    size_t written = preferences.putString("config", json);
    preferences.end();

    return written == json.length();
}

bool loadMqttConfig(MqttSettings& settings) {
    Preferences preferences;
    if (!preferences.begin("classhub-mqtt", true)) {
        return false;
    }

    String json = preferences.getString("config", "");
    preferences.end();

    if (json.isEmpty()) {
        return false;
    }

    JsonDocument document;
    if (deserializeJson(document, json)) {
        return false;
    }

    MqttSettings saved;
    saved.host     = document["host"].as<String>();
    saved.port     = document["port"] | 8883;
    saved.username = document["username"].as<String>();
    saved.password = document["password"].as<String>();
    saved.roomId   = document["roomId"].as<String>();

    if (saved.host.isEmpty() || saved.port == 0 ||
        saved.username.isEmpty() || saved.password.isEmpty() ||
        saved.roomId.isEmpty()) {
        return false;
    }

    saved.updateTopics();
    settings = saved;
    return true;
}

void initMqttConfig() {
    if (!loadMqttConfig(activeMqttConfig)) {
        Serial.println("[CONFIG] Khong tim thay NVS. Khoi tao gia tri mac dinh...");
        
        activeMqttConfig.host     = "5f6dd65ef73945c2832e7dd2d5f3f8c4.s1.eu.hivemq.cloud";
        activeMqttConfig.port     = 8883;
        activeMqttConfig.username = "esp32s3";
        activeMqttConfig.password = "Abc@@123";
        activeMqttConfig.roomId   = "DTD201";
        
        activeMqttConfig.updateTopics();
        saveMqttConfig(activeMqttConfig);
    } else {
        Serial.println("[CONFIG] Da load MQTT Config thanh cong tu NVS!");
    }
}