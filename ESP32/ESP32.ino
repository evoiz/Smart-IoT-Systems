#include <WiFi.h>
#include <WebServer.h>
#include <WiFiUdp.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <uri/UriBraces.h>  
#include <uri/UriRegex.h>     

// Config file path on SPIFFS
const char* configPath = "/config.json";

// WiFi credentials (loaded from config)
String ssid;
String password;

// Server and UDP
WebServer server(80);
WiFiUDP udp;
const int UDP_PORT = 1234;

// DHT Sensor type
#define DHTTYPE DHT22

// Maximum rooms
#define MAX_ROOMS 16

// Default pins for each room (actuator, DHT, gas)
const int defaultActuatorPins[MAX_ROOMS] = {2, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26};
const int defaultDHTPins[MAX_ROOMS]      = {32, 33, 34, 35, 36, 39, 32, 33, 34, 35, 36, 39, 32, 33, 34, 35};
const int defaultGasPins[MAX_ROOMS]      = {36, 39, 32, 33, 34, 35, 36, 39, 32, 33, 34, 35, 36, 39, 32, 33};

// Room configuration and sensor structure
struct RoomConfig {
  int id;
  String name;
  int actuatorPin;
  int dhtPin;
  int gasPin;
  bool active;
  DHT* dht;
  float temp;
  float hum;
  int gas;
};

RoomConfig rooms[MAX_ROOMS];
int roomCount = 0;

// Create default config.json in SPIFFS
void createDefaultConfig() {
  DynamicJsonDocument doc(4096);
  doc["wifi"]["ssid"] = "evoiz";
  doc["wifi"]["password"] = "eee1998eee";

  JsonArray roomArray = doc.createNestedArray("rooms");
  for (int i = 0; i < MAX_ROOMS; i++) {
    JsonObject obj = roomArray.createNestedObject();
    obj["id"] = i + 1;
    obj["name"] = "Room " + String(i + 1);
    obj["actuatorPin"] = defaultActuatorPins[i];
    obj["dhtPin"] = defaultDHTPins[i];
    obj["gasPin"] = defaultGasPins[i];
    obj["active"] = false;
  }

  File f = SPIFFS.open(configPath, FILE_WRITE);
  if (f) {
    serializeJson(doc, f);
    f.close();
    Serial.println("Default config created");
  } else {
    Serial.println("Failed to create default config");
  }
}

// Load config from SPIFFS or create default
bool loadConfig() {
  if (!SPIFFS.exists(configPath)) {
    Serial.println("Config not found, creating default");
    createDefaultConfig();
  }
  File f = SPIFFS.open(configPath, FILE_READ);
  if (!f) {
    Serial.println("Failed to open config, recreating default");
    createDefaultConfig();
    return loadConfig();
  }
  DynamicJsonDocument doc(4096);
  auto err = deserializeJson(doc, f);
  f.close();
  if (err) {
    Serial.println("Config parse error, recreating default");
    SPIFFS.remove(configPath);
    createDefaultConfig();
    return loadConfig();
  }
  ssid = doc["wifi"]["ssid"].as<String>();
  password = doc["wifi"]["password"].as<String>();
  JsonArray arr = doc["rooms"].as<JsonArray>();
  roomCount = arr.size();
  for (int i = 0; i < roomCount && i < MAX_ROOMS; i++) {
    rooms[i].id = arr[i]["id"].as<int>();
    rooms[i].name = arr[i]["name"].as<String>();
    rooms[i].actuatorPin = arr[i]["actuatorPin"].as<int>();
    rooms[i].dhtPin = arr[i]["dhtPin"].as<int>();
    rooms[i].gasPin = arr[i]["gasPin"].as<int>();
    rooms[i].active = arr[i]["active"].as<bool>();
    rooms[i].dht = new DHT(rooms[i].dhtPin, DHTTYPE);
    rooms[i].dht->begin();
  }
  return true;
}

