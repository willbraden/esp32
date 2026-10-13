// =============================================================================
// Network stress test — isolate the lwIP "pbuf_free: p->ref > 0" crash seen in
// the weather firmware right after the ip-api lookup, as the HTTPS weather
// request starts.
//
// Runs three phases of ROUNDS requests each, using a verbatim copy of the
// weather firmware's httpGet():
//   phase 0: plain HTTP only  (ip-api.com)
//   phase 1: HTTPS only       (api.open-meteo.com)
//   phase 2: HTTP then HTTPS back to back (the real weather sequence)
// Progress and per-phase crash counts live in RTC_NOINIT memory, which survives
// the panic reboot, so the test resumes where it crashed and keeps tallying.
// Build: env `pebl_netstress`. Read results over serial.
// =============================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_system.h>

static const int ROUNDS = 30;
// Experiment 2: HTTPClient keeps plain-HTTP sockets open for reuse by default, so the
// socket is only closed later by ~WiFiClient. HTTP_NO_REUSE=1 makes end() stop it.
#ifndef HTTP_NO_REUSE
#define HTTP_NO_REUSE 0
#endif
static const uint32_t MAGIC = 0x5EED5EED;

struct State { uint32_t magic; int phase, round, crashes[3], ok[3]; };
RTC_NOINIT_ATTR State st;

// ---- verbatim from src/weather/weather_main.cpp ----
static String httpGet(const String& url, bool secure) {
    HTTPClient http;
    String body;
    if (secure) {
        static WiFiClientSecure c;
        c.setInsecure();
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
            if (HTTP_NO_REUSE) http.setReuse(false);
            int code = http.GET();
            if (code == 200) body = http.getString();
            else Serial.printf("HTTP %d %s\n", code, url.c_str());
            http.end();
        }
    }
    return body;
}
// -----------------------------------------------------

static const char* HTTP_URL  = "http://ip-api.com/json/?fields=status,lat,lon,city";
static const char* HTTPS_URL = "https://api.open-meteo.com/v1/forecast?latitude=39.4895&longitude=-104.8447"
                               "&current=temperature_2m,weather_code&daily=temperature_2m_max,temperature_2m_min"
                               "&hourly=precipitation_probability,snowfall,weather_code&forecast_hours=6"
                               "&temperature_unit=fahrenheit&timezone=auto&forecast_days=1";

static void report() {
    const char* names[] = {"HTTP only", "HTTPS only", "HTTP->HTTPS"};
    Serial.println("---- results ----");
    for (int p = 0; p < 3; p++)
        Serial.printf("  %-12s ok=%2d crashes=%2d\n", names[p], st.ok[p], st.crashes[p]);
}

void setup() {
    Serial.begin(115200);
    delay(800);
    esp_reset_reason_t why = esp_reset_reason();
    if (st.magic != MAGIC || (why != ESP_RST_PANIC && why != ESP_RST_SW)) {
        memset(&st, 0, sizeof(st));       // fresh run on power-on / RESET button
        st.magic = MAGIC;
    } else if (why == ESP_RST_PANIC && st.phase < 3) {
        st.crashes[st.phase]++;           // the round in progress crashed
        st.round++;
    }
    Serial.printf("\n=== netstress (no_reuse=%d) === reset=%d phase=%d round=%d\n", HTTP_NO_REUSE, (int)why, st.phase, st.round);
    report();

    WiFi.mode(WIFI_STA);
    WiFi.begin();
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 25000) delay(200);
    Serial.printf("WiFi %s\n", WiFi.status() == WL_CONNECTED ? "OK" : "FAILED");
}

void loop() {
    if (st.phase >= 3) {
        report();
        Serial.println("DONE");
        delay(10000);
        return;
    }
    if (st.round >= ROUNDS) { st.phase++; st.round = 0; return; }

    int p = st.phase;
    Serial.printf("phase %d round %d ... ", p, st.round);
    bool ok = true;
    if (p == 0 || p == 2) ok &= httpGet(HTTP_URL, false).length() > 0;
    if (p == 1 || p == 2) ok &= httpGet(HTTPS_URL, true).length() > 0;
    Serial.println(ok ? "ok" : "empty");
    if (ok) st.ok[p]++;
    st.round++;
    delay(500);
}
