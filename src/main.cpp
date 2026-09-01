#include <Arduino.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <MQTTPubSubClient.h>

#define CABINET_ID   "DTD201"
#define CABINET_NAME "Tu DTD-201"

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 160
#define DRAW_BUF_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT / 10 * 2)

#define SERVO_PIN     37
#define BUTTON_PIN    42
#define LED_PIN       35

#define SERVO_CHANNEL 4
#define SERVO_LOCKED_ANGLE   90
#define SERVO_UNLOCKED_ANGLE 180

const char ssid[] = "Quan Dat";
const char pass[] = "012345678900";


uint8_t stableDoorState = LOW;
uint8_t lastRawDoorState = LOW;
unsigned long lastDoorDebounceTime = 0;
const unsigned long DOOR_DEBOUNCE_DELAY = 50;

const char mqttHost[] = "5f6dd65ef73945c2832e7dd2d5f3f8c4.s1.eu.hivemq.cloud";
const uint16_t mqttPort = 8884;
const char mqttUsername[] = "esp32s3";
const char mqttPassword[] = "Abc@@123";

const String TOPIC_SUB_OTP    = "backend/cabinet/" + String(CABINET_ID) + "/otp";
const String TOPIC_SUB_ACTION = "backend/cabinet/" + String(CABINET_ID) + "/action";
const String TOPIC_PUB_STATUS = "iot/cabinet/" + String(CABINET_ID) + "/status";

TFT_eSPI tft = TFT_eSPI();
WebSocketsClient client;

MQTTPubSubClient mqtt;
//arduino::mqtt::PubSubClient<512, 16> mqtt;

uint8_t draw_buf[DRAW_BUF_SIZE];
lv_obj_t *lb_header;
lv_obj_t *lb_status;
lv_obj_t *lb_otp;
lv_obj_t *lb_network;

struct CabinetState {
    String currentSlipId = "";
    bool isOpen = false;
    bool isOnline = false;
} cabState;

enum PendingAction { 
    ACTION_NONE, 
    ACTION_OPEN, 
    ACTION_LOCK 
};
PendingAction pendingAction = ACTION_NONE;
String pendingSlipId = "";

unsigned long unlockTimestamp = 0;
unsigned long lastHeartbeat = 0;
unsigned long lastReconnectAttempt = 0;
const unsigned long HEARTBEAT_INTERVAL = 15000;
uint32_t lastBtnState = 0;

uint32_t angleToDuty(uint8_t angle) {
    return map(angle, 0, 180, 410, 2048);
}

void setServoAngle(uint8_t angle) {
    ledcWrite(SERVO_CHANNEL, angleToDuty(angle));
}

void displayFlush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    uint32_t w = lv_area_get_width(area);
    uint32_t h = lv_area_get_height(area);
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)px_map, w * h, false);
    tft.endWrite();
    lv_display_flush_ready(disp);
}

