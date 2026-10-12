// =============================================================================
// Battery-sense probe v5 — fast-drain GPIO18 monitor.
//
// Findings so far:
//   v3: GPIO18 is the only externally pulled-up pin; it reads ADC full scale.
//       It is NOT the USER SW button.
//   v4: GPIO18 stays at full scale with USB unplugged, so it is not a VBUS
//       detect. Most likely it is pulled up to the 3.3V rail, which would make
//       it a supply monitor: it only drops if the regulator falls out of
//       regulation near the end of the battery.
//
// This build drains the battery ~3x faster (continuous WiFi scanning + a busy
// second core) to reach end-of-battery quickly, and shows GPIO18 on the e-ink
// panel every ~10s. The panel keeps its last frame after power is lost, so the
// final frame answers: does the rail sag (readable warning) before shutdown?
//
// GPIO18 is on ADC2, which WiFi owns while active, so each sample is taken with
// WiFi briefly stopped.
// Build: env `pebl_battprobe`.
// =============================================================================
#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <GxEPD2_BW.h>
#include <epd/GxEPD2_213_B74.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>

static const uint8_t PIN_EPD_PWR  = 13;
static const uint8_t PIN_EPD_RST  = 8;
static const uint8_t PIN_EPD_CS   = 16;
static const uint8_t PIN_EPD_DC   = 12;
static const uint8_t PIN_EPD_BUSY = 14;
static const uint8_t PIN_EPD_SCLK = 10;
static const uint8_t PIN_EPD_MOSI = 9;
static const uint8_t PIN_PROBE    = 18;

static const uint32_t SAMPLE_PERIOD_MS = 10000;

GxEPD2_BW<GxEPD2_213_B74, GxEPD2_213_B74::HEIGHT>
    display(GxEPD2_213_B74(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY));

static int rawAt(adc_attenuation_t att) {
    analogSetPinAttenuation(PIN_PROBE, att);
    analogRead(PIN_PROBE);  // discard first conversion after reconfig
    long acc = 0;
    for (int i = 0; i < 16; i++) acc += analogRead(PIN_PROBE);
    return acc / 16;
}

// Keeps core 0 busy alongside the WiFi stack; the 1-tick delay lets the idle
// task run so the task watchdog stays fed.
static void burnTask(void *) {
    for (;;) {
        for (volatile uint32_t i = 0; i < 200000; i++) {}
        vTaskDelay(1);
    }
}

static void loadFor(uint32_t ms) {
    WiFi.mode(WIFI_STA);
    uint32_t start = millis();
    WiFi.scanNetworks(true);
    while (millis() - start < ms) {
        int st = WiFi.scanComplete();
        if (st != WIFI_SCAN_RUNNING) { WiFi.scanDelete(); WiFi.scanNetworks(true); }
        for (volatile uint32_t i = 0; i < 100000; i++) {}  // keep core 1 busy too
        delay(1);
    }
    WiFi.scanDelete();
    WiFi.mode(WIFI_OFF);
}

void setup() {
    Serial.begin(115200);
    pinMode(PIN_EPD_PWR, OUTPUT); digitalWrite(PIN_EPD_PWR, HIGH);
    delay(10);
    pinMode(PIN_EPD_CS,  OUTPUT); digitalWrite(PIN_EPD_CS, HIGH);
    pinMode(PIN_EPD_DC,  OUTPUT); digitalWrite(PIN_EPD_DC, LOW);
    pinMode(PIN_EPD_RST, OUTPUT); digitalWrite(PIN_EPD_RST, HIGH);
    pinMode(PIN_EPD_BUSY, INPUT);
    SPI.begin(PIN_EPD_SCLK, -1, PIN_EPD_MOSI, -1);
    display.init(115200, true, 2, false);
    display.setRotation(3);
    display.setTextColor(GxEPD_BLACK);
    WiFi.persistent(false);  // never touch the saved credentials
    xTaskCreatePinnedToCore(burnTask, "burn", 2048, nullptr, 0, nullptr, 0);
}

void loop() {
    static uint32_t n = 0;
    static int minR11 = 4095;

    int r0 = rawAt(ADC_0db), r6 = rawAt(ADC_6db), r11 = rawAt(ADC_11db);
    pinMode(PIN_PROBE, INPUT);
    int lvl = digitalRead(PIN_PROBE);
    if (r11 < minR11) minR11 = r11;
    Serial.printf("#%lu G18 raw0=%d raw6=%d raw11=%d min11=%d dig=%d\n",
                  (unsigned long)n, r0, r6, r11, minR11, lvl);

    if (n == 0) display.setFullWindow(); else display.setPartialWindow(0, 0, display.width(), display.height());
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setFont(&FreeSansBold24pt7b);
        display.setCursor(6, 44);
        display.printf("%d", r11);
        display.setFont(&FreeSansBold12pt7b);
        display.setCursor(150, 44);
        display.printf("dig %d", lvl);
        display.setCursor(6, 80);
        display.printf("min %d  6dB %d", minR11, r6);
        display.setCursor(6, 112);
        display.printf("FAST #%lu %lus", (unsigned long)n, (unsigned long)(millis() / 1000));
    } while (display.nextPage());
    n++;
    loadFor(SAMPLE_PERIOD_MS);
}
