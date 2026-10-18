# Status screens

Plain bordered text screens drawn by `renderMessage()` when a step fails. Each one is followed by a normal 30-minute sleep, so the device retries on its own.

## Sub-features

| Screen | When | Line 2 |
|---|---|---|
| WiFi reconnecting... | Timer or button wake, saved-credential reconnect timed out (25 s) | `wake #N, retrying in 30m` |
| WiFi not connected | Cold boot, no credentials, portal timed out (180 s) | `Join 'pebl-weather' to set up` |
| Location error | ip-api request failed or returned non-success | `Will retry shortly` |
| Weather error | Open-Meteo request or JSON parse failed | `Will retry shortly` |

## How to get to it (user POV)

Only when something is wrong: router down, no internet, or an API outage.

## Driving it

- WiFi reconnecting: power the router off, or move the device out of range, then wait for or trigger a wake.
- Location/Weather error: block the host at the router or use a DNS sinkhole. There's no in-firmware switch. Don't add a test-only bypass to production code.
- Evidence: the matching serial line (`Location lookup failed`, `Weather fetch failed`, `WiFi reconnect failed on wake`) plus a panel photo.

## Gotchas

- Status screens don't have the weekday letter or the cached reading. A failed fetch replaces a good screen with an error rather than keeping the old data. Possible improvement: on failure, redraw from the RTC cache if it's recent.
- "Will retry shortly" actually means the next 30-minute wake.