// Save config back to SPIFFS
void saveConfig() {
  DynamicJsonDocument doc(4096);
  doc["wifi"]["ssid"] = ssid;
  doc["wifi"]["password"] = password;
  JsonArray roomArray = doc.createNestedArray("rooms");
  for (int i = 0; i < roomCount; i++) {
    JsonObject obj = roomArray.createNestedObject();
    obj["id"] = rooms[i].id;
    obj["name"] = rooms[i].name;
    obj["actuatorPin"] = rooms[i].actuatorPin;
    obj["dhtPin"] = rooms[i].dhtPin;
    obj["gasPin"] = rooms[i].gasPin;
    obj["active"] = rooms[i].active;
  }
  File f = SPIFFS.open(configPath, FILE_WRITE);
  if (f) {
    serializeJson(doc, f);
    f.close();
  }
}

// Helper to log route and params
void logRequest(const String& route) {
  Serial.print("[HTTP] Route: ");
  Serial.println(route);
  if (server.args() > 0) {
    for (int i = 0; i < server.args(); i++) {
      Serial.print("  Param: ");
      Serial.print(server.argName(i));
      Serial.print(" = ");
      Serial.println(server.arg(i));
    }
  }
}

// HTTP: POST /config
void handlePostConfig() {
  logRequest("POST /config");  // log route
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "No JSON data provided");
    return;
  }
  String json = server.arg("plain");
  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, json) == DeserializationError::Ok) {
    SPIFFS.remove(configPath);
    File f = SPIFFS.open(configPath, FILE_WRITE);
    serializeJson(doc, f);
    f.close();
    server.send(200, "text/plain", "Config updated");
  } else {
    server.send(400, "text/plain", "Invalid JSON");
  }
}

// HTTP: GET /config
void handleGetConfig() {
  logRequest("GET /config");  // log route
  File f = SPIFFS.open(configPath, FILE_READ);
  if (!f) {
    server.send(500, "application/json", "{\"error\":\"Config not available\"}");
    return;
  }
  String json;
  while (f.available()) json += char(f.read());
  f.close();
  server.send(200, "application/json", json);
}

// HTTP: GET /room/{id}
void handleGetRoom() {
  int id = server.pathArg(0).toInt();
  logRequest("GET /room/" + String(id));  // log route
  for (int i = 0; i < roomCount; i++) {
    if (rooms[i].id == id) {
      DynamicJsonDocument doc(4096);
      doc["room_id"] = id;
      doc["temperature"] = rooms[i].temp;
      doc["humidity"] = rooms[i].hum;
      doc["gas_level"] = rooms[i].gas;
      doc["active"] = rooms[i].active;
      String out;
      serializeJson(doc, out);
      server.send(200, "application/json", out);
      return;
    }
  }
  server.send(404, "text/plain", "Room not found");
}

