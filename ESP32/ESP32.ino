#include <WiFi.h>
#include <WebServer.h>
#include <WiFiUdp.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>

// Server and UDP
WebServer server(80);
WiFiUDP udp;
const int UDP_PORT = 1234;

// DHT22 Sensor
#define DHTTYPE DHT22
struct Room {
  int id;
  int dhtPin;
  int gasPin;
  float temp;
  float hum;
  int gas;
  bool active;
  DHT *dht;
};
Room rooms[5];
int roomCount = 0;

// Task handle for sensor reading
TaskHandle_t sensorTask;

// Global WiFi settings
String ssid;
String password;
String default_ssid="default_ssid";
String default_password="default_password";


// Connect to WiFi
void connectWiFi() {
  Serial.begin(115200);
  WiFi.begin(ssid.c_str(), password.c_str());
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi");
  Serial.println(WiFi.localIP());
}

// Load configuration from SPIFFS
void loadConfig() {
  // Check if config file exists
  if (!SPIFFS.exists("/config.json")) {
    Serial.println("Config file not found, creating default configuration");
    // Create default JSON configuration
    DynamicJsonDocument doc(1024);
    doc["wifi"]["ssid"] = default_ssid;
    doc["wifi"]["password"] = default_password;
    JsonArray roomsArray = doc.createNestedArray("rooms");

    // Save default configuration to SPIFFS
    File file = SPIFFS.open("/config.json", "w");
    if (!file) {
      Serial.println("Failed to create config file");
      return;
    }
    serializeJson(doc, file);
    file.close();
    Serial.println("Default configuration saved successfully");
  }

  // Load configuration from file
  File file = SPIFFS.open("/config.json", "r");
  if (!file) {
    Serial.println("Failed to open config file");
    ssid = default_ssid;
    password = default_password;
    roomCount = 0;
    return;
  }
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, file);
  if (error) {
    Serial.println("Failed to parse config JSON");
    ssid = default_ssid;
    password = default_password;
    roomCount = 0;
    file.close();
    return;
  }
  Serial.println("successful to parse config JSON");
  ssid = doc["wifi"]["ssid"].as<String>();
  password = doc["wifi"]["password"].as<String>();
  JsonArray roomsArray = doc["rooms"];
  roomCount = roomsArray.size();
  for (int i = 0; i < roomCount && i < 5; i++) {
    rooms[i].id = roomsArray[i]["id"];
    rooms[i].dhtPin = roomsArray[i]["dhtPin"];
    rooms[i].gasPin = roomsArray[i]["gasPin"];
    rooms[i].active = roomsArray[i]["active"];
    rooms[i].dht = new DHT(rooms[i].dhtPin, DHTTYPE);
    rooms[i].dht->begin();
  }
  file.close();
}

// Save configuration to SPIFFS
void saveConfig() {
  DynamicJsonDocument doc(1024);
  doc["wifi"]["ssid"] = ssid;
  doc["wifi"]["password"] = password;
  JsonArray roomsArray = doc.createNestedArray("rooms");
  for (int i = 0; i < roomCount; i++) {
    JsonObject room = roomsArray.createNestedObject();
    room["id"] = rooms[i].id;
    room["dhtPin"] = rooms[i].dhtPin;
    room["gasPin"] = rooms[i].gasPin;
    room["active"] = rooms[i].active;
  }
  File file = SPIFFS.open("/config.json", "w");
  if (!file) {
    Serial.println("Failed to open config file for writing");
    return;
  }
  serializeJson(doc, file);
  file.close();
}

// Handle POST /config
void handlePostConfig() {
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "No JSON data provided");
    return;
  }
  String json = server.arg("plain");
  File file = SPIFFS.open("/config.json", "w");
  if (!file) {
    server.send(500, "text/plain", "Failed to save config");
    return;
  }
  file.print(json);
  file.close();
  server.send(200, "text/plain", "Config updated, restarting");
  delay(1000);
  ESP.restart();
}

// Handle POST /room/:id/activate
void handleActivateRoom() {
  String idStr = server.pathArg(0);
  int id = idStr.toInt();
  for (int i = 0; i < roomCount; i++) {
    if (rooms[i].id == id) {
      rooms[i].active = true;
      saveConfig();
      server.send(200, "text/plain", "Room activated");
      return;
    }
  }
  server.send(404, "text/plain", "Room not found");
}

// Handle POST /room/:id/deactivate
void handleDeactivateRoom() {
  String idStr = server.pathArg(0);
  int id = idStr.toInt();
  for (int i = 0; i < roomCount; i++) {
    if (rooms[i].id == id) {
      rooms[i].active = false;
      saveConfig();
      server.send(200, "text/plain", "Room deactivated");
      return;
    }
  }
  server.send(404, "text/plain", "Room not found");
}

// Handle GET /room/:id
void handleGetRoom() {
  String idStr = server.pathArg(0);
  int id = idStr.toInt();
  for (int i = 0; i < roomCount; i++) {
    if (rooms[i].id == id) {
      DynamicJsonDocument doc(1024);
      doc["room_id"] = id;
      doc["temperature"] = rooms[i].temp;
      doc["humidity"] = rooms[i].hum;
      doc["gas_level"] = rooms[i].gas;
      doc["active"] = rooms[i].active;
      String json;
      serializeJson(doc, json);
      server.send(200, "application/json", json);
      return;
    }
  }
  server.send(404, "text/plain", "Room not found");
}

void handleGetRoomInfo() {
  DynamicJsonDocument doc(1024);
  doc["rooms"] = roomCount;
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);

}

// UDP Discovery
void handleUDP() {
  char packet[255];
  int packetSize = udp.parsePacket();
  if (packetSize) {
    int len = udp.read(packet, 255);
    packet[len] = 0;
    if (strcmp(packet, "DISCOVER") == 0) {
      String response = "EnvHub32-real," + WiFi.macAddress() + "," + WiFi.localIP().toString() + "," + String(roomCount);
      udp.beginPacket(udp.remoteIP(), udp.remotePort());
      udp.print(response);
      udp.endPacket();
    }
  }
}

// Sensor reading task
void readSensors(void *pvParameters) {
  for (;;) {
    for (int i = 0; i < roomCount; i++) {
      if (rooms[i].active) {
        rooms[i].temp = rooms[i].dht->readTemperature();
        rooms[i].hum = rooms[i].dht->readHumidity();
        rooms[i].gas = analogRead(rooms[i].gasPin);
      }
    }
    vTaskDelay(2000 / portTICK_PERIOD_MS); // Every 2 seconds
  }
}

void setup() {
  SPIFFS.begin(true); // Initialize SPIFFS, format if mount fails
  loadConfig();
  connectWiFi();
  server.on("/config", HTTP_POST, handlePostConfig);
  server.on("/room/{id}", HTTP_GET, handleGetRoom);
  server.on("/room", HTTP_GET, handleGetRoomInfo);
  server.on("/room/{id}/activate", HTTP_POST, handleActivateRoom);
  server.on("/room/{id}/deactivate", HTTP_POST, handleDeactivateRoom);
  server.begin();
  udp.begin(UDP_PORT);
  xTaskCreatePinnedToCore(readSensors, "SensorTask", 10000, NULL, 1, &sensorTask, 1);
}

void loop() {
  server.handleClient();
  handleUDP();
}