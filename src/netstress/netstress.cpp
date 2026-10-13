// =============================================================================
// Network stress test — isolate the lwIP "pbuf_free: p->ref > 0" crash.
//
// History (see the verify-pebl skill, known-issues.md):
//   v1: steady-state loops (one boot, many requests). Old core crashed ~18%;
//       arduino-esp32 3.3.11 / IDF 5.5.5 ran 90/90 clean, yet the weather
//       firmware still crashed ~30% of boots. 7 of 11 old-core crashes hit on
//       the 1st/2nd request after a boot.
//   v2 (this): one round per BOOT, then ESP.restart(), like the real firmware
//       and like an OTA trial boot. lwIP here is built without
//       LWIP_RANDOMIZE_INITIAL_LOCAL_PORTS, so every boot's first connection
//       reuses the same source port. Two phases:
//         phase 0: baseline (fixed ports)
//         phase 1: advance lwIP's port counter by a random amount first
//       If phase 1 stops crashing, source-port reuse across reboots is the trigger.
// Counts survive the reboots in RTC_NOINIT memory.
// Build: env `pebl_netstress`. Read results over serial.
// =============================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_system.h>
#include <lwip/sockets.h>

static const int ROUNDS = 30;
static const uint32_t MAGIC = 0x5EED5EEE;

struct State { uint32_t magic; int phase, round, crashes[2], ok[2]; uint16_t ports[2][ROUNDS]; };
RTC_NOINIT_ATTR State st;

// Advance lwIP's ephemeral-port counter by a random amount: each bind to port 0
// takes the next port. Emulates LWIP_RANDOMIZE_INITIAL_LOCAL_PORTS.
static void shufflePorts() {
    int n = 1 + (esp_random() % 1000);
    for (int i = 0; i < n; i++) {
        int s = lwip_socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0) break;
        struct sockaddr_in a = {};
        a.sin_family = AF_INET;
        a.sin_port = 0;
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        lwip_bind(s, (struct sockaddr*)&a, sizeof(a));
        lwip_close(s);
    }
}

// ---- verbatim from src/weather/weather_main.cpp, plus local-port capture ----
static String httpGet(const String& url, bool secure, uint16_t* localPort) {
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
            int code = http.GET();
            if (localPort) *localPort = c.localPort();
            if (code == 200) body = http.getString();
            else Serial.printf("HTTP %d %s\n", code, url.c_str());
            http.end();
        }
    }
    return body;
}
// -----------------------------------------------------------------------------

static const char* HTTP_URL  = "http://ip-api.com/json/?fields=status,lat,lon,city";
static const char* HTTPS_URL = "https://api.open-meteo.com/v1/forecast?latitude=39.4895&longitude=-104.8447"
                               "&current=temperature_2m,weather_code&daily=temperature_2m_max,temperature_2m_min"
                               "&temperature_unit=fahrenheit&timezone=auto&forecast_days=1";

static void report() {
    const char* names[] = {"fixed ports", "shuffled ports"};
    Serial.println("---- results (one round per boot) ----");
    for (int p = 0; p < 2; p++) {
        Serial.printf("  %-15s ok=%2d crashes=%2d  first ports:", names[p], st.ok[p], st.crashes[p]);
        for (int i = 0; i < 8 && i < ROUNDS; i++) Serial.printf(" %u", st.ports[p][i]);
        Serial.println();
    }
}

void setup() {
    Serial.begin(115200);
    delay(800);
    esp_reset_reason_t why = esp_reset_reason();
    if (st.magic != MAGIC || (why != ESP_RST_PANIC && why != ESP_RST_SW)) {
        memset(&st, 0, sizeof(st));
        st.magic = MAGIC;
    } else if (why == ESP_RST_PANIC && st.phase < 2) {
        st.crashes[st.phase]++;           // the previous boot's round crashed
        st.round++;
    }
    if (st.phase < 2 && st.round >= ROUNDS) { st.phase++; st.round = 0; }
    Serial.printf("\n=== netstress v2 === reset=%d phase=%d round=%d\n", (int)why, st.phase, st.round);
    report();
    if (st.phase >= 2) { Serial.println("DONE"); return; }

    WiFi.mode(WIFI_STA);
    WiFi.begin();
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 25000) delay(200);
    if (WiFi.status() != WL_CONNECTED) { Serial.println("WiFi FAILED"); ESP.restart(); }

    int p = st.phase;
    if (p == 1) shufflePorts();
    uint16_t port = 0;
    bool ok = httpGet(HTTP_URL, false, &port).length() > 0;
    ok &= httpGet(HTTPS_URL, true, nullptr).length() > 0;
    st.ports[p][st.round] = port;
    Serial.printf("phase %d round %d port %u %s\n", p, st.round, port, ok ? "ok" : "empty");
    if (ok) st.ok[p]++;
    st.round++;
    Serial.flush();
    ESP.restart();                        // like an OTA trial boot: reconnect immediately
}

void loop() {
    report();
    Serial.println("DONE");
    delay(10000);
}
