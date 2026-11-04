// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bbs/setupportal.h"

namespace axis::bbs {

std::string htmlEscape(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            case '\'': o += "&#39;"; break;
            default: o += c;
        }
    }
    return o;
}

std::string portalPage(const std::vector<std::string>& ssids, const std::string& message) {
    std::string p =
        "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width\">"
        "<title>AXIS-SETUP</title><style>body{background:#000;color:#ccc;font-family:monospace;margin:16px}"
        "h1{color:#f80}input,select,button{font:inherit;padding:6px;margin:4px 0;width:100%;max-width:360px}"
        "button{background:#f80;color:#000;border:0}</style></head><body>"
        "<h1>AXIS-SETUP</h1><p>Axis Micro BBS Firmware: headless node. Pick the WiFi network this node should join.</p>";
    if (!message.empty()) p += "<p style=\"color:#f80\">" + htmlEscape(message) + "</p>";
    p += "<form method=\"post\" action=\"/join\"><label>Network<br><input name=\"ssid\" list=\"nets\" autocomplete=\"off\"></label>"
         "<datalist id=\"nets\">";
    for (const std::string& s : ssids) p += "<option value=\"" + htmlEscape(s) + "\">";
    p += "</datalist><br><label>Password<br><input name=\"pass\" type=\"password\" autocomplete=\"off\"></label><br>"
         "<button type=\"submit\">Join</button></form><p>Seen here:</p><ul>";
    for (const std::string& s : ssids) p += "<li>" + htmlEscape(s) + "</li>";
    p += "</ul></body></html>";
    return p;
}

}  // namespace axis::bbs
