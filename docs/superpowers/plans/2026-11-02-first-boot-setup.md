# First-Boot Setup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (Rev chose native execution). Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** On first boot the board gets WiFi from the touchscreen, then asks with a tap whether to finish setup on the screen or over telnet (setup PIN), runs a wizard that creates the sysop account and BBS settings, saves them, and the BBS uses them.

**Architecture:** The wizard is portable C++ (`SetupWizard`): a list of fields with defaults and validators driven by text input, so the telnet path and PC tests share it; the touch path drives the same field list with an on-screen keyboard. Settings live in `BbsConfig`, saved as an INI file in LittleFS (internal flash, so settings survive card swaps). Passwords are salted SHA-256 (public-domain implementation by Brad Conte, vendored, so PC tests and the board share one implementation).

**Tech Stack:** as plan 1 (C++17, doctest on the PC, PlatformIO + Arduino-ESP32 + LovyanGFX on the CYD), plus LittleFS.

**Spec:** `docs/superpowers/specs/2026-11-02-axis-underground-bbs-v1-design.md` section 5.13 (also 5.4 users, 5.9 sysop).

## Global Constraints

- License header on every new file (`SPDX-License-Identifier: GPL-3.0-or-later`, `Copyright (C) 2026 Rev. J. Money`); vendored code keeps its own notice and is listed in `THIRD_PARTY_NOTICES.md`.
- Defaults: BBS name `Axis Underground CNC`, board name `axis` (so `axis.local`), telnet port `23`, timezone `EASTERN` (`EST5EDT,M3.2.0,M11.1.0`), FTN address `99:1/0`, new-user security `10`, daily minutes `60`, idle minutes `5`.
- Timezones offered: the CYD Demoscene list (EASTERN, CENTRAL, MOUNTAIN, ARIZONA, PACIFIC, ALASKA, HAWAII, UTC, UK, CENTRAL EUROPE) with the same POSIX rules.
- Setup PIN: 6 digits from the hardware RNG, shown only on the CYD screen; never logged over serial.
- Passwords: never stored or logged in clear; config holds `salt` (16 hex bytes) and `sha256(salt + password)` hex.
- WiFi password: same rules as the demoscene keyboard (never logged; only page and character count in debug output).
- Config file: `/axis.ini` in LittleFS, `key=value` lines, unknown keys preserved on save.
- Screens drawn for the wizard are also capturable: a serial command `@SHOT` dumps the current screen buffer so tests can save PNGs (the panel cannot be read back).

## Review Focus

1. Power loss in the middle of setup: the board must boot back into setup, never half-configured. Test: Task 2 `setupDone only after save` and Task 3 `wizard result not applied until confirm`.
2. Bad input in fields (port 0 or 70000, FTN address `abc`, empty sysop handle, mismatched password confirm): re-ask with a reason, never crash. Test: Task 3 validator cases.
3. A wrong setup PIN over telnet: the caller gets a polite refusal and is disconnected after 3 tries. Test: Task 4 `pin gate`.
4. Config file damaged or from a newer version (unknown keys, missing keys): load defaults for missing keys and keep unknown ones. Test: Task 2 `missing keys get defaults`, `unknown keys survive save`.
5. Telnet caller hangs up mid-wizard: nothing is saved, the board stays in setup mode. Test: Task 4 live check.

---

### Task 1: SHA-256 and password hashing

**Files:** Create `third_party/sha256/sha256.{h,c}` (Brad Conte crypto-algorithms, public domain), `src/bbs/password.h/.cpp`, `tests/test_password.cpp`; modify `CMakeLists.txt`.

**Interfaces (produces):**
```cpp
namespace axis::bbs {
std::string sha256Hex(const std::string& data);
std::string makeSalt(uint32_t (*rng)());                  // 32 hex chars
std::string hashPassword(const std::string& salt, const std::string& password);
bool        checkPassword(const std::string& salt, const std::string& hash, const std::string& password);
}
```
- [ ] Failing tests: `sha256 of "abc"` = `ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad`; `sha256 of empty` = `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`; `check accepts the right password and rejects others`; `salt is 32 hex chars and differs per rng`.
- [ ] Run, see FAIL; implement; run, see PASS; commit.

### Task 2: BbsConfig load and save

**Files:** Create `src/bbs/config.h/.cpp`, `tests/test_config.cpp`.

**Interfaces (produces):**
```cpp
struct BbsConfig {
    std::string sysopHandle, sysopSalt, sysopHash, sysopRealName;
    std::string bbsName = "Axis Underground CNC", location, boardName = "axis", publicHost;
    int telnetPort = 23; std::string timezone = "EASTERN"; std::string ftnAddress = "99:1/0";
    int newUserSecurity = 10, dailyMinutes = 60, idleMinutes = 5;
    bool setupDone = false;
    std::vector<std::pair<std::string, std::string>> unknown;   // preserved on save
};
BbsConfig   parseConfig(const std::string& ini);
std::string formatConfig(const BbsConfig& c);
const char* tzRule(const std::string& name);            // "" if unknown
const std::vector<std::string>& tzNames();
```
- [ ] Failing tests: `round trip keeps every field`; `missing keys get defaults`; `unknown keys survive save`; `setupDone only after save` (a config without `setup_done=1` parses as not done); `tz rule for EASTERN`; values with `=` and spaces survive.
- [ ] Run, FAIL; implement; PASS; commit.

