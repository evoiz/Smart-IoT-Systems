#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include <Ticker.h>
#include <DHT.h>

#define MAX_ROOMS 5
#define SENSOR_INTERVAL 2000
#define UDP_INTERVAL 100
#define ESP_NAME "SmartEnvironmentHub"

//const char* ssid = "Kali";
//const char* password = "y=e^(cos(Xy))";

//const char* ssid = "Hnndes";
//const char* password = "58KMCHqR";

const char* ssid = "evoiz";
const char* password = "eee1998eee";

class Room {
  private:
    int dhtPin;
    int gasPin;
    DHT* dht = nullptr;
    
  public:
    float temperature;
    float humidity;
    int gasValue;
    
    Room(int dht_pin = -1, int gas_pin = -1) : 
      dhtPin(dht_pin), gasPin(gas_pin) {
      if (dht_pin != -1) {
        dht = new DHT(dht_pin, DHT22);
      }
    }

    ~Room() {
      if (dht != nullptr) {
        delete dht;
      }
    }

    void begin() {
      pinMode(gasPin, INPUT);
      if (dht != nullptr) {
        dht->begin();
      }
    }

    void readSensors() {
      if (dht != nullptr) {
        temperature = dht->readTemperature();
        humidity = dht->readHumidity();
      } else {
        temperature = -1;
        humidity = -1;
      }
      gasValue = analogRead(gasPin);
      
      if (isnan(temperature) || isnan(humidity)) {
        temperature = -1;
        humidity = -1;
      }
    }
};

Room rooms[MAX_ROOMS];
int active_rooms = 0;

ESP8266WebServer server(80);
WiFiUDP udp;
Ticker sensorTicker;
Ticker udpTicker;

void handleCreateRoom();
void handleRoomRequest();
void checkUDPMessages();

void setup() {
  Serial.begin(115200);
  
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);
  Serial.println("Connected! IP: " + WiFi.localIP().toString());

  server.on("/createRoom", HTTP_POST, handleCreateRoom);
  server.on("/room{room_num}", HTTP_GET, handleRoomRequest);
  server.begin();

  sensorTicker.attach_ms(SENSOR_INTERVAL, []() {
    for(int i=0; i<active_rooms; i++) rooms[i].readSensors();
  });

  udpTicker.attach_ms(UDP_INTERVAL, []() {
    checkUDPMessages();
  });
  udp.begin(1234);
}

void loop() {
  server.handleClient();
  if (WiFi.status() != WL_CONNECTED) WiFi.reconnect();
}

void handleRoomRequest() {
  int room_num = server.uri().substring(server.uri().lastIndexOf('m')+1).toInt();
  if(room_num <1 || room_num >active_rooms) {
    server.send(404, "text/plain", "Invalid room number");
    return;
  }

  DynamicJsonDocument doc(256);
  Room& room = rooms[room_num-1];
  doc["room"] = room_num;
  doc["temperature"] = room.temperature;
  doc["humidity"] = room.humidity;
  doc["gas"] = room.gasValue;

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void checkUDPMessages() {
  int packetSize = udp.parsePacket();
  String message = udp.readStringUntil('\n');
  if(packetSize && message == "DISCOVER_DEVICES") {
    String response = String(ESP_NAME) + "," +
                     WiFi.macAddress() + "," +
                     WiFi.localIP().toString() + "," +
                     String(active_rooms);
    
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.print(response);
    udp.endPacket();
  }
}

void handleCreateRoom() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }

  DynamicJsonDocument doc(256);
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  
  if (error) {
    server.send(400, "text/plain", "Invalid JSON");
    return;
  }

  int dht_pin = doc["dht_pin"];
  int gas_pin = doc["gas_pin"];
  
  if (active_rooms >= MAX_ROOMS) {
    server.send(507, "application/json", "{\"status\":\"MAX_ROOMS_REACHED\"}");
    return;
  }

  rooms[active_rooms] = Room(dht_pin, gas_pin);
  rooms[active_rooms].begin();
  active_rooms++;

  DynamicJsonDocument responseDoc(256);
  responseDoc["status"] = "SUCCESS";
  responseDoc["room_number"] = active_rooms;
  responseDoc["message"] = "Room created successfully";
  
  String response;
  serializeJson(responseDoc, response);
  server.send(201, "application/json", response);
}