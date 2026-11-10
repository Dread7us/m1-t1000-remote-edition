# Room Radar

Open **WiFi 2.4G → Room Radar**. All observations stay on the M1. The map writes to `System/room-radar.tsv` on the SD card. Motion and LAN results stay in memory.

| View | What it measures | Requirements |
| --- | --- | --- |
| Wi-Fi map | Named snapshots of the strongest 2.4 GHz access point, signal in dBm, channel and total APs seen. Up/Down browses the latest 24 readings. The TSV keeps the full history. | Mounted SD to save |
| Hotspot clients | DHCP clients joined to the M1's own hotspot. Static-IP devices can be absent. | M1 in AP or AP+STA mode |
| LAN devices | Distinct IPv4 addresses that answer a ping on the M1's directly connected private subnet. Some devices block replies. | Room Radar ESP32 companion, joined Wi-Fi, private /24 or smaller subnet |
| Room motion | Change in the shape of Wi-Fi channel measurements, compared with an empty-room baseline. | Room Radar ESP32 companion, joined Wi-Fi and a stationary M1 |

For a room test, set the M1 down, open **Room motion**, and press OK with the room still. Once the baseline is recorded, press OK to measure again. Test an empty room and a person crossing the room with their phone's Wi-Fi off. Repeat while the person sits still. Left discards the baseline so you can recalibrate after moving the M1 or changing Wi-Fi networks. **Quiet window** means the signal was below the motion threshold during that measurement. Motion may also come from fans, pets, or movement outside the intended area. There is no head-count estimate.

The LAN view discovers the station subnet automatically. It uses a single bound ICMP socket, tests at most 253 other addresses, and discards unrelated replies. There is no router login or model-specific dependency. Start it only on a network you manage. The motion view filters CSI frames from the associated access point, pings the IPv4 gateway for a bounded measurement, and returns a score and sample count. Back requests cancellation. The coprocessor also ends the job on its own timeout.

## ESP32 status messages

Revision 15 accepts a complete status line even when the separate acknowledgement is absent. It distinguishes a transport timeout, a rejected command, and an incomplete reply. A timeout or incomplete reply does not identify the installed ESP32 image. Try the ESP32-C6 reset in System and check Wi-Fi Scan before replacing the companion.

## Companion firmware

The existing companion firmware can run the map and hotspot view. LAN and motion need the matching ESP32-C6 SPI build. The source overlay in `tools/room_radar` targets [M1 T-800 V0.4.0](https://github.com/dagnazty/M1-T-800/tree/d07b2f5e6ee1c43ba0f191774667b3ce7f91ddb2) and adds `AT+M1RADAR` without modifying the upstream repository. The [companion workflow](../../.github/workflows/room-radar-companion.yml) builds the `.bin` and matching `.bin.md5` as an Actions artifact. Keep both files together for the M1 ESP32 updater. Install the STM32 firmware and verify the two basic views first. The companion is a separate update.

The host tests cover scoring, result parsing, reply matching, and subnet bounds. The CSI classifier and radio behavior need on-device verification.
