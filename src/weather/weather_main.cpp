// =============================================================================
// pebl ESP32-S3 — Weather Display
// =============================================================================
// A self-contained weather-station firmware for the pebl.ink board's
// DEPG0213BN (SSD1680) 2.13" e-ink display.
//
//   * Reuses the WiFi credentials already provisioned on the device (falls back
//     to a captive-portal "pebl-weather" AP if none are saved).
//   * Auto-detects location by IP (ip-api.com) — no config needed.
//   * Fetches current conditions + daily high/low from Open-Meteo (free, no key).
//   * Renders a clean weather card, then deep-sleeps REFRESH_MINUTES and repeats.
//
// Build: env `pebl_weather` (board esp32-s3-devkitc-1). Confirmed display pins
// are below — see EINK_PIN_DISCOVERY.md for the reverse-engineering story.
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <GxEPD2_BW.h>
#include <epd/GxEPD2_213_B74.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <esp_sleep.h>

// ----- CONFIRMED DISPLAY PINS (pebl ESP32-S3) -----
static const uint8_t PIN_EPD_PWR  = 13;  // panel power enable — MUST drive HIGH
static const uint8_t PIN_EPD_RST  = 8;
static const uint8_t PIN_EPD_CS   = 16;
static const uint8_t PIN_EPD_DC   = 12;
static const uint8_t PIN_EPD_BUSY = 14;
static const uint8_t PIN_EPD_SCLK = 10;
static const uint8_t PIN_EPD_MOSI = 9;

// ----- BEHAVIOR -----
#define REFRESH_MINUTES   30      // how often to wake and update
#define USE_FAHRENHEIT    1       // 1 = °F, 0 = °C
#define WIFI_TIMEOUT_MS   25000   // max wait for WiFi reconnect on each wake

RTC_DATA_ATTR int bootCount = 0;  // survives deep sleep — counts wake cycles

GxEPD2_BW<GxEPD2_213_B74, GxEPD2_213_B74::HEIGHT>
    display(GxEPD2_213_B74(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY));

// ----- weather model -----
struct Weather {
    bool   ok = false;
    float  temp = 0, hi = 0, lo = 0;
    int    code = 0;
    String city = "";
    String updated = "";   // "3:45 PM"
};

// Icon categories derived from WMO weather codes
enum Cat { CAT_CLEAR, CAT_PCLOUD, CAT_CLOUD, CAT_FOG, CAT_RAIN, CAT_SNOW, CAT_STORM };

static Cat catOf(int c) {
    if (c == 0)                      return CAT_CLEAR;
    if (c == 1 || c == 2)            return CAT_PCLOUD;
    if (c == 3)                      return CAT_CLOUD;
    if (c == 45 || c == 48)          return CAT_FOG;
    if (c >= 71 && c <= 77)          return CAT_SNOW;
    if (c == 85 || c == 86)          return CAT_SNOW;
    if (c >= 95)                     return CAT_STORM;
    if (c >= 51 && c <= 67)          return CAT_RAIN;
    if (c >= 80 && c <= 82)          return CAT_RAIN;
    return CAT_CLOUD;
}

static const char* labelOf(int c) {
    switch (c) {
        case 0:  return "Clear";
        case 1:  return "Mostly Clear";
        case 2:  return "Partly Cloudy";
        case 3:  return "Cloudy";
        case 45: case 48: return "Fog";
        case 51: case 53: case 55: return "Drizzle";
        case 56: case 57: return "Frz Drizzle";
        case 61: return "Light Rain";
        case 63: return "Rain";
        case 65: return "Heavy Rain";
        case 66: case 67: return "Frz Rain";
        case 71: return "Light Snow";
        case 73: return "Snow";
        case 75: return "Heavy Snow";
        case 77: return "Snow";
        case 80: case 81: return "Showers";
        case 82: return "Heavy Showers";
        case 85: case 86: return "Snow Showers";
        case 95: return "Thunderstorm";
        case 96: case 99: return "Thunderstorm";
        default: return "—";
    }
}

// ----- HTTP helper -----
static String httpGet(const String& url, bool secure) {
    HTTPClient http;
    String body;
    if (secure) {
        // Static so the TLS client outlives the HTTPClient teardown cleanly
        // (avoids lwIP pbuf double-free asserts seen with stack-scoped clients).
        static WiFiClientSecure c;
        c.setInsecure();                 // skip cert validation (fine for weather)
        if (http.begin(c, url)) {
            http.setTimeout(9000);
            int code = http.GET();
            if (code == 200) body = http.getString();
            else Serial.printf("HTTP %d (secure) %s\n", code, url.c_str());
            http.end();
        }
        c.stop();
    } else {
        WiFiClient c;
        if (http.begin(c, url)) {
            http.setTimeout(9000);
            int code = http.GET();
            if (code == 200) body = http.getString();
            else Serial.printf("HTTP %d %s\n", code, url.c_str());
            http.end();
        }
    }
    return body;
}

