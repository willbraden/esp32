// =============================================================================
// Button probe — find which GPIO (if any) the USER SW button is wired to.
//
// Findings so far:
//   - Internal pull-ups on all free pins: BOOT shows as GPIO0, USER SW moves nothing.
//   - Internal pull-downs on all free pins: USER SW moves nothing.
// This pass also covers the display pins (display left unpowered) and samples
// every pin under BOTH pulls each cycle, classifying it as:
//   float (reads 1 with pull-up, 0 with pull-down)
//   GND   (reads 0/0 — something is pulling it low, e.g. a pressed active-low button)
//   3V3   (reads 1/1 — pulled high, e.g. a pressed active-high button)
// and prints every change, so a press of either polarity shows up.
//
// Pins left alone: USB (19,20), SPI flash (26-32), not broken out on WROOM-1 (33,34).
// Build: env `pebl_btnprobe`. Press each button while watching serial.
// =============================================================================
#include <Arduino.h>

static const int PINS[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
                           21, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48};
static const int N = sizeof(PINS) / sizeof(PINS[0]);
static const char* const NAMES[] = {"float", "GND", "3V3", "odd"};
static int last[N];

static int classify(int p) {
    pinMode(p, INPUT_PULLUP);   delayMicroseconds(300); int up = digitalRead(p);
    pinMode(p, INPUT_PULLDOWN); delayMicroseconds(300); int dn = digitalRead(p);
    if (up && !dn) return 0;    // float
    if (!up && !dn) return 1;   // driven/pulled low
    if (up && dn) return 2;     // driven/pulled high
    return 3;                   // reads 0 with pull-up and 1 with pull-down (shouldn't happen)
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== pebl button probe v3 === (both pulls, display pins included)");
    Serial.print("non-floating at rest:");
    for (int i = 0; i < N; i++) {
        last[i] = classify(PINS[i]);
        if (last[i]) Serial.printf(" G%d=%s", PINS[i], NAMES[last[i]]);
    }
    Serial.println();
}

void loop() {
    for (int i = 0; i < N; i++) {
        int c = classify(PINS[i]);
        if (c != last[i]) {
            Serial.printf("%8lu ms  G%d: %s -> %s\n", (unsigned long)millis(), PINS[i],
                          NAMES[last[i]], NAMES[c]);
            last[i] = c;
        }
    }
    static uint32_t beat = 0;
    if (millis() - beat > 5000) { beat = millis(); Serial.println("(alive)"); }
    delay(5);
}
