const express = require("express");
const dgram = require("dgram");
const app = express();
const udpServer = dgram.createSocket("udp4");

// Middleware to parse JSON bodies
app.use(express.json());

// Get ports from .env or use defaults
const HTTP_PORT = process.env.HTTP_PORT || 3000;
const UDP_PORT = process.env.UDP_PORT || 1234;

// Simulated room data
let rooms = [];
const MAX_ROOMS = 5;

// Simulated MAC and IP (for UDP discovery)
const MAC_ADDRESS = "00:1A:2B:3C:4D:5E";
const IP_ADDRESS = `127.0.0.1:${HTTP_PORT}`;

// UDP Discovery Server
udpServer.on("message", (msg, rinfo) => {
  if (msg.toString() === "DISCOVER") {
    const response = `EnvHub32,${MAC_ADDRESS},${IP_ADDRESS},${rooms.length}`;
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


// POST /room - Create a new room
app.post("/room", (req, res) => {
  const { room_id, dht22_pin, gas_pin } = req.body;

  // Validation
  if (!room_id || !dht22_pin || !gas_pin) {
    return res.status(400).send("Missing required fields");
  }
  if (rooms.length >= MAX_ROOMS) {
    return res.status(400).send("Room limit reached");
  }
  if (dht22_pin < 0 || dht22_pin > 39 || gas_pin < 0 || gas_pin > 39) {
    return res.status(400).send("Invalid GPIO pins");
  }
  if ((dht22_pin >= 6 && dht22_pin <= 11) || (gas_pin >= 6 && gas_pin <= 11)) {
    return res.status(400).send("GPIO pins in reserved range (6-11)");
  }
  if (rooms.some((room) => room.dht22_pin === dht22_pin || room.gas_pin === gas_pin)) {
    return res.status(400).send("GPIO conflict");
  }

  // Add room
  rooms.push({
    id: room_id,
    dht22_pin,
    gas_pin,
    temperature: 25.0, // Initial values
    humidity: 50.0,
    gas_level: 300,
  });

  console.log(`Created room ${room_id}`);
  res.status(200).send("Room created");
});

// GET /room/:id - Get room sensor data
app.get("/room/:id", (req, res) => {
  const roomId = parseInt(req.params.id);
  const room = rooms.find((r) => r.id === roomId);

  if (!room) {
    return res.status(404).send("Room not found");
  }

  res.json({
    room_id: room.id,
    temperature: room.temperature,
    humidity: room.humidity,
    gas_level: room.gas_level,
  });
});

// Simulate sensor readings (update every 2 seconds)
setInterval(() => {
  rooms.forEach((room) => {
    // Generate random sensor data within realistic ranges
    room.temperature = (20 + Math.random() * 10).toFixed(1); // 20–30°C
    room.humidity = (30 + Math.random() * 40).toFixed(1); // 30–70%
    room.gas_level = Math.floor(100 + Math.random() * 900); // 100–1000 ppm
  });
}, 2000);

// Start HTTP server
// HTTP Server setup
app.listen(HTTP_PORT, () => {
  console.log(`HTTP server listening on port ${HTTP_PORT}`);
});
