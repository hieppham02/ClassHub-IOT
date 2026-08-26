#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <MQTTPubSubClient.h>
#include <String.h>
#include <Preferences.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 160
#define SERVO_PIN 37
#define BUTTON_PIN 45
#define LED_PIN 35
#define DRAW_BUF_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT / 10 * 2)

TFT_eSPI tft = TFT_eSPI();
Preferences preferences;
WebSocketsClient client;
MQTTPubSubClient mqtt;

uint8_t draw_buf[DRAW_BUF_SIZE];
lv_obj_t *lb_text;
static uint32_t click_count = 0;
uint32_t lastBtn = 0;

const char ssid[] = "Quan Dat";
const char pass[] = "012345678900";
const char mqttUrl[] = "5f6dd65ef73945c2832e7dd2d5f3f8c4.s1.eu.hivemq.cloud";
const char hiveClientId[] = "ESP32S3";
const char hiveUsername[] = "esp32s3";
const char hivePassword[] = "Abc@@123";
const uint16_t mqttPort = 8884;
//"ESP32S3", "esp32s3", "Abc@@123"
typedef struct LockerState
{
    String id;
    bool isOpen;
    String room;
};

LockerState ls;

void st7735_init();
void lvgl_init();
void displayFlush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
void displayOnScreen();
void mqtt_init();
void closeLocker();
void mqttSendData(String topic, String payload);
void mqttCallbacks();
uint32_t angleToDuty(uint8_t angle);

void st7735_init()
{
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);
}

void lvgl_init()
{
    lv_init();
    lv_tick_set_cb((uint32_t (*)(void))millis);
}

void displayFlush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    uint32_t w = lv_area_get_width(area);
    uint32_t h = lv_area_get_height(area);
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)px_map, w * h, false);
    tft.endWrite();
    lv_display_flush_ready(disp);
}

void displayOnScreen()
{
    lv_display_t *disp = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_display_set_flush_cb(disp, displayFlush);
    lv_display_set_buffers(disp, draw_buf, NULL, sizeof(draw_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x000000), LV_PART_MAIN);

    lb_text = lv_label_create(lv_screen_active());
    lv_obj_set_width(lb_text, 128);
    lv_obj_set_height(lb_text, LV_SIZE_CONTENT);
    lv_obj_set_style_text_color(lb_text, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_label_set_text_fmt(lb_text, "%d", click_count);
    lv_obj_set_style_text_align(lb_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lb_text, LV_ALIGN_TOP_MID, 0, 160 / 2);
    lv_label_set_long_mode(lb_text, LV_LABEL_LONG_MODE_WRAP);
}

void mqtt_init()
{
    Serial.println("Connecting to wifi...");
    lv_label_set_text(lb_text, "Connecting to wifi...");
    lv_timer_handler();

    WiFi.begin(ssid, pass);

    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.print("...");
        delay(500);
        lv_timer_handler();
    }
    Serial.println("\nConnected to WiFi!");
    lv_label_set_text(lb_text, "Connected to WiFi!");
    lv_timer_handler();
    delay(200);

    Serial.println("Connecting to host...");
    lv_label_set_text(lb_text, "Connecting to host...");
    lv_timer_handler();

    mqtt.begin(client);
    client.disconnect();

    const char *maqtt_server = mqttUrl;
    client.beginSSL(maqtt_server, mqttPort, "/mqtt");
    client.setReconnectInterval(2000);

    Serial.println("Connecting to MQTT Broker...");
    lv_label_set_text(lb_text, "Connecting to MQTT Broker...");
    lv_timer_handler();

    while (!mqtt.connect("ESP32S3", "esp32s3", "Abc@@123"))
    {
        Serial.print(".");
        delay(500);
        lv_timer_handler();
    }
    Serial.println("Connected to MQTT Broker!");
    lv_label_set_text(lb_text, "Connected to MQTT Broker!");
    lv_timer_handler();
}

