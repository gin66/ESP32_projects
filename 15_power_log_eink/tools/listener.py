#!/usr/bin/env python3
import socket

# UDP settings
UDP_IP = "0.0.0.0"  # Listen on all interfaces
UDP_PORT = 56374     # Same port as ESP32

# Create UDP socket
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
sock.bind((UDP_IP, UDP_PORT))

print(f"Listening for UDP broadcasts on port {UDP_PORT}...")

while True:
    data, addr = sock.recvfrom(1024)  # Buffer size for receiving
    print(f"Received packet from {addr}: {data}")
