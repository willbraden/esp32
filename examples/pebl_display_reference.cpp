// =============================================================================
// pebl ESP32-S3 — E-Ink Display Reference / Minimal Working Example
// =============================================================================
// Confirmed pinout for the pebl.ink board's DEPG0213BN (SSD1680) 2.13" display,
// reverse-engineered 2026-06-13. See EINK_PIN_DISCOVERY.md for the full story.
//
// This is a standalone, copy-pasteable example. Build env: lilygo_t5_s3_depg_bw
// (board esp32-s3-devkitc-1). It prints "WILL" then a counter on the screen.
// =============================================================================

#include <Arduino.h>
#include <SPI.h>
#include <GxEPD2_BW.h>
#include <epd/GxEPD2_213_B74.h>   // DEPG0213BN / SSD1680

// ----- CONFIRMED DISPLAY PINS (pebl ESP32-S3) -----
static const uint8_t PIN_EPD_PWR  = 13;  // panel power enable — MUST drive HIGH
static const uint8_t PIN_EPD_RST  = 8;
static const uint8_t PIN_EPD_CS   = 16;
static const uint8_t PIN_EPD_DC   = 12;
static const uint8_t PIN_EPD_BUSY = 14;
static const uint8_t PIN_EPD_SCLK = 10;
static const uint8_t PIN_EPD_MOSI = 9;

// GxEPD2_213_B74 constructor arg order is (CS, DC, RST, BUSY)
GxEPD2_BW<GxEPD2_213_B74, GxEPD2_213_B74::HEIGHT>
    display(GxEPD2_213_B74(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY));

void setup() {
    Serial.begin(115200);
    delay(500);

    // 1) Power the panel (without this, nothing works)
    pinMode(PIN_EPD_PWR, OUTPUT);
    digitalWrite(PIN_EPD_PWR, HIGH);

    // 2) Pre-claim control pins as GPIO so GxEPD2's early writes don't trip the
    //    ESP32-S3 peripheral manager ("IO X is not set as GPIO").
    pinMode(PIN_EPD_CS,  OUTPUT); digitalWrite(PIN_EPD_CS, HIGH);
    pinMode(PIN_EPD_DC,  OUTPUT); digitalWrite(PIN_EPD_DC, LOW);
    pinMode(PIN_EPD_RST, OUTPUT); digitalWrite(PIN_EPD_RST, HIGH);
    pinMode(PIN_EPD_BUSY, INPUT);
    delay(300);

    // 3) Bind SPI to the display's clock/data pins (no MISO, software CS)
    SPI.begin(PIN_EPD_SCLK, -1, PIN_EPD_MOSI, -1);

    // 4) Init + draw
    display.init(115200, true, 2, false);
    display.setRotation(1);                 // landscape 250x122
    display.setTextColor(GxEPD_BLACK);
    display.setFullWindow();

    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextSize(6);
        display.setCursor(6, 30);
        display.print("WILL");
    } while (display.nextPage());

    Serial.println("Display ready.");
}

void loop() {
    // E-ink retains its image with no power; only refresh when content changes.
}
