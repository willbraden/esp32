#pragma once
// =============================================================================
// Persistent boot log for the pebl weather firmware.
//
// Each boot keeps one text record in NVS (namespace "pebl-boot", key "cur") and
// rewrites it as it passes each stage, so the record survives whatever ends the
// boot: deep sleep, a panic, an OTA restart, a rollback, or a power cut. The
// next boot archives it into a ring of the last 16 boots ("h0".."h15"), adding
// next-boot=<its own reset reason>, and prints the most recent ones.
//
// Text, not a struct, on purpose: an OTA rollback means a different image reads
// what the failed image wrote, and both must agree on the format. Read it from
// serial at boot, or from a flash dump with tools/boot_log.py (works even when
// serial output is lost).
//
// There is no wall clock on this device, so records are ordered by a boot
// sequence number that never resets, and times are uptime in seconds.
// =============================================================================
#include <Arduino.h>

namespace bootlog {

// Call once, first thing after Serial is up. Archives the previous boot's
// record and starts this one. trial: this image is an unconfirmed OTA update.
// rollbackFrom: version the bootloader just rolled back from, or nullptr.
void begin(bool trial, const char* rollbackFrom);

// Mark a stage passed: "wifi", "loc", "weather", "drawn", "cache", "ota",
// "sleep". The record keeps the whole path (boot>wifi>loc>...), so the last
// entry is how far the boot got. Persisted immediately.
void stage(const char* name);

// Named facts about this boot, persisted immediately.
void fail(const char* what);           // e.g. "wifi", "wifi+portal-timeout", "loc"
void ota(const char* result);          // e.g. "up-to-date", "installing-1.2.0"
void wifiMs(uint32_t ms);              // time spent on the direct reconnect
void button(uint32_t heldMs, bool longPress);   // a USER SW wake: how long it was held
void note(const char* what);           // appended, e.g. "wifi-retry-2", "rtc-fresh"
// A finished screen refresh: which screen ("numbers", "word", "msg"), how long
// the panel held BUSY (~2300 ms is a real full refresh), and whether a USB host
// was attached at the time. Appended, so a boot that draws twice keeps both.
void draw(const char* what, uint32_t busyMs, bool usbHost);

}  // namespace bootlog
