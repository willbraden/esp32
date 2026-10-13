// =============================================================================
// Network stress test — isolate the lwIP "pbuf_free: p->ref > 0" crash.
//
// History (see the verify-pebl skill, known-issues.md):
//   v1: steady-state loops (one boot, many requests). Old core crashed ~18%;
//       arduino-esp32 3.3.11 / IDF 5.5.5 ran 90/90 clean, yet the weather
//       firmware still crashed on most boots. 7 of 11 old-core crashes hit on
//       the 1st/2nd request after a boot.
//   v2: one round per boot, then ESP.restart(). Reproduced it (crash on the
//       2nd boot) and showed source ports already differ every boot.
//   v3 (this): one factor per phase, ROUNDS boots each, and every crash is
//       tagged with the step the app was in when lwIP's thread asserted
//       (a marker in RTC_NOINIT memory survives the panic reboot):
//         phase 0: plain HTTP (ip-api) only, right after WiFi connects
//         phase 1: HTTPS (Open-Meteo) only, right after WiFi connects
//         phase 2: plain HTTP only, 3 s after WiFi connects
//         phase 3: HTTP then HTTPS, like the weather firmware
//       Result (2026-10-13): HTTP only crashed 28 of 30 boots, every time in the
//       restart AFTER a finished request; HTTPS only 0 of 10. Cause: httpGet()
//       declared HTTPClient before a stack WiFiClient, so the client died first
//       and ~HTTPClient() called stop() on it (end() keeps keep-alive
//       connections and their client pointer): a use-after-free on the heap.
//   v3.1: results live in NVS (namespace "netstress"), saved after every round,
//       because the USB serial link can die mid-run while the firmware keeps
//       going (seen 2026-10-13: board answered pings, USB answered nothing).
//       RESET, USB loss or a power cycle can't lose them. A new build starts a
//       new run (keyed on the build timestamp).
//   v4 (this): A/B of the fix, plain HTTP to ip-api, one request per boot:
//         phase 0: OLD pattern (HTTPClient declared first, reuse on): control
//         phase 1: FIXED ordering (client outlives HTTPClient) + setReuse(false)
//         phase 2: FIXED, HTTP then HTTPS, the weather firmware's real sequence
//         phase 3: FIXED ordering only, reuse left on: is ordering alone enough?
//       Prediction: phase 0 crashes on most boots, phases 1-3 never.
// Build: env `pebl_netstress`. Read results over serial, or from flash with
// tools/boot_log.py --ns netstress (download mode).
// =============================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_system.h>
#include <Preferences.h>
#include "../weather/ca_bundle.h"

static const int ROUNDS = 30;
static const int PHASES = 4;
static const uint32_t MAGIC = 0x5EED5EF4;

// Where the app thread was when the crash hit.
enum Step : uint8_t { S_NONE, S_WIFI, S_WAIT, S_HTTP_GET, S_HTTP_READ, S_HTTP_END, S_HTTP_DONE,
                      S_HTTPS_GET, S_HTTPS_READ, S_HTTPS_END, S_HTTPS_DONE, S_RESTART, S_COUNT };
static const char* STEP_NAMES[S_COUNT] = {"none", "wifi", "wait", "http-get", "http-read", "http-end",
                                          "http-done", "https-get", "https-read", "https-end", "https-done", "restart"};
static const char* PHASE_NAMES[PHASES] = {"OLD http", "FIXED http", "FIXED http+https", "FIXED order only"};

struct State {
    int phase, round;
    int ok[PHASES], empty[PHASES], crashes[PHASES];
    uint8_t crashAt[PHASES][S_COUNT];   // crashes by step
};
static State st;
// The running round's step: RTC_NOINIT survives the panic reboot (and is cheap
// to write); everything else is in NVS.
RTC_NOINIT_ATTR uint32_t stepMagic;
RTC_NOINIT_ATTR uint8_t step;
static void at(Step s) { step = s; stepMagic = MAGIC; }

static const char* BUILD = __DATE__ " " __TIME__;

static void load() {
    Preferences p; p.begin("netstress", false);
    if (p.getString("build", "") != BUILD || p.getBytesLength("state") != sizeof(st)) {
        memset(&st, 0, sizeof(st));       // a new build: start a new run
        p.putString("build", BUILD);
        p.putBytes("state", &st, sizeof(st));
    } else {
        p.getBytes("state", &st, sizeof(st));
    }
    p.end();
}

static void save() {
    char line[200]; int n = 0;
    for (int ph = 0; ph < PHASES; ph++) {
        n += snprintf(line + n, sizeof(line) - n, "%sp%d ok=%d empty=%d crash=%d", ph ? " | " : "",
                      ph, st.ok[ph], st.empty[ph], st.crashes[ph]);
        for (int s = 0; s < S_COUNT && n < (int)sizeof(line) - 16; s++)
            if (st.crashAt[ph][s]) n += snprintf(line + n, sizeof(line) - n, " %s:%u", STEP_NAMES[s], st.crashAt[ph][s]);
    }
    Preferences p; p.begin("netstress", false);
    p.putBytes("state", &st, sizeof(st));
    p.putString("summary", line);         // human-readable copy for tools/boot_log.py --ns netstress
    p.putUInt("round", st.phase * ROUNDS + st.round);
    p.end();
}

