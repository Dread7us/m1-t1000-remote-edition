# Infrared service reliability · revision 12

The review found concrete defects; it does not establish that they explain every missed appliance response.

- Universal power playback started a hardware frame and slept 220 ms without advancing multi-frame protocols. It now pumps parsed/raw playback to full completion, stops each send before switching, supports Back during transmission, and aborts on a five-second timeout.
- Inter-frame delays previously allocated software timers whose callback could wait indefinitely for room in the UI queue. Deadline polling removes that callback and timer-command/allocation dependencies. Active transmit loops poll at one tick/millisecond under the current 1 kHz configuration, with unsigned elapsed-time arithmetic across counter rollover. Idle UI polling stays slower.
- Receiver entry resets TIM2, the HAL handle, pending interrupts and edge detection before capture starts. Exit disables capture/update interrupts and the timer before teardown, clears pending flags, and resets edge detection.
- The serial diagnostic transmitter previously busy-waited, treated a gap as completion, and attempted to free the encoder's static OTA buffer. It now yields while pumping the complete state machine, times out safely, and leaves buffer ownership with the encoder.

## Verification

The firmware builds and the complete native regression suite passes. Tests exercise the actual parsed transmitter for 1,000 commands, including multi-frame delays and tick rollover without timer notifications. The actual power-playback workflow is tested for parsed/raw completion, timeout, cancellation and per-action cleanup. Existing tests cover 1–512 timing entries, carrier initialization, Kaseikyo wire bytes, raw parsing and automation cleanup. Receiver ordering is checked in source; receiver hardware and radio/resource interactions are not emulated by these tests.

Physical verification is pending: compare repeated saved-remote sends with a known working remote, enter/exit Learn between sends, switch between parsed and raw remotes, try universal power playback and an automation, then test after display sleep. A missed appliance response still needs comparison with the same signal from a known working transmitter. Build/test success is not a guarantee that every receiver, protocol or timing source works.
