// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")
#include "web.h"

#include <HTTPClient.h>
#include <atomic>
#include <base64.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "bbs/noip.h"
#include "bbs/webwork.h"
#include "heapscope.h"
#include "web_roots.h"

namespace web {

namespace {
QueueHandle_t      queue_ = nullptr;
std::atomic<bool>  running_{false};  // a web task exists (set by whoever starts it, cleared by the task as it ends)
volatile bool      main_ = true;  // (any board with the settings does the work)
String             discord_, domain_, host_, key_, secret_, lastIp_, lastResult_ = "idle";
String             noipHost_, noipUser_, noipPass_, noipIp_, noipResult_ = "idle";
volatile bool      noipStopped_ = false;  // No-IP refused: wait for new settings (it bans clients that keep trying)
volatile bool      busy_ = false;    // inside request()
volatile bool      door_ = false;    // a door is using the heap: do not start a handshake
SemaphoreHandle_t  lock_ = nullptr;  // the settings, shared with the task
constexpr size_t   kLine = 320;
constexpr uint32_t kStack = 7168;              // TLS wants a deep stack (about 1 KB spare at its peak)
constexpr size_t   kTlsNeeds = 48 * 1024;      // a TLS handshake wants about 45 KB in one piece
constexpr size_t   kTlsReserve = 32 * 1024;    // and a callers' worth of heap must stay free besides
std::atomic<uint32_t> dropped_{0}, gated_{0};  // Discord lines dropped / DNS checks held for lack of memory
volatile uint32_t     gatedLargest_ = 0;       // the largest block the last held DNS check saw
std::atomic<uint32_t> startFails_{0}, stackMark_{0};  // task starts refused for lack of memory / lowest stack mark seen
volatile uint32_t     startLargest_ = 0;       // the largest block the last refused start saw
std::atomic<bool>     checkPending_{false};    // a DNS check handed to the task (the loop's tick keeps the timer)

// a busy CYD main can't spare a TLS handshake: trying anyway starves the whole board.
// A running door wins even when the largest block still looks big enough: the handshake
// would take that block and the next guest page would fail.
bool tlsFits() { return !door_ && heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) >= kTlsNeeds + kTlsReserve; }

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
    // No-IP's own checker answers over plain HTTP: the answer is only our public IP, no secret travels
    if (request("GET", "http://ip1.dynupdate.no-ip.com/", "", "", &ip) != 200) return "";
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
    if (!tlsFits()) {  // the password never goes over plain HTTP: wait for memory, retry next cycle
        gatedLargest_ = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
        gated_++;
        noipResult_ = "noip: waiting for memory";
        return;
    }
    int    code = request("GET", url, "", auth, &reply,
                          "AxisMicroBBS/0.3 therealrevjmoney@gmail.com");
    auto   r = axis::bbs::noipReply(reply.c_str());
    noipResult_ = "noip: " + host + " -> " + ip + " " + (r.ok ? "ok" : "FAILED") + " (" + "HTTPS " + String(code) + " " +
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
    if (!tlsFits()) {
        gatedLargest_ = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
        gated_++;
        lastResult_ = "ddns: waiting for memory";
        return;
    }
    int code = request("PUT", "https://api.godaddy.com/v1/domains/" + domain + "/records/A/" + host,
                       "[{\"data\":\"" + ip + "\",\"ttl\":600}]", auth, nullptr);
    lastResult_ = "ddns: " + host + "." + domain + " -> " + ip + (code == 200 ? " ok" : " FAILED (HTTP " + String(code) + ")");
    Serial.printf("axis: %s\n", lastResult_.c_str());
    if (code == 200) lastIp_ = ip;
}

volatile bool ddnsDue_ = false;  // bench: @DDNS now makes the next tick check at once
bool          first_ = true;     // the timer: kept by tick() on the loop (core 1) only
uint32_t      lastDdns_ = 0, nextTick_ = 0, failedAt_ = 0;
bool          failed_ = false;  // the last start was refused: wait 2 s (loop core only, like the timer)

uint32_t queued() { return queue_ ? (uint32_t)uxQueueMessagesWaiting(queue_) : 0; }

bool hasWork() { return axis::bbs::webHasWork(door_, checkPending_, queued()); }

