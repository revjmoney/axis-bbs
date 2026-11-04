// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")
#include "web.h"

#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "web_roots.h"

namespace web {

namespace {
QueueHandle_t      queue_ = nullptr;
TaskHandle_t       task_ = nullptr;
volatile bool      main_ = true;  // (any board with the settings does the work)
String             discord_, domain_, host_, key_, secret_, lastIp_, lastResult_ = "idle";
SemaphoreHandle_t  lock_ = nullptr;  // the settings, shared with the task
constexpr size_t   kLine = 320;
constexpr uint32_t kDdnsEveryMs = 10 * 60 * 1000;

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

int request(const char* method, const String& url, const String& body, const String& auth, String* reply) {
    WiFiClientSecure tls;
    tls.setCACert(kWebRoots);
    tls.setTimeout(8);
    HTTPClient http;
    if (!http.begin(tls, url)) return -1;
    http.setTimeout(8000);
    if (body.length()) http.addHeader("Content-Type", "application/json");
    if (auth.length()) http.addHeader("Authorization", auth);
    int code = http.sendRequest(method, (uint8_t*)body.c_str(), body.length());
    if (reply && code > 0) *reply = http.getString();
    http.end();
    return code;
}

void ddnsCheck() {
    xSemaphoreTake(lock_, portMAX_DELAY);
    String domain = domain_, host = host_, auth = "sso-key " + key_ + ":" + secret_;
    xSemaphoreGive(lock_);
    if (!domain.length()) return;
    String ip;
    if (request("GET", "https://api.ipify.org", "", "", &ip) != 200 || ip.length() < 7 || ip.length() > 15) {
        lastResult_ = "ddns: couldn't learn the public IP";
        return;
    }
    ip.trim();
    if (ip == lastIp_) return;
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
    for (;;) {
        if (xQueueReceive(queue_, line, pdMS_TO_TICKS(2000)) == pdTRUE) {
            xSemaphoreTake(lock_, portMAX_DELAY);
            String url = discord_;
            xSemaphoreGive(lock_);
            if (main_ && url.length() && WiFi.status() == WL_CONNECTED) {
                int code = request("POST", url, String("{\"content\":\"") + jsonText(line) + "\"}", "", nullptr);
                if (code != 204 && code != 200) Serial.printf("axis: discord post failed (HTTP %d)\n", code);
                vTaskDelay(pdMS_TO_TICKS(1500));  // Discord's rate limit: a few posts a second at most
            }
        }
        if (main_ && WiFi.status() == WL_CONNECTED && (first || millis() - lastDdns > kDdnsEveryMs)) {
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

void begin() {
    if (!lock_) lock_ = xSemaphoreCreateMutex();
    Preferences p;
    p.begin("web", true);
    discord_ = p.getString("discord", "");
    domain_ = p.getString("dd_domain", "");
    host_ = p.getString("dd_host", "");
    key_ = p.getString("dd_key", "");
    secret_ = p.getString("dd_secret", "");
    p.end();
    if (discord_.length() || domain_.length()) startTask();
}

void setMain(bool isMain) { main_ = isMain; }

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

String ping() {
    String ip;
    uint32_t t = millis();
    int code = request("GET", "https://api.ipify.org", "", "", &ip);
    return "HTTP " + String(code) + " " + ip + " in " + String(millis() - t) + " ms, heap " + String(ESP.getFreeHeap() / 1024) + " KB";
}

String status() {
    return String("discord ") + (discord_.length() ? "on" : "off") + ", ddns " + (domain_.length() ? host_ + "." + domain_ : String("off")) +
           ", " + (main_ ? "main" : "not main") + ", last: " + lastResult_;
}

}  // namespace web
