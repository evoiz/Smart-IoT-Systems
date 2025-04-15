#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include <Ticker.h>
#include <DHT.h>

#define MAX_ROOMS 5
#define SENSOR_INTERVAL 2000
#define UDP_PORT 1234
#define ESP_NAME "EnvironmentHub"
#define MAX_GPIO 16
#define JSON_SIZE 128

// const char* ssid = "Kali";
// const char* password = "y=e^(cos(xy))";

const char* ssid = "Hnndes";
const char* password = "58KMCHqR";

struct RoomConfig {
  int8_t dht_pin = -1;
  int8_t gas_pin = -1;
  bool initialized = false;
};

class SafeRoom {
private:
  DHT* dht = nullptr;
  RoomConfig config;

  bool validGPIO(int8_t pin) { return pin >= 0 && pin <= MAX_GPIO; }
  
public:
  float temperature = NAN;
  float humidity = NAN;
  int gas_value = -1;

  SafeRoom() = default;

  bool begin(const RoomConfig& cfg) {
    if(config.initialized) return false;
    
    config = cfg;
    if(validGPIO(config.dht_pin)) {
      dht = new DHT(config.dht_pin, DHT22);
      dht->begin();
    }
    if(validGPIO(config.gas_pin)) {
      pinMode(config.gas_pin, INPUT);
    }
    config.initialized = true;
    return true;
  }

  void read() {
    if(dht) {
      temperature = dht->readTemperature(false);
      humidity = dht->readHumidity(false);
      yield();
    }
    if(validGPIO(config.gas_pin)) {
      gas_value = analogRead(config.gas_pin);
    }
  }

  ~SafeRoom() {
    if(dht) delete dht;
  }
};

SafeRoom rooms[MAX_ROOMS];
uint8_t active_rooms = 0;

ESP8266WebServer server(80);
WiFiUDP udp;
Ticker sensorTicker;

void setupNetwork() {
  WiFi.persistent(true);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while(WiFi.status() != WL_CONNECTED) {
    delay(250);
    yield();
  }
}

void handleCreateRoom();
void handleGetRoom();
void handleUDP();
bool validatePins(int8_t dht, int8_t gas);

void setup() {
  Serial.begin(115200);
  setupNetwork();
  
  server.on("/room", HTTP_POST, handleCreateRoom);
  server.on("/room/{id}", HTTP_GET, handleGetRoom);
  server.begin();

  sensorTicker.attach_ms(SENSOR_INTERVAL, [](){
    for(uint8_t i=0; i<active_rooms; i++) {
      if(rooms[i].temperature || rooms[i].humidity) { // Force no-op optimization
        rooms[i].read();
      }
    }
  });

  udp.begin(UDP_PORT);
  Serial.printf("\nSystem Ready\nIP: %s\n", WiFi.localIP().toString().c_str());
}

void loop() {
  server.handleClient();
  handleUDP();
  yield();
}

void handleGetRoom() {
  uint8_t id = server.pathArg(0).toInt();
  if(id == 0 || id > active_rooms) {
    server.send(400, "text/plain", "Invalid ID");
    return;
  }

  StaticJsonDocument<JSON_SIZE> doc;
  SafeRoom& room = rooms[id-1];
  
  if(!isnan(room.temperature)) doc["temp"] = room.temperature;
  if(!isnan(room.humidity)) doc["hum"] = room.humidity;
  if(room.gas_value != -1) doc["gas"] = room.gas_value;

  char response[JSON_SIZE];
  serializeJson(doc, response, sizeof(response));
  server.send(200, "application/json", response);
}

void handleCreateRoom() {
  StaticJsonDocument<JSON_SIZE> doc;
  if(deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok) {
    server.send(400, "text/plain", "Bad JSON");
    return;
  }

  RoomConfig cfg{
    doc["dht_pin"] | -1,
    doc["gas_pin"] | -1
  };

  if(!validatePins(cfg.dht_pin, cfg.gas_pin) || active_rooms >= MAX_ROOMS) {
    server.send(400, "text/plain", "Invalid pins");
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
  
  char output[JSON_SIZE];
  serializeJson(res, output, sizeof(output));
  server.send(201, "application/json", output);
}

void handleUDP() {
  int packetSize = udp.parsePacket();
  if(packetSize) {
    String message = udp.readStringUntil('\n');
    
    if(message == "DISCOVER_DEVICES") {
      String response = String(ESP_NAME) + "," +
                     WiFi.macAddress() + "," +
                     WiFi.localIP().toString() + "," +
                     String(active_rooms);
      
      udp.beginPacket(udp.remoteIP(), udp.remotePort());
      udp.print(response);
      udp.endPacket();
    }
  }
}

bool validatePins(int8_t dht, int8_t gas) {
  bool valid = true;
  if(dht != -1) valid &= (dht >= 0 && dht <= MAX_GPIO);
  if(gas != -1) valid &= (gas >= 0 && gas <= MAX_GPIO);
  return valid;
}

