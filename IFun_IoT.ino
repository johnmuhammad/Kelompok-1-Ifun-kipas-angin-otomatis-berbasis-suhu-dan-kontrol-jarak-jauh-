// === LIBRARY ===
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <PubSubClient.h>

// === WiFi & Telegram ===
const char* ssid = "Redmi 10";
const char* password = "Mukhliss98";
const char* telegramToken = "8072334984:AAEQrVL3Sj2a_uqw86iBJEFgjycvGm6P7D8";

// === MQTT HiveMQ Cloud ===
const char* mqttServer = "dff77b874b2e406db1a0a51e8113f2d8.s1.eu.hivemq.cloud";
const int mqttPort = 8883;
const char* mqttUser = "Ifun034";
const char* mqttPass = "Ifun0349";

// === PIN & DEVICE ===
#define LED_PIN 2
#define FAN_PIN 18
#define DHT_PIN 4
#define DHT_TYPE DHT11

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
DHT dht(DHT_PIN, DHT_TYPE);

// === NETWORK OBJECT ===
WiFiClientSecure client;
WiFiClientSecure secureClient;
UniversalTelegramBot bot(telegramToken, client);
PubSubClient mqtt(secureClient);

// === FLAG & TIMER ===
bool autoFanEnabled = false;
unsigned long lastSensorRead = 0;
unsigned long lastTimeBotRan = 0;
const unsigned long sensorInterval = 5000;
int botRequestDelay = 1000;

// === OLED HELPERS ===
String truncateText(String text, int maxWidth, int textSize) {
  display.setTextSize(textSize);
  int16_t x1, y1;
  uint16_t w, h;
  while (text.length() > 0) {
    display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    if (w <= maxWidth) break;
    text = text.substring(0, text.length() - 1);
  }
  return text;
}

void updateOLED(String status, String message, float temp = -1, float hum = -1) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(2, 2);
  display.println("IoT Control");
  display.drawLine(0, 12, 127, 12, SSD1306_WHITE);

  display.setCursor(2, 16);
  display.print("LED: ");
  display.print(status);
  (status == "ON") ? display.fillCircle(118, 18, 4, SSD1306_WHITE) : display.drawCircle(118, 18, 4, SSD1306_WHITE);

  if (temp >= 0 && hum >= 0) {
    display.setCursor(2, 26);
    display.print("Temp: ");
    display.print(temp, 1);
    display.print(" C");

    display.setCursor(2, 36);
    display.print("Hum: ");
    display.print(hum, 1);
    display.print(" %");
  }

  display.setCursor(2, temp >= 0 ? 46 : 26);
  display.print("Msg: ");
  display.print(truncateText(message, 100, 1));

  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
  display.display();
}

// === MQTT CALLBACK ===
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) message += (char)payload[i];
  String topicStr = String(topic);

  if (topicStr == "home/led") {
    digitalWrite(LED_PIN, message == "ON" ? HIGH : LOW);
    mqtt.publish("home/led/state", digitalRead(LED_PIN) ? "ON" : "OFF", true);
    updateOLED(digitalRead(LED_PIN) ? "ON" : "OFF", "LED via MQTT");
  }

  else if (topicStr == "home/fan") {
    if (message == "ON") {
      autoFanEnabled = true;
      digitalWrite(LED_PIN, HIGH);
      float temp = dht.readTemperature();
      float hum = dht.readHumidity();
      if (!isnan(temp) && !isnan(hum)) {
        if (temp > 30) {
          digitalWrite(FAN_PIN, LOW);
          mqtt.publish("home/fan/state", "ON", true);
          updateOLED("ON", "Fan ON (Auto)", temp, hum);
        } else {
          digitalWrite(FAN_PIN, HIGH);
          mqtt.publish("home/fan/state", "OFF", true);
          updateOLED("ON", "Fan OFF (Auto)", temp, hum);
        }
        mqtt.publish("home/temperature", String(temp).c_str(), true);
        mqtt.publish("home/humidity", String(hum).c_str(), true);
      } else {
        updateOLED("ON", "Sensor Error");
      }
      mqtt.publish("home/led/state", "ON", true);
    } else if (message == "OFF") {
      autoFanEnabled = false;
      digitalWrite(FAN_PIN, HIGH);
      digitalWrite(LED_PIN, LOW);
      mqtt.publish("home/fan/state", "OFF", true);
      mqtt.publish("home/led/state", "OFF", true);
      updateOLED("OFF", "Manual OFF");
    } else if (message == "AUTO") {
      autoFanEnabled = true;
      updateOLED("FAN", "Set to AUTO");
    }
  }

  else if (topicStr == "home/sensor") {
    if (message == "read") {
      float temp = dht.readTemperature();
      float hum = dht.readHumidity();
      if (!isnan(temp) && !isnan(hum)) {
        mqtt.publish("home/temperature", String(temp).c_str(), true);
        mqtt.publish("home/humidity", String(hum).c_str(), true);
        updateOLED(digitalRead(LED_PIN) ? "ON" : "OFF", "Sensor Read", temp, hum);
      } else {
        updateOLED(digitalRead(LED_PIN) ? "ON" : "OFF", "Sensor Error");
      }
    }
  }
}

