import sys
import socket
import logging
from PyQt6.QtWidgets import QApplication, QMainWindow, QPushButton, QTableWidget, QTableWidgetItem, QVBoxLayout, QWidget, QMessageBox, QDialog, QComboBox, QProgressBar, QLabel
from PyQt6.QtCore import QThread, pyqtSignal, QTimer, Qt
import requests

logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s', handlers=[logging.StreamHandler(sys.stdout)])
logger = logging.getLogger(__name__)

class NetworkThread(QThread):
    devicesFound = pyqtSignal(list)

    def run(self):
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
        self.devicesFound.emit(devices)

class RoomDataDialog(QDialog):
    def __init__(self, ip, parent=None):
        super().__init__(parent)
        self.ip = ip
        self.setWindowTitle(f"Active Room Data for {ip}")
        self.setFixedSize(350, 300)  # Fixed size for content
        self.layout = QVBoxLayout(self)
        self.room_combo = QComboBox()
        self.room_combo.currentIndexChanged.connect(self.update_room_data)
        self.layout.addWidget(QLabel("Select Active Room:"))
        self.layout.addWidget(self.room_combo)
        self.temp_gauge = QProgressBar()
        self.temp_gauge.setRange(0, 50)
        self.temp_gauge.setFormat("Temperature: %v°C")
        self.layout.addWidget(self.temp_gauge)
        self.hum_gauge = QProgressBar()
        self.hum_gauge.setRange(0, 100)
        self.hum_gauge.setFormat("Humidity: %v%")
        self.layout.addWidget(self.hum_gauge)
        self.mq9_gauge = QProgressBar()
        self.mq9_gauge.setRange(0, 4095)
        self.mq9_gauge.setFormat("MQ9 Level: %v")
        self.layout.addWidget(self.mq9_gauge)
        self.mq2_gauge = QProgressBar()
        self.mq2_gauge.setRange(0, 4095)
        self.mq2_gauge.setFormat("MQ2 Level: %v")
        self.layout.addWidget(self.mq2_gauge)
        self.mq135_gauge = QProgressBar()
        self.mq135_gauge.setRange(0, 4095)
        self.mq135_gauge.setFormat("MQ135 Level: %v")
        self.layout.addWidget(self.mq135_gauge)
        self.error_label = QLabel("")
        self.layout.addWidget(self.error_label)
        self.data_timer = QTimer(self)
        self.data_timer.timeout.connect(self.update_room_data)
        self.fetch_active_rooms()
        self.data_timer.start(1000)

    def fetch_active_rooms(self):
        try:
            response = requests.get(f"http://{self.ip}/active-rooms", timeout=5)
            response.raise_for_status()
            data = response.json()
            self.room_combo.clear()
            rooms = data.get("rooms", [])
            if not rooms:
                self.error_label.setText("No active rooms found.")
                self.room_combo.setEnabled(False)
                return
            for room in rooms:
                self.room_combo.addItem(f"Room {room['room_id']} ({room['name']})", room['room_id'])
            self.room_combo.setEnabled(True)
            self.update_room_data()
        except requests.exceptions.RequestException as e:
            self.error_label.setText(f"Error fetching rooms: {str(e)}")
            self.room_combo.setEnabled(False)

    def update_room_data(self):
        if not self.room_combo.isEnabled() or self.room_combo.currentData() is None:
            return
        room_id = self.room_combo.currentData()
        try:
            response = requests.get(f"http://{self.ip}/room/{room_id}", timeout=5)
            response.raise_for_status()
            data = response.json()
            self.temp_gauge.setValue(int(data['temperature']))
            self.hum_gauge.setValue(int(data['humidity']))
            self.mq9_gauge.setValue(data['mq9_level'])
            self.mq2_gauge.setValue(data['mq2_level'])
            self.mq135_gauge.setValue(data['mq135_level'])
            self.error_label.setText("")
        except requests.exceptions.RequestException as e:
            self.error_label.setText(f"Error fetching data: {str(e)}")

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Environmental Monitoring")
        self.setFixedSize(600, 400)  # Fixed size for content
        self.central_widget = QWidget()
        self.setCentralWidget(self.central_widget)
        self.layout = QVBoxLayout(self.central_widget)
        self.device_table = QTableWidget(0, 4)
        self.device_table.setHorizontalHeaderLabels(["Name", "MAC", "IP", "Active Rooms"])
        self.device_table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self.device_table.cellClicked.connect(self.on_device_select)
        self.device_table.doubleClicked.connect(self.on_row_double_clicked)
        self.layout.addWidget(self.device_table)
        self.auto_scan_btn = QPushButton("Start Auto-Scan")
        self.auto_scan_btn.clicked.connect(self.toggle_auto_scan)
        self.layout.addWidget(self.auto_scan_btn)
        self.view_rooms_btn = QPushButton("View Active Rooms")
        self.view_rooms_btn.clicked.connect(self.view_active_rooms)
        self.view_rooms_btn.setEnabled(False)
        self.layout.addWidget(self.view_rooms_btn)
        self.selected_ip = None
        self.is_auto_scanning = False
        self.scan_timer = QTimer(self)
        self.scan_timer.timeout.connect(self.scan_network)
        self.thread = None

    def scan_network(self):
        if self.thread is not None and self.thread.isRunning():
            return
        self.thread = NetworkThread()
        self.thread.devicesFound.connect(self.display_devices)
        self.thread.finished.connect(self.on_thread_finished)
        self.thread.start()

    def on_thread_finished(self):
        self.thread = None

    def toggle_auto_scan(self):
        if self.is_auto_scanning:
            self.scan_timer.stop()
            self.is_auto_scanning = False
            self.auto_scan_btn.setText("Start Auto-Scan")
        else:
            self.scan_timer.start(5000)
            self.is_auto_scanning = True
            self.auto_scan_btn.setText("Stop Auto-Scan")
            self.scan_network()

    def display_devices(self, devices):
        self.device_table.setRowCount(len(devices))
        for i, device in enumerate(devices):
            self.device_table.setItem(i, 0, QTableWidgetItem(device["name"]))
            self.device_table.setItem(i, 1, QTableWidgetItem(device["mac"]))
            self.device_table.setItem(i, 2, QTableWidgetItem(device["ip"]))
            self.device_table.setItem(i, 3, QTableWidgetItem(str(device["rooms"])))

    def on_device_select(self, row, column):
        self.selected_ip = self.device_table.item(row, 2).text()
        self.view_rooms_btn.setEnabled(True)

    def on_row_double_clicked(self, index):
        row = index.row()
        ip = self.device_table.item(row, 2).text()
        self.open_room_data_dialog(ip)

    def view_active_rooms(self):
        if self.selected_ip:
            self.open_room_data_dialog(self.selected_ip)

    def open_room_data_dialog(self, ip):
        dialog = RoomDataDialog(ip, self)
        dialog.exec()

if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    sys.exit(app.exec())