// ----- data fetch -----
static bool getLocation(double& lat, double& lon, String& city) {
    String body = httpGet("http://ip-api.com/json/?fields=status,lat,lon,city", false);
    if (body.isEmpty()) return false;
    JsonDocument doc;
    if (deserializeJson(doc, body)) return false;
    if (String(doc["status"] | "") != "success") return false;
    lat  = doc["lat"]  | 0.0;
    lon  = doc["lon"]  | 0.0;
    city = String((const char*)(doc["city"] | "Weather"));
    return true;
}

static String to12h(const String& hm) {        // "15:45" -> "3:45 PM"
    int colon = hm.indexOf(':');
    if (colon < 0) return hm;
    int h = hm.substring(0, colon).toInt();
    String m = hm.substring(colon + 1);
    const char* ap = (h >= 12) ? "PM" : "AM";
    int h12 = h % 12; if (h12 == 0) h12 = 12;
    return String(h12) + ":" + m + " " + ap;
}

static bool getWeather(double lat, double lon, Weather& w) {
    char url[320];
    snprintf(url, sizeof(url),
        "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
        "&current=temperature_2m,weather_code"
        "&daily=temperature_2m_max,temperature_2m_min"
        "&temperature_unit=%s&timezone=auto&forecast_days=1",
        lat, lon, USE_FAHRENHEIT ? "fahrenheit" : "celsius");

    String body = httpGet(url, true);
    if (body.isEmpty()) return false;
    JsonDocument doc;
    if (deserializeJson(doc, body)) return false;

    w.temp = doc["current"]["temperature_2m"] | 0.0f;
    w.code = doc["current"]["weather_code"]    | 0;
    w.hi   = doc["daily"]["temperature_2m_max"][0] | 0.0f;
    w.lo   = doc["daily"]["temperature_2m_min"][0] | 0.0f;

    String t = String((const char*)(doc["current"]["time"] | ""));  // "2026-06-13T15:45"
    int tpos = t.indexOf('T');
    w.updated = (tpos >= 0) ? to12h(t.substring(tpos + 1)) : "";
    w.ok = true;
    return true;
}

// ----- icon drawing (filled black silhouettes) -----
static void drawSun(int cx, int cy, int r) {
    display.fillCircle(cx, cy, r, GxEPD_BLACK);
    for (int i = 0; i < 8; i++) {
        float a = i * PI / 4.0;
        int x1 = cx + cos(a) * (r + 4), y1 = cy + sin(a) * (r + 4);
        int x2 = cx + cos(a) * (r + 10), y2 = cy + sin(a) * (r + 10);
        display.drawLine(x1, y1, x2, y2, GxEPD_BLACK);
    }
}

static void drawCloud(int cx, int cy, int w) {
    int r1 = w * 0.20, r2 = w * 0.28, r3 = w * 0.22;
    display.fillCircle(cx - w * 0.26, cy,             r1, GxEPD_BLACK);
    display.fillCircle(cx - w * 0.02, cy - w * 0.12,  r2, GxEPD_BLACK);
    display.fillCircle(cx + w * 0.28, cy - w * 0.02,  r3, GxEPD_BLACK);
    display.fillRect(cx - w * 0.46, cy, w * 0.94, w * 0.22, GxEPD_BLACK);
}

static void drawIcon(int code, int bx, int by, int s) {
    // (bx,by) = center of an s-wide icon box
    switch (catOf(code)) {
        case CAT_CLEAR:
            drawSun(bx, by, s * 0.28);
            break;
        case CAT_PCLOUD:
            drawSun(bx - s * 0.16, by - s * 0.18, s * 0.18);
            drawCloud(bx + s * 0.05, by + s * 0.12, s * 0.8);
            break;
        case CAT_CLOUD:
            drawCloud(bx, by, s * 0.9);
            break;
        case CAT_FOG:
            drawCloud(bx, by - s * 0.12, s * 0.8);
            for (int i = 0; i < 3; i++)
                display.drawFastHLine(bx - s * 0.36, by + s * 0.18 + i * 6, s * 0.72, GxEPD_BLACK);
            break;
        case CAT_RAIN:
            drawCloud(bx, by - s * 0.14, s * 0.8);
            for (int i = -1; i <= 1; i++)
                display.drawLine(bx + i * 10, by + s * 0.16,
                                 bx + i * 10 - 4, by + s * 0.34, GxEPD_BLACK);
            break;
        case CAT_SNOW:
            drawCloud(bx, by - s * 0.14, s * 0.8);
            for (int i = -1; i <= 1; i++)
                display.fillCircle(bx + i * 11, by + s * 0.26, 2, GxEPD_BLACK);
            break;
        case CAT_STORM:
            drawCloud(bx, by - s * 0.14, s * 0.8);
            display.fillTriangle(bx, by + s * 0.10, bx - 8, by + s * 0.32,
                                 bx + 2, by + s * 0.30, GxEPD_BLACK);
            display.fillTriangle(bx + 2, by + s * 0.30, bx + 9, by + s * 0.30,
                                 bx + 1, by + s * 0.52, GxEPD_BLACK);
            break;
    }
}

