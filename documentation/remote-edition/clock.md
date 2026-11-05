# Local clock consistency · revision 10

**System → Date & Time** now edits local time, matching the home/runtime headers and the Local Clock page. Revision 11 places the editor directly under System. The RTC remains UTC internally; the configured offset is applied once for display and inverted once when saving local time.

Previously the editor read/wrote raw RTC time while the header added the offset. A correctly entered local hour could therefore produce a different header hour. The editor also always showed 24-hour time. Revision 10 uses the selected 12/24-hour preference, including explicit AM/PM in the editor and Clock app. Field underlines use measured font widths.

## After updating

1. Check **System → LCD & Notifications → UTC Offset**. Use the offset appropriate for your location/date. Pacific Standard Time is UTC−8; this integer-offset setting does not automatically apply daylight-saving rules.
2. Open **Set Time**, enter the current local date/time, and press center **OK** to save. In 12-hour mode, check the displayed AM/PM while adjusting the hour. Left/Right changes fields and Up/Down changes the value; Back prompts Save/Discard when values have changed; Back from that prompt keeps editing. Saving successfully displays **Time saved**.
3. Return home and compare the time. Use the current time when setting it, not an earlier example such as 1:01 PM.

Set the time once after this update to establish a known UTC value from your intended local time. Existing RTC contents are preserved during installation; the firmware cannot infer whether an older value came from manual local entry or UTC sync.

**WiFi → Sync RTC** remains an alternative after connecting to a network. SNTP supplies UTC. Sync now preserves the configured display offset instead of replacing it with an IP-based timezone guess, and reports failure if the RTC write fails.

## Checked access and persistence

RTC time/date reads are checked together, with the date read releasing the shadow-register latch even when the time read fails. Invalid/failed reads show `--:--` in headers rather than formatting uninitialized data. The Clock app and dashboard also report an unavailable clock. Manual save reports failures instead of silently claiming success. Weekday is derived from the selected date.

The existing backup-domain preservation across firmware updates/resets remains unchanged. Time retention depends on the backup domain staying powered. No automatic RTC migration or PC-clock synchronization is performed by installing this image.

## Validation

Tests exercise the actual RTC functions and Set Time workflow: local 13:01 roundtrips across offsets −12 through +14; leap/day/year rollover cases; weekday calculation; invalid values; failed reads/writes; exact home-header output for 13:01, noon, midnight, and RTC failure. Existing layout, text, IR, and automation checks remain included. Firmware builds locally. The owner confirmed on the physical M1 that the home clock agrees after saving in revision 10. Full power-loss RTC retention is not newly certified. Revision 11 tests additionally cover Save/Discard/keep-editing and success feedback.

Built on [T-1000 by dagnazty and contributors](https://github.com/dagnazty/M1_T-1000).
