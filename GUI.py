import socket
import tkinter as tk
from tkinter import ttk, messagebox
from threading import Thread
import requests

class EnvironmentMonitor:
    def __init__(self, master):
        self.master = master
        master.title("Environment Monitor")
        
        self.setup_ui()
        self.devices = []

    def setup_ui(self):
        self.btn_scan = ttk.Button(self.master, text="Scan Network", command=self.start_scan)
        self.btn_scan.pack(pady=10)

        self.tree = ttk.Treeview(self.master, columns=('Name', 'MAC', 'IP', 'Rooms'), show='headings')
        self.tree.heading('Name', text='Device Name')
        self.tree.heading('MAC', text='MAC Address')
        self.tree.heading('IP', text='IP Address')
        self.tree.heading('Rooms', text='Rooms')
        self.tree.pack(padx=10, pady=10, fill='both', expand=True)
        
        self.tree.bind('<<TreeviewSelect>>', self.show_room_data)

    def start_scan(self):
        Thread(target=self.discover_devices, daemon=True).start()

    def discover_devices(self):
        self.btn_scan.config(state=tk.DISABLED)
        self.tree.delete(*self.tree.get_children())
        
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        sock.settimeout(2)
        
        try:
            sock.sendto(b"DISCOVER_DEVICES", ("255.255.255.255", 1234))
            while True:
                try:
                    data, addr = sock.recvfrom(1024)
                    device_info = data.decode().split(',')
                    if len(device_info) == 4:
                        self.tree.insert('', 'end', values=(
                            device_info[0], 
                            device_info[1], 
                            device_info[2], 
                            device_info[3]
                        ))
                except socket.timeout:
                    break
        except Exception as e:
            messagebox.showerror("Error", f"Scan failed: {str(e)}")
        finally:
            self.btn_scan.config(state=tk.NORMAL)
            sock.close()

    def show_room_data(self, event):
        selected = self.tree.selection()
        if not selected:
            return
            
        device_ip = self.tree.item(selected[0])['values'][2]
        rooms = int(self.tree.item(selected[0])['values'][3])
        
        room_window = tk.Toplevel(self.master)
        room_window.title(f"Room Data - {device_ip}")
        
        for room_num in range(1, rooms+1):
            frame = ttk.Frame(room_window)
            frame.pack(fill='x', padx=5, pady=2)
            
            ttk.Label(frame, text=f"Room {room_num}:").pack(side=tk.LEFT)
            ttk.Button(frame, text="View", 
                      command=lambda ip=device_ip, rn=room_num: 
                          self.display_room_details(ip, rn)).pack(side=tk.RIGHT)

    def display_room_details(self, ip, room_num):
        try:
            response = requests.get(f"http://{ip}/room{room_num}", timeout=2)
            data = response.json()
            
            detail_window = tk.Toplevel(self.master)
            detail_window.title(f"Room {room_num} Details")
            
            ttk.Label(detail_window, text=f"Temperature: {data['temperature']}°C").pack()
            ttk.Label(detail_window, text=f"Humidity: {data['humidity']}%").pack()
            ttk.Label(detail_window, text=f"Gas Level: {data['gas']}").pack()
            
        except Exception as e:
            messagebox.showerror("Error", f"Failed to get data: {str(e)}")

if __name__ == "__main__":
    root = tk.Tk()
    app = EnvironmentMonitor(root)
    root.geometry("800x600")
    root.mainloop()