// === TELEGRAM ===
void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = String(bot.messages[i].chat_id);
    String text = bot.messages[i].text;
    String from_name = bot.messages[i].from_name;
    if (from_name == "") from_name = "Guest";

    if (text == "/on") {
      digitalWrite(LED_PIN, HIGH);
      autoFanEnabled = true;
      float temp = dht.readTemperature();
      float hum = dht.readHumidity();
      String message = "LED menyala dan kipas otomatis aktif!\n";
      if (!isnan(temp) && !isnan(hum)) {
        message += "Suhu: " + String(temp, 1) + " °C\nKelembapan: " + String(hum, 1) + " %\n";
        if (temp > 30) {
          digitalWrite(FAN_PIN, LOW);
          message += "Kipas: ON (otomatis)";
          mqtt.publish("home/fan", "ON");
        } else {
          digitalWrite(FAN_PIN, HIGH);
          message += "Kipas: OFF (otomatis)";
          mqtt.publish("home/fan", "OFF");
        }
      } else {
        message += "Gagal membaca sensor!";
      }
      bot.sendMessage(chat_id, message, "");
      updateOLED("ON", "Fan AUTO ENABLED", temp, hum);
      mqtt.publish("home/led", "ON");
    }

    else if (text == "/off") {
      digitalWrite(LED_PIN, LOW);
      autoFanEnabled = false;
      digitalWrite(FAN_PIN, HIGH);
      bot.sendMessage(chat_id, "LED dan kipas dimatikan (manual)!", "");
      updateOLED("OFF", "Fan AUTO DISABLED");
      mqtt.publish("home/led", "OFF");
    }

    else if (text == "/sensor") {
      float temp = dht.readTemperature();
      float hum = dht.readHumidity();
      if (isnan(temp) || isnan(hum)) {
        bot.sendMessage(chat_id, "Gagal membaca sensor DHT11!", "");
        updateOLED(digitalRead(LED_PIN) ? "ON" : "OFF", "Sensor error");
      } else {
        String message = "Suhu: " + String(temp, 1) + " °C\nKelembapan: " + String(hum, 1) + " %";
        if (autoFanEnabled) {
          if (temp > 30) {
            digitalWrite(FAN_PIN, LOW);
            message += "\nKipas: ON (otomatis)";
            mqtt.publish("home/fan", "ON");
          } else {
            digitalWrite(FAN_PIN, HIGH);
            message += "\nKipas: OFF (otomatis)";
            mqtt.publish("home/fan", "OFF");
          }
        } else {
          digitalWrite(FAN_PIN, HIGH);
          message += "\nKipas: OFF (manual)";
          mqtt.publish("home/fan", "OFF");
        }
        bot.sendMessage(chat_id, message, "");
        updateOLED(digitalRead(LED_PIN) ? "ON" : "OFF", "Sensor read", temp, hum);
      }
    }

    else if (text == "/start") {
      String welcome = "Selamat datang, " + from_name + ".\nGunakan perintah berikut:\n";
      welcome += "/on : Menyalakan LED & aktifkan kipas otomatis\n";
      welcome += "/off : Mematikan LED & kipas\n";
      welcome += "/sensor : Membaca suhu & kelembapan\n";
      bot.sendMessage(chat_id, welcome, "");
      updateOLED(digitalRead(LED_PIN) ? "ON" : "OFF", "Welcome message sent");
    }

    else {
      bot.sendMessage(chat_id, "Perintah tidak dikenal. Gunakan /start untuk info.", "");
      updateOLED(digitalRead(LED_PIN) ? "ON" : "OFF", "Unknown command");
    }
  }
}

// === SETUP ===
void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(FAN_PIN, HIGH);
  dht.begin();

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(20, 25);
  display.println("Booting...");
  display.display();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);

  client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
  secureClient.setInsecure();
  mqtt.setServer(mqttServer, mqttPort);
  mqtt.setCallback(mqttCallback);

  while (!mqtt.connected()) {
    if (mqtt.connect("ESP32Client", mqttUser, mqttPass)) {
      mqtt.subscribe("home/led");
      mqtt.subscribe("home/fan");
      mqtt.subscribe("home/sensor");
    } else {
      delay(2000);
    }
  }

  mqtt.publish("home/status", "ESP32 started", true);
  mqtt.publish("home/led/state", "OFF", true);
  mqtt.publish("home/fan/state", "OFF", true);
  updateOLED("OFF", "Boot OK");
}

// === LOOP ===
void loop() {
  
  if (millis() - lastSensorRead > sensorInterval) {
    lastSensorRead = millis();
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t) && !isnan(h)) {
      if (autoFanEnabled) {
        digitalWrite(FAN_PIN, t > 30 ? LOW : HIGH);
        mqtt.publish("home/fan/state", t > 30 ? "ON" : "OFF", true);
      }
      mqtt.publish("home/temperature", String(t).c_str(), true);
      mqtt.publish("home/humidity", String(h).c_str(), true);
    }
  }


  if (millis() - lastTimeBotRan > botRequestDelay) {
    int newMsg = bot.getUpdates(bot.last_message_received + 1);
    while (newMsg) {
      handleNewMessages(newMsg);
      newMsg = bot.getUpdates(bot.last_message_received + 1);
    }
    lastTimeBotRan = millis();
  }

  mqtt.loop();
}
