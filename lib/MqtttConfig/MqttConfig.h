#pragma once

#include <Arduino.h>

struct MqttTopics {
    String otp;
    String action;
    String status;
    String heartbeat;
};

struct MqttSettings {
    String host;
    uint16_t port = 8883;
    String username;
    String password;
    String roomId;
    MqttTopics topics;

    void updateTopics() {
        topics.otp       = "backend/cabinet/" + roomId + "/otp";
        topics.action    = "backend/cabinet/" + roomId + "/action";
        topics.status    = "iot/cabinet/" + roomId + "/status";
        topics.heartbeat = "iot/cabinet/" + roomId + "/heartbeat";
    }
};

extern MqttSettings activeMqttConfig;

bool saveMqttConfig(const MqttSettings& settings);
bool loadMqttConfig(MqttSettings& settings);
void initMqttConfig();