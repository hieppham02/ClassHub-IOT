#include <Arduino.h>
#include <ArduinoJson.h>
#include <MQTTPubSubClient.h>
#include <TFT_eSPI.h>
#include <WiFiClientSecure.h>
#include <WiFi.h>
#include <lvgl.h>
#include "secret.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 160
#define DRAW_BUFFER_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT / 10 * 2)

#define SERVO_PIN 37
#define DOOR_SENSOR_PIN 42 //button :))
#define LED_PIN 35

#define SERVO_CHANNEL 4
#define SERVO_LOCKED_ANGLE 90
#define SERVO_UNLOCKED_ANGLE 180

const unsigned long DOOR_DEBOUNCE_DELAY_MS = 50;
const unsigned long HEARTBEAT_INTERVAL_MS = 15000;
const unsigned long MQTT_RECONNECT_INTERVAL_MS = 5000;
const unsigned long OTP_DISPLAY_DURATION_MS = 180000;

TFT_eSPI tft;
WiFiClientSecure mqttTransport;
MQTTPubSub::PubSubClient<256> mqttClient;

uint8_t drawBuffer[DRAW_BUFFER_SIZE];
lv_obj_t *headerLabel;
lv_obj_t *statusLabel;
lv_obj_t *otpLabel;
lv_obj_t *networkLabel;

struct CabinetState {
    String currentSessionId = "";
    bool isUnlocked = false;
    bool isOnline = false;
};

CabinetState cabinetState;

enum PendingAction {
    ACTION_NONE,
    ACTION_OPEN,
    ACTION_LOCK
};

PendingAction pendingAction = ACTION_NONE;
String pendingSessionId = "";

uint8_t stableDoorState = LOW;
uint8_t lastRawDoorState = LOW;
unsigned long lastDoorDebounceTime = 0;
unsigned long lastHeartbeatTime = 0;
unsigned long lastMqttReconnectAttempt = 0;
unsigned long otpReceivedTime = 0;

uint32_t angleToDuty(uint8_t angle) {
    return map(angle, 0, 180, 410, 2048);
}

void setServoAngle(uint8_t angle) {
    ledcWrite(SERVO_CHANNEL, angleToDuty(angle));
}

void displayFlush(lv_display_t *display, const lv_area_t *area, uint8_t *pixelMap) {
    uint32_t width = lv_area_get_width(area);
    uint32_t height = lv_area_get_height(area);

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, width, height);
    tft.pushColors(reinterpret_cast<uint16_t *>(pixelMap), width * height, false);
    tft.endWrite();

    lv_display_flush_ready(display);
}

