# Wake and sleep cycle

How the firmware decides what to do on each wake, the 30-minute schedule, and the RTC cache that makes button presses fast.

## Sub-features

- cold-boot: power-on or RESET, which fetches and draws the saved mode
- timer-wake: every `REFRESH_MINUTES` (30), fetch and redraw
- button-wake: USER SW, which toggles the mode then redraws (see `mode-button.md`)
- rtc-cache: last reading kept in RTC memory across deep sleep
- schedule-keeping: a cached redraw sleeps only the remaining time, so presses don't push the refresh back
- deep-sleep: radio off, display hibernated, wake on timer or GPIO16 low

## How to get to it (user POV)

Nothing to do. The panel updates itself every 30 minutes. RESET forces an immediate fetch. A USER SW press flips screens within a few seconds.

## Driving it

`setup()` flow:

1. Read the wake cause. On `ESP_SLEEP_WAKEUP_EXT0`, toggle the mode and save it to NVS.
2. `initDisplay()`.
3. On a button wake with a valid cache younger than `REFRESH_MINUTES*60 - 60` s: render from cache, then sleep `REFRESH_MINUTES*60 - age` s. No WiFi.
4. Otherwise: WiFi, location, weather, `cacheStore()`, render, sleep 1800 s.

Evidence: `Boot #N wake=...`, then either `Redrawing cached reading (Ns old)` or `WiFi OK` / `Loc` / `Temp`, then `Sleeping Ns`. On a cached redraw, N + the sleep seconds ≈ 1800.

Timer wakes take 30 minutes to occur. To prove timer behavior quickly, build with a temporary smaller `REFRESH_MINUTES`, prove it, and restore it before committing. Say that you did.

## Gotchas

- All RTC state (`bootCount`, `otaWakes`, the cache) is one struct, `rtc`, stamped with the running image's ELF SHA-256. It survives deep sleep but is cleared by RESET and power-off. After RESET the first button press has no cache and does a full fetch.
- RTC memory is reloaded on every reset but not on a deep-sleep wake, so only the image that wrote it may read it. Every image switch currently goes through a reset (OTA install, rollback, crash), but `rtcInit()` also checks the hash and starts fresh on a mismatch (`notes=rtc-fresh` if that ever happens on a wake). Any new RTC variable goes inside `RtcState`, never as a loose `RTC_DATA_ATTR`. That's expected.
- System time (`gettimeofday`) keeps counting through deep sleep. That's what makes the cache age correct. Nothing sets wall-clock time; the weekday comes from Open-Meteo, not the RTC.
- `sleepUntilNext()` waits up to 5 s for USER SW to be released before arming the level-triggered wake. Otherwise a held button would wake it immediately in a loop.
- `CachedWeather` must stay plain data. A `String` in RTC memory holds a heap pointer that is garbage after sleep.
