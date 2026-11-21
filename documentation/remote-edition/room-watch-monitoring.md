# Automatic Room Watch

Open Room Watch and use Left/Right to reach the monitor view (3/4). Center OK starts or stops. Save a baseline from Actions first if none exists. The first check starts immediately; later checks wait for the configured pause after the previous check completes. A full LAN check can take about 35 seconds, so a one-minute pause is not a one-minute start-to-start interval.

While waiting, the countdown remains live and the other views are available. Back stops a running monitor; press Back again to exit. Keep Room Watch open: this is an on-device monitoring session, not a background service across other tools or reboot. Normal backlight dimming still works; enabled alerts wake the screen using the saved brightness.

## Settings

Actions (4/4) > Monitor settings. Up/Down selects a row; center OK changes its value and saves immediately. Failed saves keep the previous values. Saved preferences have a checksum and recovery copy on the SD card.

- Scan pause: 1, 2, 5 or 10 minutes after each completed or failed check.
- Alerts: Off, Screen, or Screen + Beep. Default: Screen.
- Sensitivity: High, Normal, Low. Default: Normal.
- Test alert: exercises the configured notification without changing the baseline. Off stays silent. Test alerts are labeled and are not written as environmental events.

Sensitivity thresholds are 2/4/8 nearby networks or responding peers, and 8/12/18 dB signal change, respectively. A connected-router channel change is also tracked. Two consecutive checks must exceed a threshold in the same direction before an event is confirmed. That category does not alert again until two checks fall below the threshold. Dismiss alert clears the screen message without rearming the detector. Changing preferences resets confirmation history.

## Failures and saved data

Failed captures never become snapshots or clear a confirmed change. They retry after the configured pause; two consecutive failures can trigger a notification. A different connected access point (BSSID) stops monitoring instead of comparing different environments. This includes roaming between nodes of a home mesh with the same network name. Run a fresh room check and explicitly save its baseline to monitor that access point; the previous baseline remains in the recovery copy. Replacing the baseline also stops monitoring. No saved credentials or ESP32 firmware changes are needed.

Completed snapshots append to `System/room-watch.csv`. Confirmed changes append to `System/room-watch-events.csv`: uptime milliseconds, direction/category bitmask, network-count delta, responding-peer delta, signal delta. Bit values: 1 more networks, 2 fewer networks, 4 more responding peers, 8 fewer responding peers, 16 stronger signal, 32 weaker signal, 64 different channel. A full SD card is reported; notifications can still occur if event logging fails.

These are network observations. Sleeping peers, radio conditions and background traffic can change the counts; an event is not an intrusion or occupancy verdict.

## r29 hardware validation — 21 November 2026

The M1 passed baseline save, byte-for-byte readback, backup preservation and reload. Three real scheduled scans completed with a one-minute pause: completion spacing was 84.8 and 84.3 seconds, including each scan's runtime. A temporary synthetic baseline produced alert counts **0 → 1 → 1**, verifying two-check confirmation and repeat suppression. Back stopped monitoring. The real baseline was restored and verified; synthetic alert history and the temporary recovery file were removed. Wi-Fi scans and a 51-sample CSI capture passed afterward. This validates the alert mechanism, not intrusion or occupancy accuracy. Audible beep output has not been independently heard; screen notifications and the buzzer call path are covered.

### Safe USB automation

FatFs in this build is not configured for concurrent access. Remote file operations now return Busy while a tool is active. Use the live Room Watch status channel for progress; exit the app before reading or changing SD files. The status reports include confirmation phase, completed scans, monitor state and baseline-save attempts/results. Automation waits for these acknowledgements instead of sending timed bursts of buttons.

The opt-in hardware runner is:

```text
python tools/room_radar/verify_watch_hardware.py --output local-test-results --temporary-baseline
```

Start with Room Watch closed and highlighted on the home menu, connected to Wi-Fi, with a valid baseline and one-minute Screen alert settings. The runner backs up the baseline, installs an explicit test fixture, collects three live scans, verifies confirmation and suppression, and restores the original baseline and alert history in cleanup. It exits unsuccessfully if the test or restoration fails. Test reports may contain local network details; keep them private.

See [saved-device beta status](device-beta-status.md) for the distinction between access-point targeting and individual-client modes.