// HTTP: GET /room
// Modified to return list of active room IDs
void handleGetRoomInfo() {
  logRequest("GET /room");  // log route
  
  DynamicJsonDocument doc(1024);
  JsonArray activeRooms = doc.createNestedArray("active_rooms");
  
  int activeCount = 0;
  for (int i = 0; i < roomCount; i++) {
    if (rooms[i].active) {
      activeRooms.add(rooms[i].id);
      activeCount++;
    }
  }
  
  doc["active_count"] = activeCount;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

// HTTP: POST /room/{id}/activate
void handleActivateRoom() {
  int id = server.pathArg(0).toInt();
  logRequest("POST /room/" + String(id) + "/activate");  // log route
  for (int i = 0; i < roomCount; i++) {
    if (rooms[i].id == id) {
      rooms[i].active = true;
      digitalWrite(rooms[i].actuatorPin, HIGH);
      saveConfig();
      server.send(200, "text/plain", "Room activated");
      return;
    }
  }
  server.send(404, "text/plain", "Room not found");
}

// HTTP: POST /room/{id}/deactivate
void handleDeactivateRoom() {
  int id = server.pathArg(0).toInt();
  logRequest("POST /room/" + String(id) + "/deactivate");  // log route
  for (int i = 0; i < roomCount; i++) {
    if (rooms[i].id == id) {
      rooms[i].active = false;
      digitalWrite(rooms[i].actuatorPin, LOW);
      saveConfig();
      server.send(200, "text/plain", "Room deactivated");
      return;
    }
  }
  server.send(404, "text/plain", "Room not found");
}

// HTTP: GET /active-rooms
// New endpoint to return all active room data at once
void handleGetActiveRooms() {
  logRequest("GET /active-rooms");  // log route
  
  DynamicJsonDocument doc(4096);
  JsonArray roomsArray = doc.createNestedArray("rooms");
  
  for (int i = 0; i < roomCount; i++) {
    if (rooms[i].active) {
      JsonObject roomObj = roomsArray.createNestedObject();
      roomObj["room_id"] = rooms[i].id;
      roomObj["name"] = rooms[i].name;
      roomObj["temperature"] = rooms[i].temp;
      roomObj["humidity"] = rooms[i].hum;
      roomObj["gas_level"] = rooms[i].gas;
    }
  }
  
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

// UDP Discovery Handler
void handleUDP() {
  char packet[255];
  int packetSize = udp.parsePacket();
  if (packetSize) {
    int len = udp.read(packet, 255);
    packet[len] = 0;
    if (strcmp(packet, "DISCOVER") == 0) {
      int activeCount = 0;
      for (int i = 0; i < roomCount; i++) if (rooms[i].active) activeCount++;
      String response = "EnvHub32-real," + WiFi.macAddress() + "," + WiFi.localIP().toString() + "," + String(activeCount);
      udp.beginPacket(udp.remoteIP(), udp.remotePort());
      udp.print(response);
      udp.endPacket();
    }
  }
}

// Sensor reading task: skip inactive rooms
void readSensors(void *pvParameters) {
  for (;;) {
    for (int i = 0; i < roomCount; i++) {
      if (!rooms[i].active) continue;  // skip if inactive
      float t = rooms[i].dht->readTemperature();
      float h = rooms[i].dht->readHumidity();
      int g = analogRead(rooms[i].gasPin);
      if (!isnan(t) && !isnan(h)) {
        rooms[i].temp = t;
        rooms[i].hum = h;
        rooms[i].gas = g;
      }
    }
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }
}

void setup() {
  Serial.begin(115200);
  delay(3000);

  SPIFFS.format();
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed");
    return;
  }

  loadConfig();
  Serial.println("Config loaded");

  for (int i = 0; i < roomCount; i++) {
    pinMode(rooms[i].actuatorPin, OUTPUT);
    digitalWrite(rooms[i].actuatorPin, rooms[i].active ? HIGH : LOW);
  }

  WiFi.begin(ssid.c_str(), password.c_str());
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
  }
  Serial.println("\nConnected, IP: " + WiFi.localIP().toString());

  server.on("/config", HTTP_POST, handlePostConfig);
  server.on("/config", HTTP_GET, handleGetConfig);
  server.on(UriBraces("/room/{}"), HTTP_GET, handleGetRoom);
  server.on("/room", HTTP_GET, handleGetRoomInfo);
  server.on(UriBraces("/room/{}/activate"), HTTP_POST, handleActivateRoom);
  server.on(UriBraces("/room/{}/deactivate"), HTTP_POST, handleDeactivateRoom);
  server.on("/active-rooms", HTTP_GET, handleGetActiveRooms); // New endpoint for all active room data
  server.begin();
  udp.begin(UDP_PORT);

  xTaskCreatePinnedToCore(readSensors, "SensorTask", 10000, NULL, 1, NULL, 1);
}

void loop() {
  server.handleClient();
  handleUDP();
}