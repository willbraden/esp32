// Over-the-air updates. See ota.h for the design and safety rules.
#include "ota.h"
#include "ota_pubkey.h"
#include "ca_bundle.h"
#include "semver.h"

#include <HTTPClient.h>
#include <NetworkClient.h>
#include <NetworkClientSecure.h>
#include <Update.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <esp_ota_ops.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <mbedtls/base64.h>

// Take over the Arduino core's automatic "mark valid at boot": a new image is
// only confirmed after it has proven it can still fetch and draw (and so can
// still receive the next update). See confirmHealthy().
extern "C" bool verifyRollbackLater() { return true; }

namespace ota {

static const char* NVS_NS        = "pebl-ota";
static const char* KEY_TRY_VER   = "try_ver";    // version being installed
static const char* KEY_TRY_PART  = "try_part";   // partition it was written to
static const char* KEY_BAD_VER   = "bad_ver";    // version that rolled back
static const char* KEY_BAD_CNT   = "bad_cnt";    // how many times it rolled back
static const uint8_t MAX_TRIES   = 3;            // then stop downloading it
static const char* SIGNED_DOMAIN = "pebl-weather-ota/1";
static const size_t MANIFEST_MAX = 4096;
static const uint32_t STALL_MS   = 20000;        // abort a download that stops moving

static bool s_pending = false;

// ---------------------------------------------------------------- versions
bool isNewer(const char* a, const char* b) { return semver::isNewer(a, b); }

// ---------------------------------------------------------------- boot state
static char s_rolledBack[40] = "";

const char* rolledBackFrom() { return s_rolledBack[0] ? s_rolledBack : nullptr; }

bool beginBoot() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    Preferences p;
    p.begin(NVS_NS, false);
    if (p.isKey(KEY_TRY_VER)) {
        String tryVer = p.getString(KEY_TRY_VER, "");
        String tryPart = p.isKey(KEY_TRY_PART) ? p.getString(KEY_TRY_PART, "") : "";
        if (tryPart == running->label) {
            Serial.printf("OTA: first boot of v%s on %s (trial)\n", tryVer.c_str(), running->label);
        } else {
            // The bootloader went back to the old slot: the update rebooted before
            // confirming. Could be a broken release or an unrelated crash, so allow
            // a few attempts before giving up on this version.
            uint8_t n = (p.isKey(KEY_BAD_VER) && p.getString(KEY_BAD_VER, "") == tryVer)
                            ? p.getUChar(KEY_BAD_CNT, 0) + 1 : 1;
            p.putString(KEY_BAD_VER, tryVer);
            p.putUChar(KEY_BAD_CNT, n);
            p.remove(KEY_TRY_VER);
            p.remove(KEY_TRY_PART);
            Serial.printf("OTA: v%s never confirmed, rolled back to v%s on %s (attempt %u/%u)\n",
                          tryVer.c_str(), FW_VERSION, running->label, n, MAX_TRIES);
            snprintf(s_rolledBack, sizeof(s_rolledBack), "%s(attempt-%u/%u)", tryVer.c_str(), n, MAX_TRIES);
        }
    }
    p.end();
    esp_ota_img_states_t st;
    s_pending = esp_ota_get_state_partition(running, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY;
    return s_pending;
}

void confirmHealthy() {
    if (!s_pending) return;
    esp_err_t e = esp_ota_mark_app_valid_cancel_rollback();
    Serial.printf("OTA: v%s confirmed healthy (%s)\n", FW_VERSION, esp_err_to_name(e));
    if (e != ESP_OK) return;
    s_pending = false;
    Preferences p;
    p.begin(NVS_NS, false);
    p.remove(KEY_TRY_VER);
    p.remove(KEY_TRY_PART);
    // An earlier attempt at this version may have rolled back; it has now
    // proven itself, so drop that history.
    if (p.isKey(KEY_BAD_VER) && p.getString(KEY_BAD_VER, "") == FW_VERSION) {
        p.remove(KEY_BAD_VER);
        p.remove(KEY_BAD_CNT);
    }
    p.end();
}

// ---------------------------------------------------------------- network
// GET url, following GitHub's redirect to its asset host. https URLs are
// certificate-checked against the embedded root bundle; plain http is allowed
// for a local staging server (the signature still protects the image).
static int beginGet(HTTPClient& http, NetworkClientSecure& tls, NetworkClient& plain, const String& url) {
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(15000);
    bool ok;
    if (url.startsWith("https://")) {
        tls.setCACertBundle(ca_bundle_start, ca_bundle_size());
        ok = http.begin(tls, url);
    } else {
        ok = http.begin(plain, url);
    }
    if (!ok) return -1;
    return http.GET();
}

// ---------------------------------------------------------------- crypto
static void toHex(const uint8_t* in, size_t n, char* out) {
    static const char* d = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) { out[2 * i] = d[in[i] >> 4]; out[2 * i + 1] = d[in[i] & 15]; }
    out[2 * n] = 0;
}

static bool verifySignature(const String& version, size_t size, const char* shaHex, const char* sigB64) {
    uint8_t sig[128]; size_t sigLen = 0;
    if (mbedtls_base64_decode(sig, sizeof(sig), &sigLen, (const uint8_t*)sigB64, strlen(sigB64)) != 0)
        return false;
    String msg = String(SIGNED_DOMAIN) + "\n" + version + "\n" + String((unsigned long)size) + "\n" + shaHex + "\n";
    uint8_t hash[32];
    mbedtls_sha256((const uint8_t*)msg.c_str(), msg.length(), hash, 0);
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    int rc = mbedtls_pk_parse_public_key(&pk, (const uint8_t*)OTA_PUBKEY_PEM, sizeof(OTA_PUBKEY_PEM));
    if (rc == 0) rc = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, sizeof(hash), sig, sigLen);
    mbedtls_pk_free(&pk);
    return rc == 0;
}

