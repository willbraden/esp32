#pragma once
// =============================================================================
// Over-the-air updates for the pebl weather firmware.
//
// Pull model (the device deep-sleeps, so nothing can push to it): after a
// normal fetch + render, fetch a small signed manifest from the release
// channel, and if it names a newer version, stream that image into the spare
// OTA slot, verify the ECDSA P-256 signature against the key compiled in
// (ota_pubkey.h), then reboot into it.
//
// Safety:
//   * Unsigned / tampered / wrong-size images are aborted before the boot
//     partition changes, so the running firmware stays in charge.
//   * A new image boots in the bootloader's PENDING_VERIFY state and must call
//     confirmHealthy() after a successful fetch + render. If it reboots before
//     that, the bootloader rolls back to the previous image.
//   * A version that rolls back 3 times is never downloaded again (a single
//     rollback can be an unrelated crash, so it gets retried).
// Publishing side: .github/workflows/weather-release.yml + tools/ota_sign.py.
// =============================================================================
#include <Arduino.h>

#ifndef FW_VERSION
#define FW_VERSION "0.0.0+unknown"   // set by scripts/weather_version.py
#endif

// Channel manifest. Build-time config: stable for normal devices; a staging
// device is built with -D OTA_CHANNEL_URL=".../weather-ota-staging/manifest.json".
#ifndef OTA_CHANNEL_URL
#define OTA_CHANNEL_URL "https://github.com/willbraden/esp32/releases/download/weather-ota-stable/manifest.json"
#endif

namespace ota {

enum class Result { UpToDate, NoChannel, SkippedBad, Failed };

// Call once at boot, before anything can fail. Records a rollback if one just
// happened. Returns true if this image is a fresh update awaiting confirmation.
bool beginBoot();

// After beginBoot(): "1.2.0(attempt-2/3)" if the bootloader just rolled back
// from that version, else nullptr. For the boot log.
const char* rolledBackFrom();

// Mark the running image good (cancels the pending rollback). Cheap no-op when
// the image is already confirmed.
void confirmHealthy();

// Check the channel and install a newer signed build. onInstalling(version) is
// called once, just before the download starts (draw an "Updating" screen).
// On a successful install the chip restarts and this never returns.
Result checkAndInstall(void (*onInstalling)(const char* version));

// Semver precedence: true if a > b. Build metadata (+...) is ignored, and a
// prerelease (-rc1) sorts before its release.
bool isNewer(const char* a, const char* b);

}  // namespace ota
