// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Telnet protocol handling for one caller: option negotiation in, clean bytes out.
// Portable (no sockets); the firmware and the PC host feed it raw network bytes.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace axis::net {

// LFD3K-LINK, telnet option 90: the LORA FLOWTRON DATAMASTER 3000 SERIAL MODEM - LFD3K-SM offers WILL 90, and once we
// answer DO 90 it sends "IAC SB 90 v=1 modem=... kind=radio tier=SF7 bps=... snr=... rssi=... retrains=... lz=... IAC SE"
// at connect, on every retrain and every 10 s. Display and screen weight only: it never grants anything, so a spoofed
// report can at worst mislabel a caller. Every field is optional (unknown until a report names it) and clamped.
constexpr uint8_t kLinkOption = 90;
constexpr size_t  kLinkPayloadMax = 160;  // a longer report is dropped whole

struct LinkInfo {
    bool        any = false;                // a report has come in this call
    int         version = 0;                // v=
    std::string modem, kind, caller, tier;  // names: letters, digits and - _ . only; kind "radio" or "wifi"; tier SF5..SF12 or WIFI
    int32_t     bps = -1;                   // air bits per second, -1 unknown
    int         snrTenths = 0, rssi = 0;    // SNR in tenths of a dB, RSSI in dBm
    bool        hasSnr = false, hasRssi = false;
    int32_t     retrains = -1;              // -1 unknown
    int         lzTenths = -1;              // compression ratio in tenths (18 = 1.8), 0 = off, -1 unknown
    bool        radio() const { return kind == "radio" || (kind.empty() && sf() > 0); }
    int         sf() const;                 // 5..12 for a LoRa tier, 0 otherwise
};

// One report's payload (the bytes after the option number). False, and `out` untouched, for anything that is not a
// report: too long, a byte outside printable ASCII, or no v= key.
bool        parseLinkInfo(const std::string& payload, LinkInfo& out);
// "RADIO SF7 -81" (withRssi) or "RADIO SF7"; "WIFI" for a call that came over the modem's wire; "" when no report came.
std::string linkLabel(const LinkInfo& l, bool withRssi = true);

class TelnetFilter {
public:
    // What the server sends right after accepting a caller.
    std::string initialNegotiation() const;
    // Raw bytes from the caller: clean keystrokes go to toApp, protocol answers to replyToNet.
    void fromNet(const uint8_t* p, size_t n, std::string& toApp, std::string& replyToNet);
    // Bytes from the BBS or a door, escaped for the wire (0xFF doubled).
    std::string toNet(const uint8_t* p, size_t n) const;
    size_t pendingSubnegotiation() const { return sb_.size(); }
    int width() const { return width_; }
    bool callerBinary() const { return callerBinary_; }  // caller said WILL BINARY: input is 8-bit clean
    int height() const { return height_; }
    // LFD3K-LINK: answer the modem's WILL 90 with DO 90 (off: DONT, as for any option we don't use). Off by default.
    // Turned off during a call, further reports are ignored.
    void            acceptLink(bool on) { linkOn_ = on; }
    bool            linkAgreed() const { return linkAgreed_; }
    const LinkInfo& link() const { return link_; }
    uint32_t        linkReports() const { return linkSeq_; }  // one more for every report taken

private:
    enum class St { Data, Iac, Opt, Sb, SbIac } st_ = St::Data;
    uint8_t     cmd_ = 0;      // WILL/WONT/DO/DONT waiting for its option byte
    std::string sb_;           // subnegotiation payload
    bool        sbOver_ = false;  // this subnegotiation ran past the cap: dropped whole at IAC SE
    bool        lastCr_ = false, callerBinary_ = false;
    bool        linkOn_ = false, linkAgreed_ = false;
    LinkInfo    link_;
    uint32_t    linkSeq_ = 0;
    int         width_ = 80, height_ = 25;
    void        subnegotiation();  // a whole IAC SB ... IAC SE has arrived
};

}  // namespace axis::net
