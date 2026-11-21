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

Failed captures never become snapshots or clear a confirmed change. They retry after the configured pause; two consecutive failures can trigger a notification. A different connected router stops monitoring instead of comparing different environments. Replacing the baseline also stops monitoring. No saved credentials or ESP32 firmware changes are needed.

Completed snapshots append to `System/room-watch.csv`. Confirmed changes append to `System/room-watch-events.csv`: uptime milliseconds, direction/category bitmask, network-count delta, responding-peer delta, signal delta. Bit values: 1 more networks, 2 fewer networks, 4 more responding peers, 8 fewer responding peers, 16 stronger signal, 32 weaker signal, 64 different channel. A full SD card is reported; notifications can still occur if event logging fails.

These are network observations. Sleeping peers, radio conditions and background traffic can change the counts; an event is not an intrusion or occupancy verdict.
