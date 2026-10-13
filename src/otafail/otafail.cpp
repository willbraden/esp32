// =============================================================================
// OTA rollback test image (env pebl_otafail). Shipped through the normal signed
// OTA path to prove rollback: it never confirms itself (no
// esp_ota_mark_app_valid_cancel_rollback) and reboots after 5 s, so the
// bootloader must mark it invalid and boot the previous weather firmware, which
// then logs the rollback and refuses to download this version again.
// =============================================================================
#include <Arduino.h>

extern "C" bool verifyRollbackLater() { return true; }   // stop the core auto-confirming

void setup() {
    Serial.begin(115200);
    delay(800);
    Serial.println("\n=== otafail: unconfirmed OTA image, rebooting in 5 s ===");
    delay(5000);
    ESP.restart();
}

void loop() {}