// ---------------------------------------------------------------- update
Result checkAndInstall(void (*onInstalling)(const char* version)) {
    // 1. Manifest
    String body;
    {
        HTTPClient http; NetworkClientSecure tls; NetworkClient plain;
        int code = beginGet(http, tls, plain, OTA_CHANNEL_URL);
        if (code == 404) {
            Serial.println("OTA: no release on this channel yet");
            http.end();
            return Result::NoChannel;
        }
        if (code != 200) {
            Serial.printf("OTA: manifest fetch failed (%d)\n", code);
            http.end();
            return Result::Failed;
        }
        int len = http.getSize();
        if (len > (int)MANIFEST_MAX) { http.end(); Serial.println("OTA: manifest too large"); return Result::Failed; }
        body = http.getString();
        http.end();
    }
    JsonDocument m;
    if (deserializeJson(m, body) || (m["format"] | 0) != 1) {
        Serial.println("OTA: manifest unreadable");
        return Result::Failed;
    }
    String version = m["version"] | "";
    String url     = m["url"] | "";
    size_t size    = m["size"] | 0;
    String shaWant = m["sha256"] | "";
    String sigB64  = m["sig"] | "";
    if (!version.length() || !url.length() || !size || shaWant.length() != 64 || !sigB64.length()) {
        Serial.println("OTA: manifest missing fields");
        return Result::Failed;
    }
    if (!isNewer(version.c_str(), FW_VERSION)) {
        Serial.printf("OTA: up to date (running v%s, channel v%s)\n", FW_VERSION, version.c_str());
        return Result::UpToDate;
    }
    {
        Preferences p; p.begin(NVS_NS, false);
        bool bad = p.isKey(KEY_BAD_VER) && p.getString(KEY_BAD_VER, "") == version
                   && p.getUChar(KEY_BAD_CNT, 0) >= MAX_TRIES;
        p.end();
        if (bad) {
            Serial.printf("OTA: skipping v%s (rolled back %u times)\n", version.c_str(), MAX_TRIES);
            return Result::SkippedBad;
        }
    }
    const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
    if (!target || size > target->size) {
        Serial.printf("OTA: v%s does not fit the update slot\n", version.c_str());
        return Result::Failed;
    }

    // 2. Download into the spare slot, hashing as we go
    Serial.printf("OTA: installing v%s over v%s (%u bytes) into %s\n",
                  version.c_str(), FW_VERSION, (unsigned)size, target->label);
    if (onInstalling) onInstalling(version.c_str());

    HTTPClient http; NetworkClientSecure tls; NetworkClient plain;
    int code = beginGet(http, tls, plain, url);
    if (code != 200 || (http.getSize() >= 0 && (size_t)http.getSize() != size)) {
        Serial.printf("OTA: download failed (%d, %d bytes)\n", code, http.getSize());
        http.end();
        return Result::Failed;
    }
    if (!Update.begin(size)) {
        Serial.printf("OTA: Update.begin: %s\n", Update.errorString());
        http.end();
        return Result::Failed;
    }
    NetworkClient* stream = http.getStreamPtr();
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    static uint8_t buf[4096];
    size_t got = 0;
    uint32_t lastData = millis();
    bool ok = true;
    while (got < size) {
        int avail = stream->available();
        if (avail > 0) {
            size_t want = min((size_t)avail, min(sizeof(buf), size - got));
            int n = stream->read(buf, want);
            if (n <= 0) continue;
            mbedtls_sha256_update(&sha, buf, n);
            if (Update.write(buf, n) != (size_t)n) { Serial.printf("OTA: write: %s\n", Update.errorString()); ok = false; break; }
            got += n;
            lastData = millis();
        } else if (millis() - lastData > STALL_MS) {
            Serial.printf("OTA: download stalled at %u/%u\n", (unsigned)got, (unsigned)size);
            ok = false; break;
        } else {
            delay(2);
        }
    }
    uint8_t digest[32];
    mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);
    http.end();
    char shaHex[65];
    toHex(digest, sizeof(digest), shaHex);

    // 3. Verify before the boot partition can change
    if (ok && shaWant != shaHex) { Serial.println("OTA: sha256 mismatch"); ok = false; }
    if (ok && !verifySignature(version, size, shaHex, sigB64.c_str())) {
        Serial.println("OTA: signature INVALID, rejecting image");
        ok = false;
    }
    if (!ok) {
        Update.abort();
        return Result::Failed;
    }

    // 4. Commit: remember the attempt so a rollback can be detected, then boot it
    Preferences p;
    p.begin(NVS_NS, false);
    p.putString(KEY_TRY_VER, version);
    p.putString(KEY_TRY_PART, target->label);
    p.end();
    if (!Update.end(true)) {
        Serial.printf("OTA: Update.end: %s\n", Update.errorString());
        p.begin(NVS_NS, false); p.remove(KEY_TRY_VER); p.remove(KEY_TRY_PART); p.end();
        return Result::Failed;
    }
    Serial.printf("OTA: v%s verified and installed, rebooting\n", version.c_str());
    Serial.flush();
    delay(200);
    ESP.restart();
    return Result::Failed;  // not reached
}

}  // namespace ota
