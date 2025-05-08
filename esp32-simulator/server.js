const express = require("express");
const dgram = require("dgram");
const app = express();
const udpServer = dgram.createSocket("udp4");

// Middleware to parse JSON bodies
app.use(express.json());

// Get ports from .env or use defaults
const HTTP_PORT = process.env.HTTP_PORT || 3000;
const UDP_PORT = process.env.UDP_PORT || 1234;

// Simulated MAC and IP (for UDP discovery)
const MAC_ADDRESS = "00:1A:2B:3C:4D:5E";
const IP_ADDRESS = `127.0.0.1:${HTTP_PORT}`;

// Enhanced room data structure to match ESP32 implementation
let rooms = [];
const MAX_ROOMS = 16; // Match ESP32 MAX_ROOMS

// Default pins for rooms (similar to ESP32 code)
const defaultActuatorPins = [2, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26];
const defaultDHTPins = [32, 33, 34, 35, 36, 39, 32, 33, 34, 35, 36, 39, 32, 33, 34, 35];
const defaultGasPins = [36, 39, 32, 33, 34, 35, 36, 39, 32, 33, 34, 35, 36, 39, 32, 33];

// Default config similar to ESP32 structure
let config = {
  wifi: {
    ssid: "evoiz",
    password: "eee1998eee"
  },
  rooms: []
};

// Initialize default rooms configuration
for (let i = 0; i < MAX_ROOMS; i++) {
  config.rooms.push({
    id: i + 1,
    name: `Room ${i + 1}`,
    actuatorPin: defaultActuatorPins[i],
    dhtPin: defaultDHTPins[i],
    gasPin: defaultGasPins[i],
    active: false
  });
  
  // Also populate rooms array with inactive rooms
  rooms.push({
    id: i + 1,
    name: `Room ${i + 1}`,
    actuatorPin: defaultActuatorPins[i],
    dhtPin: defaultDHTPins[i],
    gasPin: defaultGasPins[i],
    active: false,
    temperature: 25.0,
    humidity: 50.0,
    gas_level: 300
  });
}
// UDP Discovery Server
udpServer.on("message", (msg, rinfo) => {
  if (msg.toString() === "DISCOVER") {
    // Count active rooms
    const activeCount = rooms.filter(room => room.active).length;
    const response = `EnvHub32,${MAC_ADDRESS},${IP_ADDRESS},${activeCount}`;
    udpServer.send(response, rinfo.port, rinfo.address, (err) => {
      if (err) console.error("UDP send error:", err);
      else console.log(`Sent discovery response to ${rinfo.address}:${rinfo.port}`);
    });
  }
});

// UDP Server setup
udpServer.bind(UDP_PORT, () => {
  console.log(`UDP server listening on port ${UDP_PORT}`);
});

// HTTP: GET /config
app.get("/config", (req, res) => {
  console.log("[HTTP] Route: GET /config");
  res.json(config);
});

// HTTP: POST /config
app.post("/config", (req, res) => {
  console.log("[HTTP] Route: POST /config");
  const newConfig = req.body;
  
  if (!newConfig) {
    return res.status(400).send("No JSON data provided");
  }
  
  // Update config
  config = newConfig;
  
  // Update room data based on new config
  if (config.rooms && Array.isArray(config.rooms)) {
    config.rooms.forEach(configRoom => {
      const roomIndex = rooms.findIndex(r => r.id === configRoom.id);
      if (roomIndex >= 0) {
        rooms[roomIndex] = {
          ...rooms[roomIndex],
          ...configRoom
        };
      }
    });
  }
  
  res.status(200).send("Config updated");
});

// HTTP: GET /room/{id}
app.get("/room/:id", (req, res) => {
  const id = parseInt(req.params.id);
  console.log(`[HTTP] Route: GET /room/${id}`);
  
  const room = rooms.find(r => r.id === id);
  
  if (!room) {
    return res.status(404).send("Room not found");
  }
  
  res.json({
    room_id: room.id,
    temperature: room.temperature,
    humidity: room.humidity,
    gas_level: room.gas_level,
    active: room.active
  });
});

// HTTP: GET /room (list of active room IDs)
app.get("/room", (req, res) => {
  console.log("[HTTP] Route: GET /room");
  
  const activeRooms = rooms.filter(room => room.active).map(room => room.id);
  
  res.json({
    active_rooms: activeRooms,
    active_count: activeRooms.length
  });
});

// HTTP: POST /room/{id}/activate
app.post("/room/:id/activate", (req, res) => {
  const id = parseInt(req.params.id);
  console.log(`[HTTP] Route: POST /room/${id}/activate`);
  
  const roomIndex = rooms.findIndex(r => r.id === id);
  
  if (roomIndex === -1) {
    return res.status(404).send("Room not found");
  }
  
  rooms[roomIndex].active = true;
  
  // Also update in config
  const configRoomIndex = config.rooms.findIndex(r => r.id === id);
  if (configRoomIndex >= 0) {
    config.rooms[configRoomIndex].active = true;
  }
  
  res.status(200).send("Room activated");
});

// HTTP: POST /room/{id}/deactivate
app.post("/room/:id/deactivate", (req, res) => {
  const id = parseInt(req.params.id);
  console.log(`[HTTP] Route: POST /room/${id}/deactivate`);
  
  const roomIndex = rooms.findIndex(r => r.id === id);
  
  if (roomIndex === -1) {
    return res.status(404).send("Room not found");
  }
  
  rooms[roomIndex].active = false;
  
  // Also update in config
  const configRoomIndex = config.rooms.findIndex(r => r.id === id);
  if (configRoomIndex >= 0) {
    config.rooms[configRoomIndex].active = false;
  }
  
  res.status(200).send("Room deactivated");
});

// HTTP: GET /active-rooms (new endpoint from ESP32.ino)
app.get("/active-rooms", (req, res) => {
  console.log("[HTTP] Route: GET /active-rooms");
  
  const activeRooms = rooms.filter(room => room.active).map(room => ({
    room_id: room.id,
    name: room.name,
    temperature: room.temperature,
    humidity: room.humidity,
    gas_level: room.gas_level
  }));
  
  res.json({
    rooms: activeRooms
  });
});

// Simulate sensor readings (update every 2 seconds)
setInterval(() => {
  rooms.forEach((room) => {
    if (room.active) {  // Only update active rooms
      // Generate random sensor data within realistic ranges
      room.temperature = parseFloat((20 + Math.random() * 10).toFixed(1)); // 20–30°C
      room.humidity = parseFloat((30 + Math.random() * 40).toFixed(1)); // 30–70%
      room.gas_level = Math.floor(100 + Math.random() * 900); // 100–1000 ppm
    }
  });
}, 2000);

// Start HTTP server
app.listen(HTTP_PORT, () => {
  console.log(`HTTP server listening on port ${HTTP_PORT}`);
});