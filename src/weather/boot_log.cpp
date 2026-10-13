#include "boot_log.h"
#include "ota.h"            // FW_VERSION
#include <Preferences.h>
#include <esp_system.h>
#include <esp_sleep.h>
#include <stdarg.h>

// NVS wear: ~10 short string writes per boot at 48 boots/day spread over the
// 20 KB nvs partition is a few page erases a day, decades of flash life.

namespace bootlog {

static const char*   NS       = "pebl-boot";
static const uint8_t LOG_SIZE = 16;  // archived boots kept ("h0".."h15")
static const uint8_t SHOW     = 4;   // printed at boot

static struct {
    bool     started;
    uint32_t seq;
    bool     trial, longPress;
    const char* wake;
    char     rollback[40];
    char     path[72];   // stages passed, in order: boot>wifi>loc>...
    char     fail[24];
    char     ota[32];
    char     notes[192]; // comma-separated: retries, faults, trial failures, rollback
    int32_t  wifiMs;
} s = {};

static const char* resetName(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON:   return "power-on";
        case ESP_RST_EXT:       return "ext-reset";
        case ESP_RST_SW:        return "sw-restart";
        case ESP_RST_PANIC:     return "PANIC";
        case ESP_RST_INT_WDT:   return "INT-WDT";
        case ESP_RST_TASK_WDT:  return "TASK-WDT";
        case ESP_RST_WDT:       return "WDT";
        case ESP_RST_DEEPSLEEP: return "deep-sleep";
        case ESP_RST_BROWNOUT:  return "BROWNOUT";
        case ESP_RST_USB:       return "usb-reset";
        case ESP_RST_JTAG:      return "jtag-reset";
        default:                return "unknown";
    }
}

static const char* wakeName(esp_sleep_wakeup_cause_t w) {
    switch (w) {
        case ESP_SLEEP_WAKEUP_TIMER: return "timer";
        case ESP_SLEEP_WAKEUP_EXT0:  return "button";
        default:                     return "reset";
    }
}

// Bounded append: stops cleanly at the end of the buffer instead of overrunning.
static void add(char* buf, size_t size, size_t& n, const char* fmt, ...) {
    if (n >= size - 1) return;
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(buf + n, size - n, fmt, ap);
    va_end(ap);
    if (w > 0) n = (n + w < size) ? n + w : size - 1;
}

static void write() {
    if (!s.started) return;
    char line[512];   // worst case is ~440 chars
    size_t n = 0;
    add(line, sizeof(line), n, "#%lu v%s wake=%s path=%s up=%.1fs",
        (unsigned long)s.seq, FW_VERSION, s.wake, s.path, millis() / 1000.0);
    if (s.wifiMs >= 0)  add(line, sizeof(line), n, " wifi=%ldms", (long)s.wifiMs);
    if (s.fail[0])      add(line, sizeof(line), n, " fail=%s", s.fail);
    if (s.ota[0])       add(line, sizeof(line), n, " ota=%s", s.ota);
    if (s.trial)        add(line, sizeof(line), n, " trial");
    if (s.longPress)    add(line, sizeof(line), n, " long-press");
    if (s.notes[0])     add(line, sizeof(line), n, " notes=%s", s.notes);
    if (s.rollback[0])  add(line, sizeof(line), n, " rolled-back-from=%s", s.rollback);
    Preferences p;
    p.begin(NS, false);
    p.putString("cur", line);
    p.end();
}

void begin(bool trial, const char* rollbackFrom) {
    Preferences p;
    p.begin(NS, false);
    uint32_t seq = (p.isKey("seq") ? p.getUInt("seq", 0) : 0) + 1;
    p.putUInt("seq", seq);

    // Archive the previous boot. This boot's reset reason is what started it
    // (deep-sleep wake, PANIC, sw-restart, power-on...); the previous record's
    // path says how far that boot got before that happened.
    if (p.isKey("cur")) {
        String prev = p.getString("cur", "");
        prev += " next-boot=";
        prev += resetName(esp_reset_reason());
        unsigned long prevSeq = prev.startsWith("#") ? strtoul(prev.c_str() + 1, nullptr, 10) : 0;
        char key[5];
        snprintf(key, sizeof(key), "h%u", (unsigned)(prevSeq % LOG_SIZE));
        p.putString(key, prev);
        p.remove("cur");
    }
    for (uint32_t k = seq > SHOW ? seq - SHOW : 1; k < seq; k++) {
        char key[5], tag[16];
        snprintf(key, sizeof(key), "h%u", (unsigned)(k % LOG_SIZE));
        snprintf(tag, sizeof(tag), "#%lu ", (unsigned long)k);
        if (!p.isKey(key)) continue;
        String r = p.getString(key, "");
        if (r.startsWith(tag)) Serial.printf("Boot log: %s\n", r.c_str());   // skip stale slots
    }
    p.end();

    s.started = true;
    s.seq = seq;
    s.trial = trial;
    s.wake = wakeName(esp_sleep_get_wakeup_cause());
    strlcpy(s.rollback, rollbackFrom ? rollbackFrom : "", sizeof(s.rollback));
    strlcpy(s.path, "boot", sizeof(s.path));
    s.wifiMs = -1;
    write();
}

void stage(const char* name) {
    strlcat(s.path, ">", sizeof(s.path));
    strlcat(s.path, name, sizeof(s.path));
    write();
}
void fail(const char* what)     { strlcpy(s.fail, what, sizeof(s.fail)); write(); }
void ota(const char* result)    { strlcpy(s.ota, result, sizeof(s.ota)); write(); }
void wifiMs(uint32_t ms)        { s.wifiMs = (int32_t)ms; write(); }
void longPress()                { s.longPress = true; write(); }
void note(const char* what) {
    if (s.notes[0]) strlcat(s.notes, ",", sizeof(s.notes));
    strlcat(s.notes, what, sizeof(s.notes));
    write();
}

}  // namespace bootlog