### Task 3: SetupWizard (text)

**Files:** Create `src/bbs/setup_wizard.h/.cpp`, `tests/test_setup_wizard.cpp`.

**Interfaces (produces):**
```cpp
class SetupWizard {
public:
    SetupWizard(BbsConfig start, uint32_t (*rng)());
    void begin();                                         // intro + first question
    void input(const uint8_t* p, size_t n);               // clean keystrokes (CR ends a line)
    std::string takeOutput();
    bool finished() const;                                // confirmed and complete
    bool cancelled() const;
    const BbsConfig& result() const;                      // setupDone=true only when finished()
    // field metadata for the touch path
    struct Field { std::string key, label, hint; bool secret; };
    const std::vector<Field>& fields() const;
    std::string defaultFor(const std::string& key) const;
    std::string validate(const std::string& key, const std::string& value) const;   // "" = ok, else reason
    void set(const std::string& key, const std::string& value);
};
```
Field order: sysop handle, sysop password, password again, real name, BBS name, location, board name, public hostname (optional), telnet port, timezone (number from the list), FTN address, new-user security, daily minutes, idle minutes; then a summary and `Save these settings? (Y/N)`. Enter keeps the default shown in brackets. Echo with `*` for secret fields.
- [ ] Failing tests: `accepting all defaults after handle and password finishes with defaults`; `port 70000 is rejected with a reason and asked again`; `ftn abc rejected`; `empty handle rejected`; `password confirm mismatch restarts the password`; `timezone by number`; `password echo is stars`; `N at the summary cancels` (cancelled, not finished); `wizard result not applied until confirm` (setupDone false until Y); `stored password verifies with checkPassword`.
- [ ] Run, FAIL; implement; PASS; commit.

### Task 4: Telnet setup flow and config in the firmware

**Files:** Create `src/platform/cyd/config_store.h/.cpp` (LittleFS `/axis.ini` load/save), `src/bbs/setup_gate.h/.cpp` (portable PIN gate: `SetupGate(pin)`, `input`, `takeOutput`, `passed()`, `rejected()`), `tests/test_setup_gate.cpp`; modify `src/main.cpp`, `src/bbs/node.h/.cpp` (welcome and goodbye use `BbsConfig::bbsName`; idle timeout from config).

- [ ] Failing tests: `pin gate passes on the right pin`; `pin gate rejects after 3 wrong tries`; `node welcome shows the configured BBS name`; `idle minutes come from config`.
- [ ] Implement. Firmware: on boot load config; if `!setupDone` show the choice screen (two big buttons; `@TAP x y` serial hook for tests) and a setup PIN; a telnet caller goes through `SetupGate` then `SetupWizard`; on finish save, apply (mDNS name, port, timezone) and switch to normal answering without a reboot. Callers during setup without the PIN get "this BBS is being set up".
- [ ] Live test on the CYD: erase `/axis.ini` (`@RESETSETUP` serial command), reboot, choose telnet with `@TAP`, read the PIN from `@SHOT`, run the wizard over telnet with the screenshot tool, check the welcome shows the new BBS name. Screenshots of every step (terminal and CYD screen) go in `shots/setup/`.
- [ ] Commit.

### Task 5: Touch WiFi and on-screen wizard

**Files:** Create `src/platform/cyd/touchui.h/.cpp` (buttons, header, message, 10x5 keyboard with lower/upper/symbol pages, `typeText`, `pickFromList`, `serialTap` hook; adapted from CYD Demoscene `wifikbd.cpp`), `src/platform/cyd/wifisetup.h/.cpp` (scan list, HIDDEN..., open-network confirm, test join, save to NVS `net`), `src/platform/cyd/screen.h/.cpp` (8-bit 320x240 sprite the UI draws into, pushed to the panel; `@SHOT` dumps it as hex RGB332 over serial), `tools/screenshots/cyd_shot.py` (reads `@SHOT`, writes PNG); modify `src/main.cpp`.
- [ ] Implement; no PC unit tests (hardware UI), so verify on the board with `@TAP` taps and `@SHOT` screenshots: WiFi picker list, keyboard pages, choice screen, each wizard field on screen, summary, saved. Use the HIDDEN... path for WiFi tests (never tap a real network in automated tests; never type Rev's password).
- [ ] Commit; update `docs/FEATURES.md`, brain note, and the BBS Screenshot Tool note (CYD screen capture).
