"""Room Watch automation via live status; SD files are read only after app exit."""
import json
import struct
import time
from device_check import RoomScope


class Watch(RoomScope):
    def status(self):
        return json.loads(self.request(0x67, expected=0x68, timeout=5))

    def wait(self, predicate, description, timeout=10):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            state = self.status()
            if predicate(state):
                return state
            time.sleep(0.1)
        raise TimeoutError(description + ": " + json.dumps(state))

    def click(self, key):
        self.request(0x22, bytes([key]), 6)

    def read_small(self, path):
        if self.status()["active"]:
            raise RuntimeError("Close Room Watch before reading SD files")
        data = self.request(0x32, path.encode() + b"\0", 0x33, 10)
        if len(data) >= 1028 or len(data) < 4 or struct.unpack_from("<I", data)[0] != 0:
            raise RuntimeError("Expected one complete small file")
        return data[4:]

    def write_small(self, path, data):
        if self.status()["active"] or len(data) > 512:
            raise RuntimeError("Small-file write requires a closed app and at most 512 bytes")
        self.request(0x34, struct.pack("<I", len(data)) + path.encode() + b"\0", 6, 10)
        self.request(0x35, struct.pack("<I", 0) + data, 6, 10)
        self.request(0x36, expected=6, timeout=10)
        if self.read_small(path) != data:
            raise RuntimeError("Saved file readback differs")

    def close_app(self):
        for _ in range(4):
            state = self.status()
            if not state["active"]:
                return state
            self.click(5)
            time.sleep(0.3)
        return self.wait(lambda s: not s["active"], "App did not exit", 10)
