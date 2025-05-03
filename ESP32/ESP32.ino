#include <WiFi.h>
#include <WebServer.h>
#include <WiFiUdp.h>
#include <DHT.h>
#include <ArduinoJson.h>

// WiFi credentials
const char *ssid = "Kali";
const char *password = "y=e^(cos(xy))";

// Server and UDP
WebServer server(80);
WiFiUDP udp;
const int UDP_PORT = 1234;

// DHT22 Sensor
#define DHTTYPE DHT22
struct Room
{
  int id;
  int dhtPin;
  int gasPin;
  float temp;
  float hum;
  int gas;
  DHT *dht;
};
Room rooms[5];
int roomCount = 0;

// Task handle for sensor reading
TaskHandle_t sensorTask;

// Connect to WiFi
void connectWiFi()
{
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi");
  Serial.println(WiFi.localIP());
}

// Handle POST /room
void handlePostRoom()
{
  if (!server.hasArg("plain"))
  {
    server.send(400, "text/plain", "No JSON data");
    return;
  }
  String json = server.arg("plain");
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, json);
  if (error)
  {
    server.send(400, "text/plain", "Invalid JSON format");
    return;
  }

  int roomId = doc["room_id"];
  int dhtPin = doc["dht22_pin"];
  int gasPin = doc["gas_pin"];

  if (roomCount >= 5 || dhtPin < 0 || dhtPin > 39 || gasPin < 0 || gasPin > 39 || (dhtPin >= 6 && dhtPin <= 11) || (gasPin >= 6 && gasPin <= 11))
  {
    server.send(400, "text/plain", "Invalid GPIO or room limit reached");
    return;
  }

  for (int i = 0; i < roomCount; i++)
  {
    if (rooms[i].dhtPin == dhtPin || rooms[i].gasPin == gasPin)
    {
      server.send(400, "text/plain", "GPIO conflict");
      return;
    }
  }

  rooms[roomCount].id = roomId;
  rooms[roomCount].dhtPin = dhtPin;
  rooms[roomCount].gasPin = gasPin;
  rooms[roomCount].dht = new DHT(dhtPin, DHTTYPE);
  rooms[roomCount].dht->begin();
  roomCount++;
  server.send(200, "text/plain", "Room created");
}

// Handle GET /room/{id}
void handleGetRoom()
{
  String path = server.uri();
  int roomId = path.substring(6).toInt();
  for (int i = 0; i < roomCount; i++)
  {
    if (rooms[i].id == roomId)
    {
      DynamicJsonDocument doc(1024);
      doc["room_id"] = roomId;
      doc["temperature"] = rooms[i].temp;
      doc["humidity"] = rooms[i].hum;
      doc["gas_level"] = rooms[i].gas;
      String json;
      serializeJson(doc, json);
      server.send(200, "application/json", json);
      return;
    }
  }
  server.send(404, "text/plain", "Room not found");
}

// UDP Discovery
void handleUDP()
{
  char packet[255];
  int packetSize = udp.parsePacket();
  if (packetSize)
  {
    int len = udp.read(packet, 255);
    packet[len] = 0;
    if (strcmp(packet, "DISCOVER") == 0)
    {
      String response = "EnvHub32," + WiFi.macAddress() + "," + WiFi.localIP().toString() + "," + String(roomCount);
      udp.beginPacket(udp.remoteIP(), udp.remotePort());
      udp.print(response);
      udp.endPacket();
    }
  }
}

// Sensor reading task
void readSensors(void *pvParameters)
{
  for (;;)
  {
    for (int i = 0; i < roomCount; i++)
    {
      rooms[i].temp = rooms[i].dht->readTemperature();
      rooms[i].hum = rooms[i].dht->readHumidity();
      rooms[i].gas = analogRead(rooms[i].gasPin);
    }
    vTaskDelay(2000 / portTICK_PERIOD_MS); // Every 2 seconds
  }
}

void setup()
{
  connectWiFi();
  server.on("/room", HTTP_POST, handlePostRoom);
  server.on("/room/[0-5]", HTTP_GET, handleGetRoom);
  server.begin();
  udp.begin(UDP_PORT);
  xTaskCreatePinnedToCore(readSensors, "SensorTask", 10000, NULL, 1, &sensorTask, 1);
}

void loop()
{
  server.handleClient();
  handleUDP();
}