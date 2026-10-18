# Multi-surface journeys

Runs that cross several features. Per-feature details live in the linked files; read those first. Baseline preconditions in [the README](README.md) apply.

## Fresh flash to steady state

1. Flash `pebl_weather` (`flashing-and-recovery.md`), start it, and log with `--until Sleeping`.
2. Expect, in order: `wake=0 (power-on/reset)`, `Mode M`, `WiFi OK`, `Loc:`, `Temp ...`, exactly one `_Update_Full`, `Drew numbers: panel busy ~2284ms` (or `word`), `Sleeping 1800s`.
3. No `assert failed` / `Rebooting...` in between. If there is one, it's the open crash in `known-issues.md`; report it, don't call the run clean.
4. Panel photo matches the saved mode's screen, with today's weekday letter on the bottom row.

## Mode round trip with persistence

1. Logger detached. Press USER SW: the screen flips (numbers → word, or back) within a few seconds, with no WiFi needed.
2. Press again: it flips back.
3. Leave it on the word screen. Unplug USB, power switch off, then on: it boots and fetches, and **stays on the word screen**.
4. Wait for (or shorten, then restore) one timer wake: still the word screen, with fresh data.

## Cached press keeps the schedule

1. Note the time of a fetch from the log (`Sleeping 1800s` right after `Temp ...`), then detach the logger.
2. About 10 minutes later, press USER SW. The screen flips with no WiFi delay.
3. Reattach the logger and wait. The next `Temp ...` fetch should arrive about 30 minutes after the original fetch, not 30 minutes after the press. (Attaching during the press would reset the chip and void the test; see `serial-diagnostics.md`.)

## Network loss and recovery

1. Router off. Timer or button wake: the "WiFi reconnecting..." screen with the wake number, then sleep.
2. Router on. Next wake: normal screen again; the mode is unchanged.

## Over-the-air update on battery

The production path end to end. Run it after each release that touches OTA, boot, the display or power.

1. Board on the previous release (or a local `pebl_weather` build, which counts as equal to its tag), switch on, USB unplugged.
2. Hold USER SW ~2 s: "Updating firmware... vA -> vB" for about a minute, then the weather. Ask the user what the panel shows.
3. Plug in, download mode, `tools/boot_log.py`: the install boot has `long-press ota=installing-B ... usb=none next-boot=sw-restart`; the trial boot has `vB ... trial draw=numbers:~2300ms usb=none`; the OTA state has no `try_ver`.

Passed for `weather-v1.1.2` on 2026-10-13. The same run on `weather-v1.1.1` once left the panel blank; see `known-issues.md`.

## Dead-battery cue

Not practical to run on demand (the forced drain took about 8 hours). The proof is that the weekday comes from the reading's date: when the device stops updating, the letter keeps the last reading's weekday. Covered by `weekday-letter.md`.
