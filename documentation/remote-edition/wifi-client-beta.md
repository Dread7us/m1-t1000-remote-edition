# Wi-Fi Clients beta

Open **Wi-Fi Clients** directly from the RoomScope home screen. Saved clients have a readable name, the client's currently associated Wi-Fi MAC, the router BSSID, a 2.4 GHz channel, and an optional private IPv4 address. The client and router are distinct entries; broadcast, multicast, all-zero and identical addresses are rejected.

## Add and manage a client

Choose **+ Add Wi-Fi client**, enter a name and the client MAC from your router's client list, then use the M1's current Wi-Fi router or enter your router BSSID/channel. Optionally add a private IP for response checks. Selecting a saved client opens its test modes directly. **Manage client** at the bottom lets you view, rename, edit or delete it. The list supports 16 profiles, checksums, a temporary-file replacement and a recovery copy. It is separate from the earlier AP/BLE/LAN device list, so those records are preserved.

The pairing is entered by you; an AP scan does not establish which client is associated with it. Check that pairing in your router before a live test. Each preflight finds the saved AP and refreshes its channel for the test. This beta supports 2.4 GHz channels 1–14.

## Modes

| Mode | Behavior |
| --- | --- |
| Check radio support | Checks that the saved router is visible and that the radio recognizes the stop/status command. Does not start a disconnect test. |
| Preview (no frames) | Shows the exact client, router and plan; records a preview without issuing a deauthentication start request. |
| Disconnect test (once) | Requests five client-addressed deauthentication attempts after target review and confirmation. |
| Disconnect test (x3) | Requests three five-attempt rounds with five-second gaps. Back stops subsequent rounds. |
| Check IP response | Pings only the saved private IP. An IP response does not establish Wi-Fi association or prove a disconnect occurred. |

The two disconnect modes use the selected station MAC, never the legacy AP-only/broadcast path. Builds with offensive tools disabled cannot start these tests. Each round is finite. A timed-out mutation is not automatically retried. The sender always attempts to stop the test and restore station mode, even if stop fails, then recovers/reconnects the M1 Wi-Fi or reports a recovery error. The M1's own Wi-Fi may temporarily disconnect during a live test; Room Watch does not run concurrently with this tool.

The companion's attempt counter is not proof of over-the-air delivery or client disconnection. Results say **Effect not verified** until independently observed. Support checks alone do not certify client-specific effect. Existing network-wide/offensive utilities remain separate; they are not relabeled as specific-client modes.

Completed attempts, errors, cancellations and previews are recorded locally in `System/wifi-client-tests.tsv`; client profiles are in `System/wifi-clients.dat` with a recovery copy. Keep those device addresses private when sharing reports.

## Validation

The native tests cover exact station/AP addressing, rejection of broadcast and invalid inputs, finite requests, no retry after an uncertain response, cancellation, independent stop/restore attempts and recoverable storage. On-device beta verification must distinguish saved-profile/preview/support checks from an actual disconnect test. A live test requires an explicitly identified device on the owner's network.