// ----- rendering -----
static void drawDegreeF(int x, int topY, bool showF) {
    // small degree ring + optional F, drawn just right of the big number
    display.drawCircle(x + 6, topY + 7, 4, GxEPD_BLACK);
    display.drawCircle(x + 6, topY + 7, 3, GxEPD_BLACK);
    if (showF) {
        display.setFont(&FreeSansBold9pt7b);
        display.setCursor(x + 12, topY + 24);
        display.print(USE_FAHRENHEIT ? "F" : "C");
    }
}

// One-word condition for the big readable layout (mockup uses single words).
static const char* shortLabelOf(int code) {
    switch (catOf(code)) {
        case CAT_CLEAR:  return "Clear";
        case CAT_PCLOUD: return "Cloudy";
        case CAT_CLOUD:  return "Cloudy";
        case CAT_FOG:    return "Fog";
        case CAT_RAIN:   return "Rain";
        case CAT_SNOW:   return "Snow";
        case CAT_STORM:  return "Storm";
    }
    return "—";
}

// Pixel width of an integer rendered in the current font/size.
static int numWidth(int val) {
    char b[8]; snprintf(b, sizeof(b), "%d", val);
    int16_t x, y; uint16_t w, h;
    display.getTextBounds(b, 0, 0, &x, &y, &w, &h);
    return w;
}

// Draw an integer temperature at (leftX, baseline) with a trailing degree ring.
static void drawTempAt(int val, int leftX, int baseY, int degR, int degCenterY) {
    char b[8]; snprintf(b, sizeof(b), "%d", val);
    display.setCursor(leftX, baseY);
    display.print(b);
    int ex = display.getCursorX();
    display.drawCircle(ex + degR + 3, degCenterY, degR, GxEPD_BLACK);
    if (degR > 4) display.drawCircle(ex + degR + 3, degCenterY, degR - 1, GxEPD_BLACK);
}

static void renderWeather(const Weather& w) {
    int W = display.width(), H = display.height();
    // ---- LAYOUT KNOBS (tweak these) ----
    const int DIV       = 92;   // x split: left = High/Low, right = current temp
    const int HI_BASE   = 56;   // baseline y of the High temp (left, upper)
    const int LO_BASE   = 106;  // baseline y of the Low temp  (left, lower)
    const int CUR_BASE  = 68;   // baseline y of the big current temp (right)
    const int COND_BASE = 102;  // baseline y of the condition word (right)

    display.setTextColor(GxEPD_BLACK);
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        // ---- LEFT: High over Low, stacked ----
        display.setFont(&FreeSansBold18pt7b);
        display.setTextSize(1);
        drawTempAt((int)lroundf(w.hi), 10, HI_BASE, 5, HI_BASE - 21);
        drawTempAt((int)lroundf(w.lo), 10, LO_BASE, 5, LO_BASE - 21);

        // ---- RIGHT: big current temp, centered in the right column ----
        display.setFont(&FreeSansBold18pt7b);
        display.setTextSize(2);
        int cur  = (int)lroundf(w.temp);
        int degR = 8;
        int totalW = numWidth(cur) + degR * 2 + 5;
        int rcx = DIV + (W - DIV) / 2;                 // center of right column
        int sx  = rcx - totalW / 2;
        if (sx < DIV + 2) sx = DIV + 2;
        drawTempAt(cur, sx, CUR_BASE, degR, CUR_BASE - 40);

        // ---- condition word, centered under the temp ----
        display.setTextSize(1);
        display.setFont(&FreeSansBold12pt7b);
        const char* lab = shortLabelOf(w.code);
        int16_t bx, by; uint16_t bw, bh;
        display.getTextBounds(lab, 0, 0, &bx, &by, &bw, &bh);
        int lx = rcx - bw / 2;
        if (lx < DIV + 2) lx = DIV + 2;
        display.setCursor(lx, COND_BASE);
        display.print(lab);
    } while (display.nextPage());
}

