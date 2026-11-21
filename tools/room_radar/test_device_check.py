"""USB protocol tests without serial hardware or pyserial."""
import binascii
import importlib.util
from pathlib import Path
import struct
import sys
import types
import unittest

serial = types.ModuleType("serial")
serial.SerialException = OSError
serial_tools = types.ModuleType("serial.tools")
ports = types.ModuleType("serial.tools.list_ports")
ports.comports = lambda: []
serial_tools.list_ports = ports
sys.modules.update({"serial": serial, "serial.tools": serial_tools, "serial.tools.list_ports": ports})
spec = importlib.util.spec_from_file_location("roomscope_check", Path(__file__).with_name("device_check.py"))
client = importlib.util.module_from_spec(spec)
spec.loader.exec_module(client)


def frame(kind, sequence, payload):
    body = struct.pack("<BBH", kind, sequence, len(payload)) + payload
    return b"\xaa" + body + struct.pack("<H", binascii.crc_hqx(body, 65535))


class FakePort:
    def __init__(self, *args, **kwargs):
        self.in_waiting = 1
        self.pending = b""
        self.writes = []

    def write(self, data):
        self.writes.append(data)
        sequence = data[2]
        broken = bytearray(frame(2, sequence, b"corrupt"))
        broken[-1] ^= 1
        self.pending = b"noise" + bytes(broken) + frame(2, sequence % 255 + 1, b"stale") + frame(2, sequence, b"ready")

    def read(self, count):
        part, self.pending = self.pending[:1], self.pending[1:]
        return part

    def close(self):
        pass


class USBTests(unittest.TestCase):
    def setUp(self):
        serial.Serial = FakePort

    def test_fragmentation_crc_and_sequence(self):
        device = client.RoomScope("fake")
        self.assertEqual(device.request(1, expected=2), b"ready")
        body = device.port.writes[0][1:-2]
        self.assertEqual(binascii.crc_hqx(body, 65535), struct.unpack("<H", device.port.writes[0][-2:])[0])

    def test_direct_radio_reply_preserves_records(self):
        device = object.__new__(client.RoomScope)
        calls = []
        def request(*args):
            calls.append(args)
            return b"\0AT+M1RADAR?\r\n+M1RADAR:1,2,48,53\r\nOK\r\n"
        device.request = request
        self.assertEqual(device.radar(), {"state": 2, "samples": 48, "score": 53})
        self.assertEqual(calls, [(0x65, b"\x03", 0x66, 35)])

    def test_direct_radio_rejects_error_and_unknown_command(self):
        device = object.__new__(client.RoomScope)
        device.request = lambda *args: b"\x01TIMEOUT"
        with self.assertRaises(RuntimeError):
            device.at("AT")
        with self.assertRaises(ValueError):
            device.at("AT+RESTORE")

    def test_timeout_is_explicit(self):
        device = client.RoomScope("fake")
        with self.assertRaises(TimeoutError):
            device.request(1, expected=2, timeout=0)

    def test_failed_job_requests_cancellation(self):
        device = object.__new__(client.RoomScope)
        responses = iter([{"state": 0}, {"state": 3, "samples": 0, "score": 0}])
        commands = []
        device.radar = lambda: next(responses)
        device.at = lambda command: commands.append(command) or "OK\r\n"
        old_sleep = client.time.sleep
        client.time.sleep = lambda _: None
        try:
            with self.assertRaises(RuntimeError):
                device.job(1)
        finally:
            client.time.sleep = old_sleep
        self.assertEqual(commands, ["AT+M1RADAR=1", "AT+M1RADAR=0"])

    def test_busy_job_is_not_restarted(self):
        device = object.__new__(client.RoomScope)
        device.radar = lambda: {"state": 1}
        device.at = lambda _: self.fail("Must not restart an active job")
        with self.assertRaises(RuntimeError):
            device.job(1)


if __name__ == "__main__":
    unittest.main()