// ---- the OLD httpGet() plain path, kept as the control: HTTPClient first ----
static String httpGetOld(const String& url) {
    HTTPClient http;                      // declared first, so destroyed LAST
    String body;
    {
        WiFiClient c;                     // destroyed first: http's destructor then uses it
        if (http.begin(c, url)) {
            http.setTimeout(9000);
            at(S_HTTP_GET);
            int code = http.GET();
            at(S_HTTP_READ);
            if (code == 200) body = http.getString();
            at(S_HTTP_END);
            http.end();                   // keep-alive: socket and _client pointer kept
        }
    }
    at(S_HTTP_DONE);
    return body;                          // ~HTTPClient(): _client->stop() on the dead c
}

// ---- the FIXED pattern, as in src/weather/weather_main.cpp fetch() ----
static String fetch(NetworkClient& client, const String& url, bool reuse, bool secure) {
    HTTPClient http;                      // lives only in here: destroyed before client
    http.setReuse(reuse);
    String body;
    if (http.begin(client, url)) {
        http.setTimeout(9000);
        at(secure ? S_HTTPS_GET : S_HTTP_GET);
        int code = http.GET();
        at(secure ? S_HTTPS_READ : S_HTTP_READ);
        if (code == 200) body = http.getString();
        at(secure ? S_HTTPS_END : S_HTTP_END);
        http.end();
    }
    return body;
}

static String httpGet(const String& url, bool secure, bool reuse) {
    if (secure) {
        static WiFiClientSecure c;
        c.setCACertBundle(ca_bundle_start, ca_bundle_size());
        String body = fetch(c, url, reuse, true);
        c.stop();
        at(S_HTTPS_DONE);
        return body;
    }
    WiFiClient c;
    String body = fetch(c, url, reuse, false);
    at(S_HTTP_DONE);
    return body;
}
// -----------------------------------------------------------------------------

static const char* HTTP_URL  = "http://ip-api.com/json/?fields=status,lat,lon,city";
static const char* HTTPS_URL = "https://api.open-meteo.com/v1/forecast?latitude=39.4895&longitude=-104.8447"
                               "&current=temperature_2m,weather_code&daily=temperature_2m_max,temperature_2m_min"
                               "&temperature_unit=fahrenheit&timezone=auto&forecast_days=1";

static void report() {
    Serial.println("---- results (one round per boot) ----");
    for (int p = 0; p < PHASES; p++) {
        Serial.printf("  %d %-16s ok=%2d empty=%2d crashes=%2d", p, PHASE_NAMES[p], st.ok[p], st.empty[p], st.crashes[p]);
        for (int s = 0; s < S_COUNT; s++)
            if (st.crashAt[p][s]) Serial.printf("  %s:%u", STEP_NAMES[s], st.crashAt[p][s]);
        Serial.println();
    }
}

void setup() {
    Serial.begin(115200);
    delay(800);
    esp_reset_reason_t why = esp_reset_reason();
    bool crashed = why == ESP_RST_PANIC || why == ESP_RST_INT_WDT || why == ESP_RST_TASK_WDT || why == ESP_RST_WDT;
    load();
    if (crashed && stepMagic == MAGIC && st.phase < PHASES) {
        st.crashes[st.phase]++;           // the previous boot crashed at `step`
        if (step < S_COUNT) st.crashAt[st.phase][step]++;
        if (step != S_RESTART) st.round++;   // a crash while restarting already counted its round
    }
    stepMagic = 0;                        // a RESET mid-round is not counted as anything
    if (st.phase < PHASES && st.round >= ROUNDS) { st.phase++; st.round = 0; }
    save();
    Serial.printf("\n=== netstress v3 === reset=%d phase=%d round=%d\n", (int)why, st.phase, st.round);
    report();
    if (st.phase >= PHASES) { Serial.println("DONE"); return; }

    int p = st.phase;
    at(S_WIFI);
    WiFi.mode(WIFI_STA);
    WiFi.begin();
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 25000) delay(200);
    if (WiFi.status() != WL_CONNECTED) { Serial.println("WiFi FAILED"); ESP.restart(); }

    bool ok = true;
    switch (p) {
        case 0: ok = httpGetOld(HTTP_URL).length() > 0; break;
        case 1: ok = httpGet(HTTP_URL, false, false).length() > 0; break;
        case 2: ok = httpGet(HTTP_URL, false, false).length() > 0
                  && httpGet(HTTPS_URL, true, false).length() > 0; break;
        case 3: ok = httpGet(HTTP_URL, false, true).length() > 0; break;
    }
    Serial.printf("phase %d round %d %s (%lums after WiFi start)\n", p, st.round, ok ? "ok" : "EMPTY", millis() - t0);
    if (ok) st.ok[p]++; else st.empty[p]++;
    st.round++;
    save();
    at(S_RESTART);                        // a crash from here on happened during the restart
    Serial.flush();
    ESP.restart();                        // like an OTA trial boot: reconnect immediately
}

void loop() {
    report();
    Serial.println("DONE");
    delay(10000);
}
