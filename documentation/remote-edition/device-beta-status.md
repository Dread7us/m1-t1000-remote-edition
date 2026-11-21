# Saved-device beta status

This is a readiness inventory, not a claim that every radio mode has been verified on hardware.

| Mode | Current scope | Validation |
| --- | --- | --- |
| Wi-Fi Check Signal | Exact saved AP BSSID | Native adapter tests; dedicated device-mode hardware run pending |
| Wi-Fi Watch Availability | Three scans of the saved AP | Native adapter tests; dedicated hardware run pending |
| Wi-Fi Check Security | Advertised AP authentication type | Native adapter tests; not a cryptographic audit |
| LAN Check Connection | Saved private IPv4 endpoint | Implemented; dedicated saved-host check pending |
| BLE advertisement, availability, address privacy | Exact saved BLE address | Native matching/cleanup tests; dedicated hardware run pending |
| Existing per-target Deauth menu | AP BSSID; UI supplies no station MAC | Legacy implementation, not verified as working with the currently installed companion |
| Individual Wi-Fi client selection for offensive tests | Station MAC plus AP BSSID | Not exposed by the saved-device UI yet |

The transport helper accepts an optional station MAC, but the existing per-target UI passes `NULL`. An access-point target is not an individual-client selector. Offensive readiness will be reported separately from passive checks; no disruption tests are run against an unspecified target as part of Room Watch validation.