void closeLocker()
{
    uint8_t currentBtn = digitalRead(BUTTON_PIN);
    if (currentBtn == 1 && lastBtn == 0)
    {
        delay(50);
        // if (digitalRead(BUTTON_PIN) == 1) {
        //     click_count++;
        //     preferences.putUInt("count", click_count);
        //     lv_label_set_text_fmt(label_count, "%d", click_count);
        // }
        if (digitalRead(BUTTON_PIN) == 1)
        {
            if (digitalRead(LED_PIN) == 1)
            {
                ls.isOpen = false;
                String payload = "{\"id\":\"" + ls.id + "\", \"room\":\"" + ls.room + "\", \"isOpen\":" + ls.isOpen + "}";
                mqttSendData("tu_thiet_bi/STATUS", payload);
                digitalWrite(LED_PIN, 0);
                ledcWrite(4, angleToDuty(100));
            }
        }
    }
    lastBtn = currentBtn;
}

void mqttSendData(String topic, String payload)
{
    if (mqtt.isConnected())
    {
        mqtt.publish(topic, payload);
        Serial.println("Sent: " + payload);
    }
}

void mqttCallbacks()
{
    // Topic điều khiển led
    mqtt.subscribe("data", [](const String &payload, const size_t size)
                   {
        Serial.print("Received: ");
        Serial.println(payload);

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);

        if(error) {
            Serial.println(error.f_str());
            return;
        }

        JsonObject objLed = doc.as<JsonObject>();
        if(!doc["led"].isNull()) {
            int ledState = doc["led"];
            digitalWrite(LED_PIN, ledState);
            //mqttSendData("led", String(ledState));
        } });

    // Topic nhận OTP
    String topic = "tu_thiet_bi/OTP";
    mqtt.subscribe(topic, [topic](const String &payload, const size_t size)
    {
        Serial.println(topic + ": " + payload);

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);

        if (error) {
            Serial.println(error.f_str());
            return;
        }

        if (!doc["otp"].isNull()) {
            String data = doc["otp"].as<String>();
            lv_label_set_text(lb_text, data.c_str());
        } 
    });

    // Topic
    mqtt.subscribe("tu_thiet_bi/ACTION", [](const String &payload, const size_t size)
                   {
        Serial.print("tu_thiet_bi/ACTION: " + payload);

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);

        if (error) {
            Serial.println(error.f_str());
            return;
        }

        if (!doc["action"].isNull()) {
            String data = doc["action"].as<String>();
            String id = doc["id"].as<String>();
            String room = doc["room"].as<String>();
            ls.id = id;
            ls.room = room;
            if(data == "open"){
                digitalWrite(LED_PIN, 1);
                ledcWrite(4, angleToDuty(180));
            }
            ls.isOpen = true;
            String payload = "{\"id\":\"" + ls.id + "\", \"room\":\"" + ls.room + "\", \"isOpen\":" + ls.isOpen + "}";
            mqttSendData("tu_thiet_bi/STATUS", payload);
            lv_label_set_text(lb_text, "Locker is opening");
        } 
    });
}

uint32_t angleToDuty(uint8_t angle)
{
    return map(angle, 0, 180, 410, 2048);
}

void setup()
{
    Serial.begin(115200);
    while (!Serial && millis() < 3000)
        ;
    st7735_init();
    lvgl_init();
    displayOnScreen();

    mqtt_init();
    mqttCallbacks();

    pinMode(BUTTON_PIN, INPUT);
    pinMode(5, INPUT);
    pinMode(LED_PIN, OUTPUT);

    ledcSetup(4, 50, 14);
    ledcAttachPin(SERVO_PIN, 4);
    ledcWrite(4, angleToDuty(90));

    // analogReadResolution(12);
}

void loop()
{
    closeLocker();
    client.loop();
    mqtt.update();
    lv_timer_handler();
    delay(5);

    // int rawValue = analogRead(6);
    // int angle = map(rawValue, 0, 4095, 0, 180);
    // ledcWrite(4, angleToDuty(angle));
}