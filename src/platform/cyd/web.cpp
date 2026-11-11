// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")
#include "web.h"

#include <HTTPClient.h>
#include <base64.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "bbs/noip.h"
#include "heapscope.h"
#include "web_roots.h"

namespace web {

namespace {
QueueHandle_t      queue_ = nullptr;
TaskHandle_t       task_ = nullptr;
volatile bool      main_ = true;  // (any board with the settings does the work)
String             discord_, domain_, host_, key_, secret_, lastIp_, lastResult_ = "idle";
String             noipHost_, noipUser_, noipPass_, noipIp_, noipResult_ = "idle";
volatile bool      noipStopped_ = false;  // No-IP refused: wait for new settings (it bans clients that keep trying)
volatile bool      busy_ = false;    // inside request()
volatile bool      door_ = false;    // a door is using the heap: do not start a handshake
SemaphoreHandle_t  lock_ = nullptr;  // the settings, shared with the task
constexpr size_t   kLine = 320;
constexpr uint32_t kDdnsEveryMs = 10 * 60 * 1000;
constexpr uint32_t kFirstCheckMs = 60 * 1000;  // after boot: time to clear bad settings before any network call
constexpr size_t   kTlsNeeds = 48 * 1024;      // a TLS handshake wants about 45 KB in one piece

// a busy CYD main can't spare a TLS handshake: trying anyway starves the whole board.
// A running door wins even when the largest block still looks big enough: the handshake
// would take that block and the next guest page would fail.
bool tlsFits() { return !door_ && heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) >= kTlsNeeds; }

String jsonText(const char* s) {  // a JSON string's inside
    String o;
    for (; *s; s++) {
        char c = *s;
        if (c == '"' || c == '\\') o += '\\', o += c;
        else if ((uint8_t)c < 0x20) o += ' ';
        else o += c;
    }
    return o;
}

int requestInner(const char* method, const String& url, const String& body, const String& auth, String* reply,
                 const char* agent) {
    bool secure = url.startsWith("https://");
    if (secure && !tlsFits()) return -2;  // not enough memory for HTTPS right now
    WiFiClientSecure tls;
    WiFiClient       plain;
    HTTPClient       http;
    if (secure) {
        tls.setCACert(kWebRoots);
        tls.setTimeout(8);
        if (!http.begin(tls, url)) return -1;
    } else if (!http.begin(plain, url)) return -1;
    http.setTimeout(8000);
    if (body.length()) http.addHeader("Content-Type", "application/json");
    if (auth.length()) http.addHeader("Authorization", auth);
    if (agent) http.setUserAgent(agent);
    int code = http.sendRequest(method, (uint8_t*)body.c_str(), body.length());
    if (reply && code > 0) *reply = http.getString();
    http.end();
    return code;
}

int request(const char* method, const String& url, const String& body, const String& auth, String* reply,
            const char* agent = nullptr) {
    HeapScope scope("web");
    busy_ = true;
    int code = requestInner(method, url, body, auth, reply, agent);
    busy_ = false;
    return code;
}

String publicIp() {
    String ip;
    // No-IP's own checker answers over plain HTTP, for a board without room for HTTPS
    if (request("GET", tlsFits() ? "https://api.ipify.org" : "http://ip1.dynupdate.no-ip.com/", "", "", &ip) != 200) return "";
    ip.trim();
    return ip.length() >= 7 && ip.length() <= 15 ? ip : String();
}

// No-IP: one GET with the account (or DDNS key) login; "all.ddnskey.com" updates every host on a key
void noipCheck(const String& ip) {
    xSemaphoreTake(lock_, portMAX_DELAY);
    String host = noipHost_, auth = "Basic " + base64::encode(noipUser_ + ":" + noipPass_);
    xSemaphoreGive(lock_);
    if (!host.length() || noipStopped_ || ip == noipIp_) return;
    String reply, url = axis::bbs::noipUrl(host.c_str(), ip.c_str()).c_str();
    bool   secure = tlsFits();
    if (!secure) url.replace("https://", "http://");  // No-IP accepts plain HTTP; HTTPS only when it fits
    int    code = request("GET", url, "", auth, &reply,
                          "AxisMicroBBS/0.3 therealrevjmoney@gmail.com");
    auto   r = axis::bbs::noipReply(reply.c_str());
    noipResult_ = "noip: " + host + " -> " + ip + " " + (r.ok ? "ok" : "FAILED") + " (" + (secure ? "HTTPS " : "HTTP ") + String(code) + " " +
                  r.text.c_str() + ")";
    if (r.stop) noipStopped_ = true, noipResult_ += ", stopped until @NOIP is set again";
    Serial.printf("axis: %s\n", noipResult_.c_str());
    if (r.ok) noipIp_ = ip;
}

void ddnsCheck() {
    xSemaphoreTake(lock_, portMAX_DELAY);
    String domain = domain_, host = host_, auth = "sso-key " + key_ + ":" + secret_, noip = noipHost_;
    xSemaphoreGive(lock_);
    if (!domain.length() && !noip.length()) return;
    String ip = publicIp();
    if (!ip.length()) {
        lastResult_ = "ddns: couldn't learn the public IP";
        return;
    }
    noipCheck(ip);
    if (!domain.length() || ip == lastIp_) return;
    int code = request("PUT", "https://api.godaddy.com/v1/domains/" + domain + "/records/A/" + host,
                       "[{\"data\":\"" + ip + "\",\"ttl\":600}]", auth, nullptr);
    lastResult_ = "ddns: " + host + "." + domain + " -> " + ip + (code == 200 ? " ok" : " FAILED (HTTP " + String(code) + ")");
    Serial.printf("axis: %s\n", lastResult_.c_str());
    if (code == 200) lastIp_ = ip;
}

void run(void*) {
    uint32_t lastDdns = 0;
    bool     first = true;
    char     line[kLine];
    bool     holding = false;  // one Discord line kept across a door, instead of dropped
    for (;;) {
        if (!holding && xQueueReceive(queue_, line, pdMS_TO_TICKS(door_ ? 500 : 2000)) == pdTRUE) holding = true;
        if (holding && door_) {
            vTaskDelay(pdMS_TO_TICKS(500));  // the door has the heap: post this line when it returns
        } else if (holding) {
            xSemaphoreTake(lock_, portMAX_DELAY);
            String url = discord_;
            xSemaphoreGive(lock_);
            // A door can start between the check above and this one: keep the line if it did.
            if (main_ && url.length() && WiFi.status() == WL_CONNECTED && tlsFits()) {
                holding = false;
                int code = request("POST", url, String("{\"content\":\"") + jsonText(line) + "\"}", "", nullptr);
                if (code == -2 && door_) holding = true;  // the handshake refused because a door started
                else if (code != 204 && code != 200) Serial.printf("axis: discord post failed (HTTP %d)\n", code);
                if (!holding) vTaskDelay(pdMS_TO_TICKS(1500));  // Discord's rate limit: a few posts a second at most
            } else if (door_) {
                vTaskDelay(pdMS_TO_TICKS(500));
            } else {
                holding = false;  // no webhook, or the heap cannot spare TLS: drop it, as before
            }
        }
        if (door_) continue;  // DNS checks allocate too; the timer is not consumed, so they run after
        if (main_ && WiFi.status() == WL_CONNECTED && millis() > kFirstCheckMs && (first || millis() - lastDdns > kDdnsEveryMs)) {
            first = false;
            lastDdns = millis();
            ddnsCheck();
        }
    }
}

void startTask() {
    if (task_) return;
    if (!queue_) queue_ = xQueueCreate(8, kLine);
    xTaskCreatePinnedToCore(run, "web", 7168, nullptr, 1, &task_, 0);  // TLS wants a deep stack; core 0 with WiFi
}
}  // namespace

