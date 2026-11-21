"""Host-side safety checks for live monitor automation."""
import unittest
import test_device_check
from watch_client import Watch


class WatchClientTests(unittest.TestCase):
    def test_active_app_never_reads_or_writes_sd(self):
        device = object.__new__(Watch)
        device.status = lambda: {"active": 1}
        device.request = lambda *args, **kwargs: self.fail("Unsafe SD command was sent")
        with self.assertRaises(RuntimeError):
            device.read_small("System/room-watch.dat")
        with self.assertRaises(RuntimeError):
            device.write_small("System/room-watch.dat", b"test")

    def test_read_rejects_partial_multiframe_files(self):
        device = object.__new__(Watch)
        device.status = lambda: {"active": 0}
        device.request = lambda *args, **kwargs: b"\0" * 1028
        with self.assertRaises(RuntimeError):
            device.read_small("test")

    def test_write_requires_exact_readback(self):
        device = object.__new__(Watch)
        device.status = lambda: {"active": 0}
        commands = []
        device.request = lambda *args, **kwargs: commands.append(args[0]) or b""
        device.read_small = lambda path: b"wrong"
        with self.assertRaises(RuntimeError):
            device.write_small("test", b"right")
        self.assertEqual(commands, [0x34, 0x35, 0x36])

    def test_live_status_has_no_sd_dependency(self):
        device = object.__new__(Watch)
        commands = []
        device.request = lambda *args, **kwargs: commands.append(args[0]) or b'{"active":1,"checks":2}'
        self.assertEqual(device.status()["checks"], 2)
        self.assertEqual(commands, [0x67])


if __name__ == "__main__":
    unittest.main()