static void renderMessage(const char* line1, const char* line2) {
    int W = display.width(), H = display.height();
    display.setTextColor(GxEPD_BLACK);
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.drawRect(0, 0, W, H, GxEPD_BLACK);
        display.setFont(&FreeSansBold9pt7b);
        display.setCursor(12, 56);
        display.print(line1);
        if (line2) {
            display.setFont(&FreeSans9pt7b);
            display.setCursor(12, 82);
            display.print(line2);
        }
    } while (display.nextPage());
}

// ----- display bring-up -----
static void initDisplay() {
    pinMode(PIN_EPD_PWR, OUTPUT); digitalWrite(PIN_EPD_PWR, HIGH);
    delay(300);
    pinMode(PIN_EPD_CS,  OUTPUT); digitalWrite(PIN_EPD_CS, HIGH);
    pinMode(PIN_EPD_DC,  OUTPUT); digitalWrite(PIN_EPD_DC, LOW);
    pinMode(PIN_EPD_RST, OUTPUT); digitalWrite(PIN_EPD_RST, HIGH);
    pinMode(PIN_EPD_BUSY, INPUT);
    SPI.begin(PIN_EPD_SCLK, -1, PIN_EPD_MOSI, -1);
    display.init(115200, true, 2, false);
    display.setRotation(3);   // landscape, flipped 180° from rotation 1
}

static void sleepAndRepeat() {
    display.hibernate();
    WiFi.disconnect(true, false);   // power down radio cleanly before sleep
    WiFi.mode(WIFI_OFF);
    esp_sleep_enable_timer_wakeup((uint64_t)REFRESH_MINUTES * 60ULL * 1000000ULL);
    Serial.printf("Sleeping %d min...\n", REFRESH_MINUTES);
    Serial.flush();
    esp_deep_sleep_start();
}

// Fast, direct WiFi reconnect using the credentials saved in NVS (set during
// first-time QR provisioning). Returns true if connected within the timeout.
// This avoids WiFiManager's captive portal blocking for minutes on a wake.
static bool connectWiFiDirect(uint32_t timeoutMs) {
    WiFi.persistent(true);
    WiFi.mode(WIFI_STA);
    WiFi.begin();                   // reuse stored SSID/password
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs) {
        delay(200);
    }
    return WiFi.status() == WL_CONNECTED;
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n=== pebl weather ===");

    esp_sleep_wakeup_cause_t wake = esp_sleep_get_wakeup_cause();
    bool timerWake = (wake == ESP_SLEEP_WAKEUP_TIMER);
    bootCount++;
    Serial.printf("Boot #%d  wake=%d %s\n", bootCount, (int)wake,
                  timerWake ? "(TIMER WAKE)" : "(power-on/reset)");

    initDisplay();

    // Try a fast direct reconnect with saved creds first.
    if (!connectWiFiDirect(WIFI_TIMEOUT_MS)) {
        if (timerWake) {
            // Unattended wake: never hang in a portal. Note it on screen and
            // retry next cycle (so we can SEE that the wake happened on battery).
            Serial.println("WiFi reconnect failed on wake — retry next cycle");
            char l2[40];
            snprintf(l2, sizeof(l2), "wake #%d, retrying in %dm", bootCount, REFRESH_MINUTES);
            renderMessage("WiFi reconnecting...", l2);
            sleepAndRepeat();
        }
        // Cold boot / button press: offer the provisioning portal.
        Serial.println("No saved WiFi — starting provisioning portal");
        WiFiManager wm;
        wm.setConfigPortalTimeout(180);
        if (!wm.autoConnect("pebl-weather")) {
            renderMessage("WiFi not connected", "Join 'pebl-weather' to set up");
            sleepAndRepeat();
        }
    }
    Serial.printf("WiFi OK: %s\n", WiFi.localIP().toString().c_str());

    double lat = 0, lon = 0; String city;
    if (!getLocation(lat, lon, city)) {
        Serial.println("Location lookup failed");
        renderMessage("Location error", "Will retry shortly");
        sleepAndRepeat();
    }
    Serial.printf("Loc: %s (%.4f, %.4f)\n", city.c_str(), lat, lon);

    Weather w;
    if (!getWeather(lat, lon, w)) {
        Serial.println("Weather fetch failed");
        renderMessage("Weather error", "Will retry shortly");
        sleepAndRepeat();
    }
    w.city = city;
    Serial.printf("Temp %.1f  Hi %.1f  Lo %.1f  code %d  @ %s\n",
                  w.temp, w.hi, w.lo, w.code, w.updated.c_str());

    renderWeather(w);
    sleepAndRepeat();
}

void loop() { /* never reached — deep sleep restarts setup() */ }
