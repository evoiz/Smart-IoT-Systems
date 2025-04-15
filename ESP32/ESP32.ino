#include <WiFi.h>
#include <AsyncUDP.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include "esp_system.h"

#define MAX_ROOMS 5
#define SENSOR_INTERVAL 2000
#define UDP_PORT 1234
#define ESP_NAME "EnvHub32"
#define MIN_GPIO 0
#define MAX_GPIO 39  // ESP32 has more GPIO pins
#define JSON_SIZE 256
#define DHT_TYPE DHT22

// WiFi Credentials
const char* ssid = "esp32";
const char* password = "123456789";

struct RoomConfig {
  int8_t dht_pin = -1;
  int8_t gas_pin = -1;
  bool initialized = false;

  bool validate() const {
    bool valid = true;
    if(dht_pin != -1) valid &= (dht_pin >= MIN_GPIO && dht_pin <= MAX_GPIO);
    if(gas_pin != -1) valid &= (gas_pin >= MIN_GPIO && gas_pin <= MAX_GPIO);
    return valid;
  }
};

class ESP32Room {
private:
  DHT* dht = nullptr;
  RoomConfig config;
  hw_timer_t* sensorTimer = nullptr;
  
  bool validGPIO(int8_t pin) { 
    return (pin >= MIN_GPIO && pin <= MAX_GPIO) && 
          !(pin >= 6 && pin <= 11);  // ESP32 flash pins
  }

public:
  float temperature = NAN;
  float humidity = NAN;
  int gas_value = -1;

  ESP32Room() = default;

  bool begin(const RoomConfig& cfg) {
    if(config.initialized || !validateConfig(cfg)) return false;
    
    config = cfg;
    if(validGPIO(config.dht_pin)) {
      dht = new DHT(config.dht_pin, DHT_TYPE);
      if(dht == nullptr) {
        Serial.println("Failed to allocate DHT object");
        return false;
      }
      dht->begin();
    }
    if(validGPIO(config.gas_pin)) {
      analogReadResolution(12);  // ESP32 ADC 0-4095
      pinMode(config.gas_pin, ANALOG);
    }
    config.initialized = true;
    return true;
  }

  void read() {
    if(dht) {
      temperature = dht->readTemperature();
      humidity = dht->readHumidity();
    }
    if(validGPIO(config.gas_pin)) {
      gas_value = analogRead(config.gas_pin);
    }
  }

  ~ESP32Room() {
    if(dht) delete dht;
    if(sensorTimer) timerEnd(sensorTimer);
  }

private:
  bool validateConfig(const RoomConfig& cfg) {
    bool valid = true;
    if(cfg.dht_pin != -1) valid &= validGPIO(cfg.dht_pin);
    if(cfg.gas_pin != -1) valid &= validGPIO(cfg.gas_pin);
    return valid;
  }
};

ESP32Room rooms[MAX_ROOMS];
uint8_t active_rooms = 0;
WebServer server(80);
AsyncUDP udp;

void handleCreateRoom();
void handleGetRoom();
void handleUDPPacket(AsyncUDPPacket packet);
void initWiFi();
void sensorTask(void* parameter);

void setup() {
  Serial.begin(115200);
  initWiFi();

  // Sensor task pinned to core 0
  xTaskCreatePinnedToCore(
    sensorTask,
    "SensorTask",
    4096,
    nullptr,
    1,
    nullptr,
    0
  );

  // Web server on core 1
  server.on("/room", HTTP_POST, handleCreateRoom);
  server.on("/room/{id}", HTTP_GET, handleGetRoom);
  server.begin();

  // UDP setup
  if(udp.listen(UDP_PORT)) {
    udp.onPacket(handleUDPPacket);
  }

  Serial.printf("\nESP32 Ready\nIP: %s\n", WiFi.localIP().toString().c_str());
}

void loop() {
  server.handleClient();
  delay(1);  // Minimal delay for watchdog
}

void initWiFi() {
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.softAP(ssid, password);
}

void sensorTask(void* parameter) {
  while(true) {
    for(uint8_t i=0; i<active_rooms; i++) {
      if(rooms[i].temperature || rooms[i].humidity) {  // Force no optimization
        rooms[i].read();
      }
    }
    delay(SENSOR_INTERVAL);
    yield();
  }
}

void handleGetRoom() {
  uint8_t id = server.pathArg(0).toInt();
  if(id == 0 || id > active_rooms) {
    server.send(400, "text/plain", "Invalid ID");
    return;
  }

  StaticJsonDocument<JSON_SIZE> doc;
  ESP32Room& room = rooms[id-1];
  
  if(!isnan(room.temperature)) doc["temp"] = room.temperature;
  if(!isnan(room.humidity)) doc["hum"] = room.humidity;
  if(room.gas_value != -1) doc["gas"] = room.gas_value;

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleCreateRoom() {
  StaticJsonDocument<JSON_SIZE> doc;
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  
  if(err) {
    server.send(400, "text/plain", "Bad JSON");
    return;
  }

  RoomConfig cfg{
    doc["dht_pin"] | -1,
    doc["gas_pin"] | -1
  };

  if(active_rooms >= MAX_ROOMS || !cfg.validate()) {
    server.send(400, "text/plain", "Invalid config");
    return;
  }

  if(!rooms[active_rooms].begin(cfg)) {
    server.send(500, "text/plain", "Init failed");
    return;
  }

  active_rooms++;
  
  StaticJsonDocument<JSON_SIZE> res;
  res["id"] = active_rooms;
  res["status"] = "created";
  
  String output;
  serializeJson(res, output);
  server.send(201, "application/json", output);
}

void handleUDPPacket(AsyncUDPPacket packet) {
  if(packet.length() >= 8 && memcmp(packet.data(), "DISCOVER", 8) == 0) {
    String response = String(ESP_NAME) + "|" +
                     WiFi.macAddress() + "|" +
                     WiFi.localIP().toString() + "|" +
                     String(active_rooms);
                     
    packet.printf(response.c_str());
  }
}