void initScreenUI() {
    lv_display_t *disp = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_display_set_flush_cb(disp, displayFlush);
    lv_display_set_buffers(disp, draw_buf, NULL, sizeof(draw_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x0000), LV_PART_MAIN);

    lb_header = lv_label_create(lv_screen_active());
    lv_obj_set_width(lb_header, SCREEN_WIDTH);
    lv_obj_set_style_text_color(lb_header, lv_color_hex(0xFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_align(lb_header, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lb_header, LV_ALIGN_TOP_MID, 0, 8);
    lv_label_set_text_fmt(lb_header, "CLASSHUB\n%s", CABINET_NAME);

    lb_status = lv_label_create(lv_screen_active());
    lv_obj_set_width(lb_status, SCREEN_WIDTH);
    lv_obj_set_style_text_color(lb_status, lv_color_hex(0xFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_align(lb_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lb_status, LV_ALIGN_CENTER, 0, -8);
    lv_label_set_text(lb_status, "Dang khoi dong...");

    lb_otp = lv_label_create(lv_screen_active());
    lv_obj_set_width(lb_otp, SCREEN_WIDTH);
    lv_obj_set_style_text_color(lb_otp, lv_color_hex(0xFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_align(lb_otp, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lb_otp, LV_ALIGN_CENTER, 0, 20);
    lv_label_set_text(lb_otp, "");

    lb_network = lv_label_create(lv_screen_active());
    lv_obj_set_width(lb_network, SCREEN_WIDTH);
    lv_obj_set_style_text_color(lb_network, lv_color_hex(0xFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_align(lb_network, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lb_network, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_label_set_text(lb_network, "MANG: OFFLINE");
}

void sendMqttStatus(bool isOpen, bool isOnline, const String &slipId = "") {
    if (!mqtt.isConnected()) return;

    JsonDocument doc;
    doc["id"] = slipId.length() > 0 ? slipId : cabState.currentSlipId;
    doc["room"] = CABINET_ID;
    doc["isOpen"] = isOpen;
    doc["isOnline"] = isOnline;

    String payload;
    serializeJson(doc, payload);

    mqtt.publish(TOPIC_PUB_STATUS, payload);
    Serial.println("[MQTT Sent -> Status] " + payload);
}

void executeUnlock(const String &slipId = "") {
    cabState.isOpen = true;
    unlockTimestamp = millis();
    if (slipId.length() > 0) cabState.currentSlipId = slipId;

    digitalWrite(LED_PIN, HIGH);
    setServoAngle(SERVO_UNLOCKED_ANGLE);

    sendMqttStatus(true, true);

    lv_label_set_text(lb_status, "CUA DANG MO");
    lv_obj_set_style_text_color(lb_status, lv_color_hex(0xFFFF), LV_PART_MAIN);
    Serial.println(">> [SERVO] DA MO CHOT THANH CONG!");
}

void executeLock() {
    cabState.isOpen = false;

    digitalWrite(LED_PIN, LOW);
    setServoAngle(SERVO_LOCKED_ANGLE);

    sendMqttStatus(false, true);

    lv_label_set_text(lb_status, "DA KHOA");
    lv_label_set_text(lb_otp, "");
    lv_obj_set_style_text_color(lb_status, lv_color_hex(0xFFFF), LV_PART_MAIN);
    Serial.println(">> [SERVO] DA KHOA CHOT THANH CONG!");
}

void handleDoorSensor() {
    
    uint8_t currentRaw = digitalRead(BUTTON_PIN);
    if (currentRaw != lastRawDoorState) {
        lastDoorDebounceTime = millis(); 
    }
    if ((millis() - lastDoorDebounceTime) > DOOR_DEBOUNCE_DELAY) {
        if (currentRaw != stableDoorState) {
            stableDoorState = currentRaw;
            if (stableDoorState == HIGH && cabState.isOpen) {
                Serial.println(">> [SENSOR] Dong cua tu cong tac!");
                pendingAction = ACTION_LOCK;
            }
        }
    }
    lastRawDoorState = currentRaw;
}

void setupMqttCallbacks() {
    // 1. Nhận mã OTP
    mqtt.subscribe(TOPIC_SUB_OTP, [](const String &payload, const size_t size) {
        Serial.println("[MQTT Recv -> OTP] " + payload);

        JsonDocument doc;
        if (deserializeJson(doc, payload)) return;

        String targetRoom = doc["room"] | "";
        if (targetRoom == CABINET_ID || targetRoom == "") {
            String otp = doc["otp"] | "";
            String id = doc["id"] | "";
            cabState.currentSlipId = id;

            if (otp.length() > 0) {
                lv_label_set_text(lb_status, "MA OTP:");
                lv_label_set_text_fmt(lb_otp, "%s", otp.c_str());
                lv_obj_set_style_text_color(lb_status, lv_color_hex(0xFFFF), LV_PART_MAIN);
                Serial.println(">> [OTP HIEN THI] Ma: " + otp);
            }
        }
    });

    // 2. Nhận Action (Mở / Khóa)
    mqtt.subscribe(TOPIC_SUB_ACTION, [](const String &payload, const size_t size) {
        Serial.println("[MQTT Recv -> ACTION] " + payload);

        JsonDocument doc;
        if (deserializeJson(doc, payload)) return;

        String targetRoom = doc["room"] | "";
        String action = doc["action"] | "";
        String id = doc["id"] | "";
        long timestamp = doc["ts"] | 0;

        Serial.printf(">> [CMD] Room: %s | Action: %s | TS: %ld\n", targetRoom.c_str(), action.c_str(), timestamp);

        // Kiểm tra đúng phòng mới xử lý
        if (targetRoom == CABINET_ID || targetRoom == "") {
            if (action == "open") {
                pendingAction = ACTION_OPEN;
                pendingSlipId = id;
            } else if (action == "lock" || action == "close") {
                pendingAction = ACTION_LOCK;
            }
        }
    });
}

void connectMQTT() {
    String uniqueClientId = "ESP32S3_" + String(CABINET_ID);
    Serial.print("Dang ket noi MQTT (" + uniqueClientId + ")... ");

    if (mqtt.connect(uniqueClientId, String(mqttUsername), String(mqttPassword))) {
        Serial.println("THANH CONG!");
        cabState.isOnline = true;

        lv_label_set_text(lb_status, "SAN SANG");
        lv_label_set_text(lb_network, "MANG: ONLINE");
        lv_obj_set_style_text_color(lb_status, lv_color_hex(0xFFFF), LV_PART_MAIN);
        lv_obj_set_style_text_color(lb_network, lv_color_hex(0xFFFF), LV_PART_MAIN);

        setupMqttCallbacks();
        sendMqttStatus(cabState.isOpen, true);
    } else {
        Serial.println("THAT BAI!");
        lv_label_set_text(lb_network, "MANG: OFFLINE");
    }
}

void setup() {
    Serial.begin(115200);
    while(!Serial && millis() < 3000);

    pinMode(BUTTON_PIN, INPUT_PULLDOWN);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    ledcSetup(SERVO_CHANNEL, 50, 14);
    ledcAttachPin(SERVO_PIN, SERVO_CHANNEL);
    setServoAngle(SERVO_LOCKED_ANGLE);

    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);

    lv_init();
    lv_tick_set_cb((uint32_t (*)(void))millis);
    initScreenUI();

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);
    while (WiFi.status() != WL_CONNECTED) {
        delay(400);
        Serial.print(".");
        lv_timer_handler();
    }
    Serial.println("\nWiFi OK!");

    mqtt.begin(client);
    client.beginSSL(mqttHost, mqttPort, "/mqtt");
    client.setReconnectInterval(2000);

    connectMQTT();
}

void loop() {
    client.loop();
    mqtt.update();
    lv_timer_handler();

    if (!mqtt.isConnected() && millis() - lastReconnectAttempt > 5000) {
        lastReconnectAttempt = millis();
        Serial.println("[MQTT] Thu ket noi lai...");
        connectMQTT();
    }

    if (pendingAction == ACTION_OPEN) {
        pendingAction = ACTION_NONE;
        executeUnlock(pendingSlipId);
    } else if (pendingAction == ACTION_LOCK) {
        pendingAction = ACTION_NONE;
        executeLock();
    }

    handleDoorSensor();

    // Gửi Heartbeat 15s/lần
    if (millis() - lastHeartbeat >= HEARTBEAT_INTERVAL) {
        lastHeartbeat = millis();
        if (mqtt.isConnected()) {
            sendMqttStatus(cabState.isOpen, true);
        }
    }

    delay(5);
}