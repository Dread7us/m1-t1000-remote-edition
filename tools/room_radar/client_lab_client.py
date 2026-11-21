"""Read-only beta status and acknowledged button control for Wi-Fi Clients."""
import json
import time
from device_check import RoomScope


class ClientLab(RoomScope):
    def status(self):
        return json.loads(self.request(0x69, expected=0x6A, timeout=5))

    def wait(self, predicate, description, timeout=15):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            state = self.status()
            if predicate(state):
                return state
            time.sleep(0.1)
        raise TimeoutError(description + ": " + json.dumps(state))

    def click(self, key):
        self.request(0x22, bytes([key]), 6)

    def close_app(self):
        for _ in range(5):
            before = self.status()
            if not before["active"]:
                return before
            self.click(5)
            self.wait(lambda s: not s["active"] or s["view"] != before["view"],
                      "Return from client screen", 40)
        raise RuntimeError("Client Lab did not close")
