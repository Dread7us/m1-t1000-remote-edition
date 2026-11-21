# Room Motion: paired validation

The USB test runner records measurements without button presses or SD-card removal. It does not infer whether someone is walking. A participant must follow the cues for the labels to be useful.

## Minimum participant work

Leave the M1 fixed on a table, connected to USB, already joined to Wi-Fi, on the RoomScope home screen. Keep its position, router, doors and other moving objects unchanged during the comparison. Keep other people and pets still if possible.

Allow about two minutes. Follow four PC cues:

1. One low beep: stay still.
2. Two high beeps: walk normally back and forth across the room.
3. One low beep: stay still again.
4. Two high beeps: repeat the same walking route until the PC says Done.

Each phase has an eight-second settling period and three captures. There are no buttons to press during the test. The operator starts the runner only after the participant confirms readiness. If the room cannot be controlled, record that limitation and repeat later.

```text
python tools/room_radar/motion_trial.py --output trial.json --start
```

## What is checked

Every capture is saved immediately with its instructed phase, UTC timestamp, sample count and score. Interrupted or sparse trials remain Inconclusive. The first quiet/walking pair proposes a threshold only if the score ranges do not overlap. The second pair checks that fixed threshold for false alarms and missed walking windows. It is not refitted to the second pair and is not deployed automatically.

A successful small trial establishes separation only for that placement and session. Repeat on another occasion before considering a firmware threshold. Quiet RF variation does not establish an empty room, and device counts do not establish a people count.

For unattended collection, use `device_check.py --ambient-windows 6`. Its windows are unlabeled and cannot be scored as walking or quiet-room accuracy. It checks Wi-Fi before and after collection.

## Current evidence

Earlier quiet scores and reported walking scores overlap. Do not lower the detection threshold merely to force a walking verdict. Investigate placement or a richer CSI metric if independently labeled trials continue to overlap.