void initializeScreen() {
    lv_display_t *display = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_display_set_flush_cb(display, displayFlush);
    lv_display_set_buffers(display, drawBuffer, nullptr, sizeof(drawBuffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x0000), LV_PART_MAIN);

    headerLabel = lv_label_create(lv_screen_active());
    lv_obj_set_width(headerLabel, SCREEN_WIDTH);
    lv_obj_set_style_text_color(headerLabel, lv_color_hex(0xFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_align(headerLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(headerLabel, LV_ALIGN_TOP_MID, 0, 8);
    lv_label_set_text_fmt(headerLabel, "CLASSHUB\n%s", CABINET_NAME);

    statusLabel = lv_label_create(lv_screen_active());
    lv_obj_set_width(statusLabel, SCREEN_WIDTH);
    lv_obj_set_style_text_color(statusLabel, lv_color_hex(0xFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_align(statusLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(statusLabel, LV_ALIGN_CENTER, 0, -8);
    lv_label_set_text(statusLabel, "Dang khoi dong...");

    otpLabel = lv_label_create(lv_screen_active());
    lv_obj_set_width(otpLabel, SCREEN_WIDTH);
    lv_obj_set_style_text_color(otpLabel, lv_color_hex(0xFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_align(otpLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(otpLabel, LV_ALIGN_CENTER, 0, 20);
    lv_label_set_text(otpLabel, "");

    networkLabel = lv_label_create(lv_screen_active());
    lv_obj_set_width(networkLabel, SCREEN_WIDTH);
    lv_obj_set_style_text_color(networkLabel, lv_color_hex(0xFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_align(networkLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(networkLabel, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_label_set_text(networkLabel, "MANG: OFFLINE");
}

String readMessageId(JsonVariantConst value) {
    if (value.is<const char *>()) return String(value.as<const char *>());
    if (value.is<int>()) return String(value.as<int>());
    if (value.is<long>()) return String(value.as<long>());
    return "";
}

bool isValidOtp(const String &otp) {
    if (otp.length() != 6) return false;

    for (size_t index = 0; index < otp.length(); index++) {
        if (!isDigit(otp[index])) return false;
    }

    return true;
}

String createHeartbeatPayload(bool isOnline) {
    JsonDocument document;
    document["room"] = CABINET_ID;
    document["isOnline"] = isOnline;

    String payload;
    serializeJson(document, payload);
    return payload;
}

void publishHeartbeat() {
    if (!mqttClient.isConnected()) return;

    String payload = createHeartbeatPayload(true);
    bool published = mqttClient.publish(MQTT_TOPIC_HEARTBEAT, payload, true, 1);

    if (published) {
        Serial.println("[MQTT Sent -> Heartbeat] " + payload);
    } else {
        Serial.println("[MQTT] Gui heartbeat that bai.");
    }
}

void publishStatus(const String &sessionId = "") {
    if (!mqttClient.isConnected()) return;

    String currentSessionId = sessionId.length() > 0 ? sessionId : cabinetState.currentSessionId;

    JsonDocument document;
    document["id"] = currentSessionId;
    document["room"] = CABINET_ID;
    document["isOpen"] = cabinetState.isUnlocked;
    document["isOnline"] = true;
    document["lockState"] = cabinetState.isUnlocked ? "UNLOCKED" : "LOCKED";

    String payload;
    serializeJson(document, payload);

    bool published = mqttClient.publish(MQTT_TOPIC_STATUS, payload, false, 1);

    if (published) {
        Serial.println("[MQTT Sent -> Status] " + payload);
    } else {
        Serial.println("[MQTT] Gui status that bai.");
    }
}

void executeUnlock(const String &sessionId = "") {
    if (sessionId.length() > 0) cabinetState.currentSessionId = sessionId;

    cabinetState.isUnlocked = true;
    otpReceivedTime = 0;

    digitalWrite(LED_PIN, HIGH);
    setServoAngle(SERVO_UNLOCKED_ANGLE);

    lv_label_set_text(statusLabel, "CUA DANG MO");
    lv_label_set_text(otpLabel, "");

    publishStatus();
    Serial.println(">> [SERVO] DA MO CHOT THANH CONG!");
}

void executeLock() {
    cabinetState.isUnlocked = false;
    otpReceivedTime = 0;

    digitalWrite(LED_PIN, LOW);
    setServoAngle(SERVO_LOCKED_ANGLE);

    lv_label_set_text(statusLabel, "DA KHOA");
    lv_label_set_text(otpLabel, "");

    publishStatus();
    Serial.println(">> [SERVO] DA KHOA CHOT THANH CONG!");
}

void handleDoorSensor() {
    uint8_t currentRawState = digitalRead(DOOR_SENSOR_PIN);

    if (currentRawState != lastRawDoorState) lastDoorDebounceTime = millis();

    if (millis() - lastDoorDebounceTime > DOOR_DEBOUNCE_DELAY_MS && currentRawState != stableDoorState) {
        stableDoorState = currentRawState;

        if (stableDoorState == HIGH && cabinetState.isUnlocked) {
            Serial.println(">> [SENSOR] Da dong cua tu.");
            pendingAction = ACTION_LOCK;
        }
    }

    lastRawDoorState = currentRawState;
}

void handleOtpMessage(const String &payload, const size_t size) {
    (void)size;
    Serial.println("[MQTT Recv -> OTP] " + payload);

    JsonDocument document;
    DeserializationError error = deserializeJson(document, payload);

    if (error) {
        Serial.println("[MQTT] Payload OTP khong phai JSON hop le.");
        return;
    }

    String targetRoom = document["room"] | "";
    if (targetRoom != CABINET_ID) {
        Serial.println("[MQTT] Bo qua OTP khong thuoc tu nay.");
        return;
    }

    String otp = document["otp"] | "";
    if (!isValidOtp(otp)) {
        Serial.println("[MQTT] OTP khong hop le.");
        return;
    }

    cabinetState.currentSessionId = readMessageId(document["id"]);
    otpReceivedTime = millis();

    lv_label_set_text(statusLabel, "MA OTP:");
    lv_label_set_text_fmt(otpLabel, "%s", otp.c_str());

    Serial.println(">> [OTP HIEN THI] Ma: " + otp);
}

void handleActionMessage(const String &payload, const size_t size) {
    (void)size;
    Serial.println("[MQTT Recv -> Action] " + payload);

    JsonDocument document;
    DeserializationError error = deserializeJson(document, payload);

    if (error) {
        Serial.println("[MQTT] Payload action khong phai JSON hop le.");
        return;
    }

    String targetRoom = document["room"] | "";
    if (targetRoom != CABINET_ID) {
        Serial.println("[MQTT] Bo qua action khong thuoc tu nay.");
        return;
    }

    String action = document["action"] | "";
    String sessionId = readMessageId(document["id"]);
    long timestamp = document["ts"] | 0;

    Serial.printf(">> [CMD] Room: %s | Action: %s | TS: %ld\n", targetRoom.c_str(), action.c_str(), timestamp);

    if (action == "open") {
        pendingSessionId = sessionId;
        pendingAction = ACTION_OPEN;
    } else if (action == "lock" || action == "close") {
        pendingAction = ACTION_LOCK;
    } else {
        Serial.println("[MQTT] Action khong duoc ho tro.");
    }
}

void subscribeToMqttTopics() {
    bool otpSubscribed = mqttClient.subscribe(MQTT_TOPIC_OTP, 1, handleOtpMessage);
    bool actionSubscribed = mqttClient.subscribe(MQTT_TOPIC_ACTION, 1, handleActionMessage);

    if (otpSubscribed && actionSubscribed) {
        Serial.println("[MQTT] Da subscribe OTP va action.");
    } else {
        Serial.println("[MQTT] Subscribe topic that bai.");
    }
}

void connectMqtt() {
    if (WiFi.status() != WL_CONNECTED || mqttClient.isConnected()) return;

    if (!mqttTransport.connected()) {
        Serial.print("Dang ket noi TLS toi HiveMQ... ");
        
        mqttTransport.setHandshakeTimeout(10); // 10s timeout
        
        if (!mqttTransport.connect(MQTT_HOST, MQTT_PORT)) {
            Serial.println("KET NOI TLS THAT BAI!");
            cabinetState.isOnline = false;
            lv_label_set_text(networkLabel, "MANG: OFFLINE");
            return;
        }
        Serial.println("TLS OK!");
    }

    String clientId = "ESP32S3_" + String(CABINET_ID);
    Serial.print("Dang ket noi MQTT (" + clientId + ")... ");

    if (!mqttClient.connect(clientId, String(MQTT_USERNAME), String(MQTT_PASSWORD))) {
        cabinetState.isOnline = false;
        lv_label_set_text(networkLabel, "MANG: OFFLINE");
        Serial.println("THAT BAI!");
        mqttTransport.stop();
        return;
    }

    cabinetState.isOnline = true;
    lastHeartbeatTime = millis();

    lv_label_set_text(statusLabel, "SAN SANG");
    lv_label_set_text(networkLabel, "MANG: ONLINE");

    subscribeToMqttTopics();
    publishHeartbeat();
    publishStatus();

    Serial.println("THANH CONG!");
}

void handleOtpExpiration() {
    if (otpReceivedTime == 0 || millis() - otpReceivedTime < OTP_DISPLAY_DURATION_MS) return;

    otpReceivedTime = 0;
    lv_label_set_text(statusLabel, "MA OTP HET HAN");
    lv_label_set_text(otpLabel, "");
}

static uint32_t get_lv_tick() {
    return millis();
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {}

    pinMode(DOOR_SENSOR_PIN, INPUT_PULLDOWN);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    ledcSetup(SERVO_CHANNEL, 50, 14);
    ledcAttachPin(SERVO_PIN, SERVO_CHANNEL);
    setServoAngle(SERVO_LOCKED_ANGLE);

    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);

    lv_init();
    lv_tick_set_cb(get_lv_tick);
    initializeScreen();

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    while (WiFi.status() != WL_CONNECTED) {
        delay(400);
        Serial.print(".");
        lv_timer_handler();
    }

    Serial.println("\nWiFi OK!");

    mqttTransport.setInsecure();
    mqttClient.begin(mqttTransport);
    mqttClient.setWill(MQTT_TOPIC_HEARTBEAT, createHeartbeatPayload(false), true, 1);

    connectMqtt();
}

void loop() {
    mqttClient.update();
    lv_timer_handler();

    if (!mqttClient.isConnected()) {
        cabinetState.isOnline = false;
        lv_label_set_text(networkLabel, "MANG: OFFLINE");

        if (millis() - lastMqttReconnectAttempt >= MQTT_RECONNECT_INTERVAL_MS) {
            lastMqttReconnectAttempt = millis();
            Serial.println("[MQTT] Thu ket noi lai...");
            connectMqtt();
        }
    }

    if (pendingAction == ACTION_OPEN) {
        String sessionId = pendingSessionId;
        pendingSessionId = "";
        pendingAction = ACTION_NONE;
        executeUnlock(sessionId);
    } else if (pendingAction == ACTION_LOCK) {
        pendingAction = ACTION_NONE;
        executeLock();
    }

    handleDoorSensor();
    handleOtpExpiration();

    if (mqttClient.isConnected() && millis() - lastHeartbeatTime >= HEARTBEAT_INTERVAL_MS) {
        lastHeartbeatTime = millis();
        publishHeartbeat();
    }

    delay(5);
}