bool busy() { return busy_; }

void begin() {
    if (!lock_) lock_ = xSemaphoreCreateMutex();
    Preferences p;
    p.begin("web", true);
    discord_ = p.getString("discord", "");
    domain_ = p.getString("dd_domain", "");
    host_ = p.getString("dd_host", "");
    key_ = p.getString("dd_key", "");
    secret_ = p.getString("dd_secret", "");
    noipHost_ = p.getString("noip_host", "");
    noipUser_ = p.getString("noip_user", "");
    noipPass_ = p.getString("noip_pass", "");
    p.end();
    if (discord_.length() || domain_.length() || noipHost_.length()) startTask();
}

void setMain(bool isMain) { main_ = isMain; }

void setDoorRunning(bool on) { door_ = on; }

void announce(const String& line) {
    if (!queue_ || !discord_.length() || !main_ || !line.length()) return;
    char b[kLine];
    strlcpy(b, line.c_str(), sizeof b);
    xQueueSend(queue_, b, 0);
}

bool setDiscord(const String& url) {
    if (url.length() && !url.startsWith("https://discord.com/api/webhooks/") && !url.startsWith("https://discordapp.com/api/webhooks/"))
        return false;
    Preferences p;
    p.begin("web", false);
    p.putString("discord", url);
    p.end();
    xSemaphoreTake(lock_, portMAX_DELAY);
    discord_ = url;
    xSemaphoreGive(lock_);
    if (url.length()) startTask();
    return true;
}

