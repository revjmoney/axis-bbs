// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Once-a-day check for a newer axispc release (GitHub tags pc-v*, not the ESP32 v* tags).
// The comparison, the release list, the cache file and the 24 hour gate are pure and have no
// network in them. The Linux program asks GitHub on a side thread; the firmware never calls this.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace axis::pcupdate {

constexpr const char* kBlink = "*NEW*";  // fits the 5-column version field on the CYD waiting screen
constexpr const char* kDefaultRepo = "revjmoney/axis-bbs";
constexpr int64_t     kCheckInterval = 86400;  // one day, unix seconds

struct Version {
    int  n = 0;
    int  part[4] = {};
    bool ok = false;
};

// "0.53" or "0.53.1". Not a tag: no "v" or "pc-v" prefix, no suffix.
Version parseVersion(std::string_view text);
int     cmpVersion(const Version& a, const Version& b);  // -1, 0, 1; an unparsed version is older

enum class TagKind { Other, Board, Pc };  // v0.61 is a board tag; pc-v0.54 is a Linux release

struct Tag {
    TagKind kind = TagKind::Other;
    Version version;
};
Tag parseTag(std::string_view tag);

struct Release {
    std::string tag, name, body, url;
    bool        draft = false, prerelease = false;
};

struct ReleaseList {
    bool                 ok = false;  // false: the body was not a release list (malformed, cut off, not JSON)
    std::vector<Release> releases;
};

// A GitHub releases JSON array. Hostile or broken input comes back ok=false and releases empty. Never throws.
ReleaseList parseReleaseList(std::string_view json);

struct Pick {
    bool        found = false;
    bool        newer = false;  // a pc-v release strictly newer than `current`
    std::string version;        // "0.54", without the pc-v prefix
    std::string tag;
    std::string summary;  // one short line
    std::string notes;    // a few lines of the release text
    std::string url;
};

// The highest non-draft, non-prerelease pc-v tag. Board tags (v*) are ignored. `repo` is only used
// when a release has no safe https://github.com/ URL of its own.
Pick pickPcRelease(const ReleaseList& list, std::string_view current, std::string_view repo);

struct Cache {
    int64_t     checked = 0;  // unix seconds of the last finished attempt, 0 when there has not been one
    std::string latest;
    std::string summary;
    std::string notes;
    std::string url;
    bool        newer = false;
};

std::string writeCache(const Cache& c);
Cache       readCache(std::string_view text);  // a broken file is an empty cache, so the next start may check

// True when there is no usable stamp, the clock went backwards, or `interval` seconds have passed.
bool due(int64_t checked, int64_t now, int64_t interval = kCheckInterval);

enum class FetchKind { Ok, NoAnswer, NoTls, BadHttp, BadBody, RateLimit };

struct Applied {
    Cache cache;
    bool  parsed = false;    // the body was a release list
    bool  remember = true;   // false for NoTls: do not stamp the day, so a later start can try again
};

// A finished attempt. Failures keep the previous latest/notes and only move `checked`, except NoTls,
// which leaves the cache untouched. A parsed list replaces latest/notes, including clearing them.
Applied applyFetch(const Cache& prev, int64_t now, std::string_view current, std::string_view repo, FetchKind kind,
                   int httpCode, std::string_view body);

struct HttpMessage {
    bool        headersOk = false;
    bool        bodyOk = false;
    int         code = 0;
    std::string body;
};

// One HTTP/1.x response (Content-Length or chunked). A cut-off or garbage message does not throw.
HttpMessage parseHttp(std::string_view raw);
FetchKind   classifyHttp(const HttpMessage& msg);

// The request the checker sends. A version with anything but digits and dots is not put on the User-Agent.
std::string buildRequest(std::string_view repo, std::string_view version);

// What the sysop reads (System, Update, or V on the waiting screen). No network.
std::string formatReport(const Cache& c, std::string_view current, bool enabled);

// The waiting screen blinks only while the check is on and a newer release is cached.
bool blinks(bool enabled, bool newer);

struct View {
    bool        alert = false;  // blink the version on the waiting screen
    std::string url;
    std::string report;
};

// Linux program only. start() reads the cache and returns. poll() is called every loop turn and does
// not block: the request runs on a side thread. stop() asks that thread to quit and does not wait long.
void        start(const std::string& dataDir, bool enabled, const std::string& repo, const std::string& currentVersion);
void        poll();
void        stop();
View        view();
std::string report();
bool        enabled();
// "on" / "off" (or 1 / 0) saves update_check in axis.conf and takes effect at once. "state" is "on" or "off".
// Anything else, including empty, is the report. A save that fails leaves the switch as it was.
std::string command(std::string_view arg);

}  // namespace axis::pcupdate