// Discord lines, oldest first. A line leaves the queue only once posted or dropped, so a door can start
// at any point and the line just waits in the queue for it (the task ends meanwhile).
void postLines() {
    char line[kLine];
    while (queue_ && !door_ && xQueuePeek(queue_, line, 0) == pdTRUE) {
        xSemaphoreTake(lock_, portMAX_DELAY);
        String url = discord_;
        xSemaphoreGive(lock_);
        // A door can start between the check above and this one: keep the line if it did.
        if (main_ && url.length() && WiFi.status() == WL_CONNECTED && tlsFits()) {
            int code = request("POST", url, String("{\"content\":\"") + jsonText(line) + "\"}", "", nullptr);
            if (code == -2 && door_) return;  // the handshake refused because a door started
            xQueueReceive(queue_, line, 0);
            if (code != 204 && code != 200) Serial.printf("axis: discord post failed (HTTP %d)\n", code);
            vTaskDelay(pdMS_TO_TICKS(1500));  // Discord's rate limit: a few posts a second at most
        } else if (door_) {
            return;
        } else {
            if (main_ && url.length() && WiFi.status() == WL_CONNECTED && !door_) dropped_++;  // TLS did not fit (not a door)
            xQueueReceive(queue_, line, 0);  // no webhook, or the heap cannot spare TLS: drop it, as before
        }
    }
}

// Each pass is its own call, so nothing it allocated (TLS client, HTTP client, Strings) is alive when the task ends.
void run(void*) {
    for (;;) {
        postLines();
        if (!door_ && checkPending_) {  // DNS checks allocate too: during a door the check stays pending for after it
            checkPending_ = false;
            ddnsCheck();
        }
        uint32_t mark = uxTaskGetStackHighWaterMark(nullptr), was = stackMark_;
        if (!was || mark < was) stackMark_ = mark;
        // Hand over before the last look: work queued after this store finds no task and starts a new one;
        // work queued before it is seen here, and this task keeps going if it wins the flag back.
        running_ = false;
        bool idle = false;
        if (!hasWork() || !running_.compare_exchange_strong(idle, true)) break;
    }
    vTaskDelete(nullptr);  // the idle task on core 0 frees the stack and control block
}

// Start the task unless one is running. Callers publish their work first (queue the line, set the check).
void kick() {
    if (failed_ && millis() - failedAt_ < 2000) return;  // a start just failed for lack of memory
    bool idle = false;
    if (!running_.compare_exchange_strong(idle, true)) return;
    // A stack this size must fit in one piece: look first, so a doomed start costs no failed allocation.
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    if (largest >= kStack + 512 && xTaskCreatePinnedToCore(run, "web", kStack, nullptr, 1, nullptr, 0) == pdPASS) {
        failed_ = false;
        return;
    }
    running_ = false;  // nothing is lost: the line stays queued, the check stays pending, a later tick tries again
    startLargest_ = largest;
    startFails_++;
    failed_ = true;
    failedAt_ = millis();
}

void ensureQueue() {
    if (!queue_) queue_ = xQueueCreate(8, kLine);
}
}  // namespace

void tick() {
    uint32_t now = millis();
    if ((int32_t)(now - nextTick_) < 0) return;
    nextTick_ = now + 1000;
    if (checkPending_) lastDdns_ = now;  // (a held check, e.g. during a door: the 10 minutes count from when it really runs)
    bool dns = domain_.length() || noipHost_.length();  // (the settings change only on this core)
    if (main_ && !door_ && !checkPending_ && (dns || discord_.length()) && WiFi.status() == WL_CONNECTED &&
        axis::bbs::webCheckDue(now, first_, ddnsDue_, lastDdns_)) {
        first_ = false;
        ddnsDue_ = false;
        lastDdns_ = now;
        if (dns) checkPending_ = true;  // (with only Discord set the check had nothing to do, as before)
    }
    if (hasWork()) kick();
}

uint32_t stackMark() { return stackMark_; }
uint32_t startFails() { return startFails_; }
uint32_t startLargestKb() { return startLargest_ / 1024; }

bool busy() { return busy_; }

uint32_t droppedLines() { return dropped_; }
uint32_t gatedChecks() { return gated_; }
uint32_t gatedLargestKb() { return gatedLargest_ / 1024; }

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
    if (discord_.length() || domain_.length() || noipHost_.length()) ensureQueue();
}

void setMain(bool isMain) { main_ = isMain; }

void setDoorRunning(bool on) { door_ = on; }

void announce(const String& line) {
    if (!queue_ || !discord_.length() || !main_ || !line.length()) return;
    char b[kLine];
    strlcpy(b, line.c_str(), sizeof b);
    xQueueSend(queue_, b, 0);
    if (hasWork()) kick();  // (during a door the line waits in the queue; the tick starts the task after it)
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
    if (url.length()) ensureQueue();
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
    if (f[0].length()) ensureQueue();
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
    if (host.length()) ensureQueue();
    return true;
}

void ddnsSoon() {
    ddnsDue_ = true;
    nextTick_ = millis();  // the very next tick hands the check over
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