bool setDdns(const String& spec) {
    String f[4];
    int    from = 0;
    for (int i = 0; i < 4; i++) {
        int bar = i < 3 ? spec.indexOf('|', from) : -1;
        f[i] = bar < 0 ? spec.substring(from) : spec.substring(from, bar);
        if (bar < 0 && i < 3) {
            if (spec.length()) return false;
            break;
        }
        from = bar + 1;
    }
    Preferences p;
    p.begin("web", false);
    p.putString("dd_domain", f[0]);
    p.putString("dd_host", f[1]);
    p.putString("dd_key", f[2]);
    p.putString("dd_secret", f[3]);
    p.end();
    xSemaphoreTake(lock_, portMAX_DELAY);
    domain_ = f[0], host_ = f[1], key_ = f[2], secret_ = f[3];
    lastIp_ = "";
    xSemaphoreGive(lock_);
    if (f[0].length()) startTask();
    return true;
}

bool setNoip(const String& spec) {
    int a = spec.indexOf('|'), b = a < 0 ? -1 : spec.indexOf('|', a + 1);
    if (spec.length() && b < 0) return false;
    String host = spec.length() ? spec.substring(0, a) : String(), user = spec.length() ? spec.substring(a + 1, b) : String(),
           pass = spec.length() ? spec.substring(b + 1) : String();
    host.trim(), user.trim();
    Preferences p;
    p.begin("web", false);
    p.putString("noip_host", host);
    p.putString("noip_user", user);
    p.putString("noip_pass", pass);
    p.end();
    xSemaphoreTake(lock_, portMAX_DELAY);
    noipHost_ = host, noipUser_ = user, noipPass_ = pass, noipIp_ = "";
    noipStopped_ = false;
    noipResult_ = host.length() ? "waiting for the first check" : "idle";
    xSemaphoreGive(lock_);
    if (host.length()) startTask();
    return true;
}

void noipNow() {
    xSemaphoreTake(lock_, portMAX_DELAY);
    noipIp_ = "";
    noipStopped_ = false;
    xSemaphoreGive(lock_);
    String ip = publicIp();
    if (ip.length()) noipCheck(ip);
    else noipResult_ = "noip: couldn't learn the public IP";
}

String ping() {
    String ip;
    uint32_t t = millis();
    int code = request("GET", "https://api.ipify.org", "", "", &ip);
    return "HTTP " + String(code) + " " + ip + " in " + String(millis() - t) + " ms, heap " + String(ESP.getFreeHeap() / 1024) + " KB";
}

String status() {
    return String("discord ") + (discord_.length() ? "on" : "off") + ", ddns " + (domain_.length() ? host_ + "." + domain_ : String("off")) +
           ", noip " + (noipHost_.length() ? noipHost_ + " as " + noipUser_ : String("off")) +
           ", " + (main_ ? "main" : "not main") + ", last: " + lastResult_ + ", " + noipResult_;
}

}  // namespace web
