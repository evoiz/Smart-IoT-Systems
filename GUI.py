import sys
import socket
import logging
from PyQt6.QtWidgets import QApplication, QMainWindow, QPushButton, QTableWidget, QTableWidgetItem, QVBoxLayout, QWidget, QMessageBox, QDialog, QTextEdit
from PyQt6.QtCore import QThread, pyqtSignal, QTimer, Qt
import requests
import json

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s',
    handlers=[logging.StreamHandler(sys.stdout)]
)
logger = logging.getLogger(__name__)

class NetworkThread(QThread):
    devicesFound = pyqtSignal(list)

    def run(self):
        logger.info("Starting network discovery")
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(2)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        sock.sendto("DISCOVER".encode(), ("255.255.255.255", 1234))
        devices = []
        try:
            while True:
                data, addr = sock.recvfrom(1024)
                parts = data.decode().split(",")
                devices.append({"name": parts[0], "mac": parts[1], "ip": parts[2], "rooms": int(parts[3])})
        except socket.timeout:
            pass
        sock.close()
        logger.info(f"Discovered {len(devices)} devices")
        self.devicesFound.emit(devices)

class RoomDataDialog(QDialog):
    def __init__(self, ip, parent=None):
        super().__init__(parent)
        self.setWindowTitle(f"Active Room Data for {ip}")
        self.setGeometry(200, 200, 400, 300)
        layout = QVBoxLayout(self)

        # Text area to display room data
        self.text_edit = QTextEdit()
        self.text_edit.setReadOnly(True)
        layout.addWidget(self.text_edit)

        # Fetch and display active room data only
        self.fetch_active_room_data(ip)

    def fetch_active_room_data(self, ip):
        """Fetch only active room data from the device"""
        try:
            # First, get list of active room IDs
            response = requests.get(f"http://{ip}/room", timeout=5)
            response.raise_for_status()
            room_info = response.json()
            
            if "active_rooms" not in room_info or len(room_info["active_rooms"]) == 0:
                self.text_edit.setText("No active rooms found.")
                logger.info(f"No active rooms found at {ip}")
                return
                
            data_text = ""
            active_room_ids = room_info["active_rooms"]
            logger.info(f"Found {len(active_room_ids)} active rooms at {ip}: {active_room_ids}")
            
            # Option 1: Use the new endpoint to get all active room data at once
            try:
                response = requests.get(f"http://{ip}/active-rooms", timeout=5)
                if response.status_code == 200:
                    all_data = response.json()
                    for room in all_data.get("rooms", []):
                        data_text += (f"Room {room['room_id']}:\n"
                                      f"  Temperature: {room['temperature']}°C\n"
                                      f"  Humidity: {room['humidity']}%\n"
                                      f"  Gas Level: {room['gas_level']} ppm\n\n")
                    self.text_edit.setText(data_text)
                    return
            except Exception as e:
                logger.warning(f"Could not use /active-rooms endpoint, falling back to individual queries: {str(e)}")
            
            # Option 2: Fetch each active room individually
            for room_id in active_room_ids:
                try:
                    response = requests.get(f"http://{ip}/room/{room_id}", timeout=5)
                    response.raise_for_status()
                    data = response.json()
                    logger.info(f"Successfully fetched data for active room {room_id} from {ip}")
                    
                    data_text += (f"Room {room_id}:\n"
                                  f"  Temperature: {data['temperature']}°C\n"
                                  f"  Humidity: {data['humidity']}%\n"
                                  f"  Gas Level: {data['gas_level']} ppm\n\n")
                except Exception as e:
                    logger.error(f"Failed to fetch data for room {room_id} from {ip}: {str(e)}")
                    data_text += f"Room {room_id}: Failed to fetch data ({str(e)})\n\n"
            
            self.text_edit.setText(data_text)
            
        except Exception as e:
            logger.error(f"Failed to get active room list from {ip}: {str(e)}")
            self.text_edit.setText(f"Error fetching active room data: {str(e)}")

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Environmental Monitoring")
        self.setGeometry(100, 100, 800, 600)
        self.central_widget = QWidget()
        self.setCentralWidget(self.central_widget)
        self.layout = QVBoxLayout(self.central_widget)

        # Device table
        self.device_table = QTableWidget(0, 4)
        self.device_table.setHorizontalHeaderLabels(["Name", "MAC", "IP", "Active Rooms"])
        self.device_table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)  # Make table non-editable
        self.device_table.cellClicked.connect(self.on_device_select)
        self.device_table.doubleClicked.connect(self.on_row_double_clicked)  # Handle double-click
        self.layout.addWidget(self.device_table)

        # Auto-scan toggle button (placed below the table)
        self.auto_scan_btn = QPushButton("Start Auto-Scan")
        self.auto_scan_btn.clicked.connect(self.toggle_auto_scan)
        self.layout.addWidget(self.auto_scan_btn)

        # View active rooms button
        self.view_rooms_btn = QPushButton("View Active Rooms")
        self.view_rooms_btn.clicked.connect(self.view_active_rooms)
        self.view_rooms_btn.setEnabled(False)  # Disabled until a device is selected
        self.layout.addWidget(self.view_rooms_btn)

        self.selected_ip = None
        self.is_auto_scanning = False
        self.scan_timer = QTimer(self)
        self.scan_timer.timeout.connect(self.scan_network)
        self.thread = None  # Initialize thread as None

    def scan_network(self):
        if self.thread is not None and self.thread.isRunning():
            logger.info("Scan already in progress, skipping")
            return
        
        logger.info("Initiating network scan")
        self.thread = NetworkThread()
        self.thread.devicesFound.connect(self.display_devices)
        self.thread.finished.connect(self.on_thread_finished)
        self.thread.start()

    def on_thread_finished(self):
        self.thread = None
        logger.info("Network scan thread finished")

    def toggle_auto_scan(self):
        if self.is_auto_scanning:
            self.scan_timer.stop()
            self.is_auto_scanning = False
            self.auto_scan_btn.setText("Start Auto-Scan")
            logger.info("Auto-scan stopped")
        else:
            self.scan_timer.start(5000)  # Scan every 5 seconds
            self.is_auto_scanning = True
            self.auto_scan_btn.setText("Stop Auto-Scan")
            logger.info("Auto-scan started")
            self.scan_network()

    def display_devices(self, devices):
        if not devices and not self.is_auto_scanning:
            logger.warning("No devices found during scan")
            QMessageBox.warning(self, "Error", "No devices found")

        current_devices = {}
        for row in range(self.device_table.rowCount()):
            ip = self.device_table.item(row, 2).text()
            current_devices[ip] = {
                "name": self.device_table.item(row, 0).text(),
                "mac": self.device_table.item(row, 1).text(),
                "rooms": int(self.device_table.item(row, 3).text())
            }

        self.device_table.setRowCount(len(devices))
        for i, device in enumerate(devices):
            if device["ip"] in current_devices:
                if (current_devices[device["ip"]]["name"] != device["name"] or
                    current_devices[device["ip"]]["mac"] != device["mac"] or
                    current_devices[device["ip"]]["rooms"] != device["rooms"]):
                    self.device_table.setItem(i, 0, QTableWidgetItem(device["name"]))
                    self.device_table.setItem(i, 1, QTableWidgetItem(device["mac"]))
                    self.device_table.setItem(i, 2, QTableWidgetItem(device["ip"]))
                    self.device_table.setItem(i, 3, QTableWidgetItem(str(device["rooms"])))
            else:
                self.device_table.setItem(i, 0, QTableWidgetItem(device["name"]))
                self.device_table.setItem(i, 1, QTableWidgetItem(device["mac"]))
                self.device_table.setItem(i, 2, QTableWidgetItem(device["ip"]))
                self.device_table.setItem(i, 3, QTableWidgetItem(str(device["rooms"])))

        logger.info(f"Updated device table with {len(devices)} devices")

    def on_device_select(self, row, column):
        self.selected_ip = self.device_table.item(row, 2).text()
        self.view_rooms_btn.setEnabled(True)  # Enable view button when a device is selected
        logger.info(f"Selected device with IP: {self.selected_ip}")

    def on_row_double_clicked(self, index):
        """Handle double-click on a table row to open room data dialog"""
        row = index.row()
        ip = self.device_table.item(row, 2).text()
        logger.info(f"Double-clicked on device {ip}, opening active room data dialog")
        self.open_room_data_dialog(ip)

    def view_active_rooms(self):
        """Handle click on View Active Rooms button"""
        if self.selected_ip:
            logger.info(f"Opening active room data dialog for {self.selected_ip}")
            self.open_room_data_dialog(self.selected_ip)
        else:
            QMessageBox.warning(self, "Warning", "Please select a device first")

    def open_room_data_dialog(self, ip):
        """Open a dialog showing only active room data"""
        dialog = RoomDataDialog(ip, self)
        dialog.exec()

if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    sys.exit(app.exec())