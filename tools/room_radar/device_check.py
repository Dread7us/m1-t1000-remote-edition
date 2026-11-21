#!/usr/bin/env python3
"""Check RoomScope over USB. Requires pyserial; reports are JSON on stdout."""
import argparse
import binascii
import json
import re
import struct
import time

import serial
from serial.tools import list_ports


class RoomScope:
    def __init__(self, port):
        self.port = serial.Serial(port, 115200, timeout=0.1, write_timeout=3)
        self.sequence = 0
        self.buffer = b""

    def request(self, command, payload=b"", expected=None, timeout=5):
        self.sequence = self.sequence % 255 + 1
        body = struct.pack("<BBH", command, self.sequence, len(payload)) + payload
        self.port.write(b"\xaa" + body + struct.pack("<H", binascii.crc_hqx(body, 65535)))
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.buffer += self.port.read(self.port.in_waiting or 1)
            while self.buffer:
                start = self.buffer.find(b"\xaa")
                if start < 0:
                    self.buffer = b""
                    break
                self.buffer = self.buffer[start:]
                if len(self.buffer) < 5:
                    break
                kind, sequence, length = struct.unpack_from("<BBH", self.buffer, 1)
                if length > 8192:
                    self.buffer = self.buffer[1:]
                    continue
                size = length + 7
                if len(self.buffer) < size:
                    break
                frame, self.buffer = self.buffer[:size], self.buffer[size:]
                if binascii.crc_hqx(frame[1:-2], 65535) != struct.unpack_from("<H", frame, size - 2)[0]:
                    continue
                if sequence != self.sequence:
                    continue
                if kind == 7:
                    raise RuntimeError("M1 rejected command " + hex(command))
                if expected is None or kind == expected:
                    return frame[5:-2]
        raise TimeoutError("No RoomScope USB response to " + hex(command))

    def cli(self, command):
        return self.request(0x60, command.encode() + b"\0", 0x61, 35).decode("utf-8", "replace")

    def at(self, command):
        commands = ("AT", "AT+CWLAP", "AT+CWJAP?", "AT+M1RADAR?",
                    "AT+M1RADAR=1", "AT+M1RADAR=2", "AT+M1RADAR=0")
        response = self.request(0x65, bytes([commands.index(command)]), 0x66, 35)
        if not response or response[0] != 0:
            raise RuntimeError("Radio command failed: " + repr(response))
        return response[1:].decode("utf-8", "replace")

    def radar(self):
        output = self.at("AT+M1RADAR?")
        match = re.search(r"\+M1RADAR:1,([0-3]),([0-9]+),([0-9]+)", output)
        if not match:
            raise RuntimeError("Invalid Room Radar status: " + output[-180:])
        return dict(zip(("state", "samples", "score"), map(int, match.groups())))

    def job(self, operation):
        current = self.radar()
        if current["state"] == 1:
            raise RuntimeError("Room Radar is already measuring; exit its measurement first")
        if not re.search(r"(?:^|[\r\n])OK(?:[\r\n]|$)", self.at("AT+M1RADAR=" + str(operation))):
            raise RuntimeError("Radio did not accept the measurement")
        done = False
        try:
            deadline = time.monotonic() + (10 if operation == 1 else 40)
            while time.monotonic() < deadline:
                time.sleep(0.5)
                result = self.radar()
                if result["state"] == 2:
                    done = True
                    return result
                if result["state"] != 1:
                    raise RuntimeError("Radio job failed: " + json.dumps(result))
            raise TimeoutError("Radio measurement timed out")
        finally:
            if not done:
                self.at("AT+M1RADAR=0")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port")
    parser.add_argument("--ambient-windows", type=int, default=0,
                        help="Collect 0..12 unlabeled signal windows; these cannot verify walking or occupancy.")
    parser.add_argument("--lan", action="store_true", help="Run LAN discovery on the already joined private network.")
    args = parser.parse_args()
    if not 0 <= args.ambient_windows <= 12:
        parser.error("--ambient-windows must be between 0 and 12")
    ports = [p.device for p in list_ports.comports()
             if (p.vid, p.pid, p.serial_number) == (0x0483, 0x5750, "3177384F3434")]
    if not args.port and len(ports) != 1:
        parser.error("Connect one M1 or specify --port")
    device = RoomScope(args.port or ports[0])
    report = {"hardware_verified": False, "ambient_windows": []}
    code = 1
    try:
        device.request(1, expected=2)
        device.request(3, expected=4)
        report["usb_control"] = "passed"
        if not re.search(r"(?:^|[\r\n])OK(?:[\r\n]|$)", device.at("AT")):
            raise RuntimeError("Radio readiness check failed")
        report["wifi_scan"] = device.at("AT+CWLAP")
        if "+CWLAP:" not in report["wifi_scan"]:
            raise RuntimeError("No Wi-Fi network records were returned")
        report["radar_status"] = device.radar()
        if args.lan or args.ambient_windows:
            joined = device.at("AT+CWJAP?")
            if "+CWJAP:" not in joined:
                raise RuntimeError("Join Wi-Fi on the M1 once before running measurements")
            if args.lan:
                report["lan"] = device.job(2)
            for _ in range(args.ambient_windows):
                report["ambient_windows"].append(device.job(1))
            report["wifi_scan_after"] = device.at("AT+CWLAP")
            if "+CWLAP:" not in report["wifi_scan_after"]:
                raise RuntimeError("Wi-Fi scan after measurements returned no records")
        report["hardware_verified"] = True
        report["scope"] = "USB, Wi-Fi and requested radio jobs; human motion and occupancy remain unverified"
        code = 0
    except (RuntimeError, TimeoutError, serial.SerialException) as error:
        report["error"] = str(error)
    finally:
        device.port.close()
    print(json.dumps(report, indent=2))
    return code


if __name__ == "__main__":
    raise SystemExit(main())
