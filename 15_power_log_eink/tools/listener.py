#!/usr/bin/env python3
import socket
import struct
import binascii

UDP_IP = "0.0.0.0"
UDP_PORT = 56374

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
sock.bind((UDP_IP, UDP_PORT))

print(f"Listening for UDP broadcasts on port {UDP_PORT}...")
last_packet = None

while True:
    data, addr = sock.recvfrom(1024)
    if len(data) == 20:
        if data == last_packet:
            continue
        tm_sec, tm_min, tm_hour, tm_wday, consume_Wh, produce_Wh, current_W, crc = (
            struct.unpack("<4B3fI", data)
        )
        expected_crc = binascii.crc32(data[:16]) & 0xFFFFFFFF
        crc_ok = (
            "OK"
            if crc == expected_crc
            else f"FAIL(got={crc:#010x} exp={expected_crc:#010x})"
        )
        print(
            f"{addr}: {tm_wday}:{tm_hour}:{tm_min}:{tm_sec} used={consume_Wh}Wh produced={produce_Wh}Wh actual={current_W}W crc={crc_ok}"
        )
        last_packet = data
    elif len(data) == 16:
        if data == last_packet:
            continue
        tm_sec, tm_min, tm_hour, tm_wday, consume_Wh, produce_Wh, current_W = (
            struct.unpack("<4B3f", data)
        )
        print(
            f"{addr}: {tm_wday}:{tm_hour}:{tm_min}:{tm_sec} used={consume_Wh}Wh produced={produce_Wh}Wh actual={current_W}W crc=NO_CRC(old_packet)"
        )
        last_packet = data
    else:
        print(f"Received packet from {addr}: {data}")
