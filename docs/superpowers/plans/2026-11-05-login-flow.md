# ViSiON/3 Login Flow (plan 2 of the ViSiON-style experience) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** When a board has menu files, a caller's call runs ViSiON/3's front door and login: the pre-login lightbar matrix (log in, new user, check access, disconnect), random pre-logon art, the LOGIN screen with its handle and password spots, the configurable login sequence (sysop notices, mail, the fast-login lightbar, last callers, oneliners, your stats...), then MAIN.

**Architecture:** The menu engine (plan 1) runs PDMATRIX and FASTLOGN as ordinary menus; their non-RUN verbs (LOGIN, NEWUSER, CHECKACCESS, DISCONNECT) come back as `Run` actions. A new portable `loginseq` unit parses `/menus/LOGIN.SEQ`. `Node` keeps its existing handle/password/application/LoginGuard state machines and adds a front door and a sequence driver around them, only when `MAIN.MNU` exists (the old flow stays for boards without menus).

**Tech Stack:** C++17, ESP-IDF 4.4.7 + Arduino component, doctest.

**Spec:** `docs/superpowers/specs/2026-11-05-axis-vision-experience-design.md` (section "The caller side": login flow exactly as ViSiON/3 does it). Reference: `F:\vision-3-bbs\docs\sysop\users\login-sequence.md`, `menus/v3/cfg/PDMATRIX.CFG`, `FASTLOGN.CFG`.

## Global Constraints

- Plan 1's constraints hold (stream art, `@` commands, ViSiON/3 as reference only, test on board 2, never touch board 1).
- The existing login security stays exactly as is: LoginGuard brake, password hold, NUP, wrong-password hang-up.
- Without `MAIN.MNU` the call flows exactly as today.

## Review Focus

1. A caller who wanders the matrix (Check Access, back, Check Access...) is disconnected after 10 picks without logging in, as ViSiON/3 does.
2. LOGIN.ANS without the `|{P}`/`|{O}` markers (or with only one) still logs in, with plain "Handle:"/"Password:" prompts.
3. A login sequence step whose function AXIS lacks (PRINTNEWS, CHECKNUV...) is skipped, never stalls the call.
4. FASTLOGN option 2-4 ends the sequence and jumps to that menu; option 1 continues the sequence.
5. A sequence step limited by level (`sec_level 255`) is skipped for an ordinary caller.

---

### Task 1: The runner passes non-RUN verbs through

**Files:** Modify `src/bbs/menurunner.cpp`; Test `tests/test_menurunner.cpp`

**Interfaces:** Produces: any CMD verb other than GOTO/DOOR/LOGOFF/QUIT/RUN comes back as `MenuAction{Run, verb, args}` (e.g. `LOGIN` → `Run "LOGIN"`).

- [ ] **Step 1: failing test**

```cpp
TEST_CASE("runner: pre-login verbs come back as Run actions") {
    Rig g;
    g.fs = {{"PDMATRIX.MNU", "cmd=J|LOGIN|*|0|\ncmd=D|DISCONNECT|*|0|\nbar=49,10,31,5,J,Journey onward.\n"}};
    REQUIRE(g.r.enter("PDMATRIX", AcsUser{0, ""}));
    MenuAction a = g.r.key('j');
    CHECK(a.kind == MenuAction::Run);
    CHECK(a.arg == "LOGIN");
    CHECK(g.r.key('d').arg == "DISCONNECT");
}
```

- [ ] **Step 2:** run → FAIL. **Step 3:** in `MenuRunner::run`, an unknown verb → `Run` with `arg = verb`. **Step 4:** PASS, suite green. **Step 5:** commit.

### Task 2: The login sequence file

**Files:** Create `src/bbs/loginseq.h`, `src/bbs/loginseq.cpp`; Test `tests/test_loginseq.cpp`

**Interfaces:** Produces:

```cpp
struct LoginStep { std::string cmd, data; bool clear = false, pause = false; int level = 0; };
std::vector<LoginStep> parseLoginSeq(const std::string& text);  // "" or no steps: ViSiON/3's shipped template
```

Format, one `/menus/LOGIN.SEQ` (`;` comments): `step=COMMAND|CLEAR(0/1)|PAUSE(0/1)|LEVEL|DATA`. The template (ViSiON/3 `configs/login.json` order): SYSOPNOTICES, NMAILSCAN (clear, pause), FASTLOGIN, PRINTNEWS, LASTCALLS, ONELINERS, USERSTATS, INFOFORMREQUIRED, NEWUSERVAL (level 255), CHECKNUV.

- [ ] **Step 1: failing tests**

```cpp
TEST_CASE("login sequence: a file's steps in order, with clear, pause, level and data") {
    auto s = parseLoginSeq("step=DISPLAYFILE|1|1|0|BULLETIN\nstep=ONELINERS|0|0|0|\nstep=NEWUSERVAL|0|0|255|\n");
    REQUIRE(s.size() == 3);
    CHECK(s[0].cmd == "DISPLAYFILE");
    CHECK(s[0].clear);
    CHECK(s[0].pause);
    CHECK(s[0].data == "BULLETIN");
    CHECK(s[2].level == 255);
}
TEST_CASE("login sequence: no file means ViSiON/3's shipped template") {
    auto s = parseLoginSeq("");
    REQUIRE(s.size() == 10);
    CHECK(s[0].cmd == "SYSOPNOTICES");
    CHECK(s[2].cmd == "FASTLOGIN");
    CHECK(s[8].level == 255);
}
```

- [ ] **Step 2:** FAIL. **Step 3:** implement. **Step 4:** PASS. **Step 5:** commit.

### Task 3: The front door: matrix, pre-logon art, LOGIN screen, check access

**Files:** Modify `src/bbs/node.h`, `src/bbs/node.cpp`; Test `tests/test_node_login.cpp`

**Interfaces:** Consumes Task 1. Produces: with menus on, `begin()` (after the splash) enters `PDMATRIX` (level 0 access) in `State::Matrix`; Run `LOGIN` → `PRELOGON1..9.ANS` at random among those present (else `PRELOGON.ANS`), "press a key" (new `State::PreLogon`), then `LOGIN.ANS` and the handle prompt; `|{P}`/`|{O}` in LOGIN.ANS place the handle and password input (found by drawing the art into an `AnsiTerm`, then blanked); Run `NEWUSER` → `NEWUSER.ANS` + a `YesNoBar` "Apply for access?" (new `State::ApplyBar`), yes → the existing application (handle prompt with the apply note), no → matrix; Run `CHECKACCESS` → "Handle to check:" (new `State::CheckAccess`) → "validated / waiting for the sysop / denied / no such user" → matrix; Run `DISCONNECT` → Done; the 11th matrix pick without logging in → "Too many tries." and Done.

- [ ] **Step 1: failing tests** (Rig as tests/test_node_menus.cpp, with a UserStore holding `Rev` validated):

```cpp
TEST_CASE("front door: the matrix lightbar, then pre-logon art, then the LOGIN screen") {
    // fs: PDMATRIX.MNU (J LOGIN, C NEWUSER, A CHECKACCESS, D DISCONNECT + bars), PRELOGON1.ANS "PRE ART", LOGIN.ANS "LOGIN ART"
    // begin(0): output has the matrix bar texts; type "j": "PRE ART"; any key: "LOGIN ART" and state Login
}
TEST_CASE("front door: check access reports a user's status and returns to the matrix") {
    // "a", "Rev\r": output contains "validated"; state Matrix
}
TEST_CASE("front door: ten wanders without logging in disconnect") {
    // 11 x ("a", "\r") → state Done
}
TEST_CASE("LOGIN.ANS markers put the handle and password where the art says, and are blanked") {
    // LOGIN.ANS "\x1b[5;10HUser: |{P}\x1b[6;10HPass: |{O}": after "j"+key, output has "\x1b[5;16H" and no "|{P}"
}
TEST_CASE("new user: NEWUSER.ANS and a yes/no lightbar; no goes back to the matrix") {
    // "c": "NEWUSER ART"; "n": state Matrix
}
```

(Each written out with real `CHECK`s on `plain(r.type(...))` and `r.n.state()`.)

- [ ] **Step 2:** FAIL. **Step 3:** implement. **Step 4:** PASS + suite. **Step 5:** commit.

### Task 4: The login sequence driver and fast login

**Files:** Modify `src/bbs/node.h`, `src/bbs/node.cpp`; Test `tests/test_node_login.cpp`

**Interfaces:** Consumes Task 2. Produces: with menus on, a good login runs `LOGIN.SEQ` (or the template) instead of the old bulletin/oneliners chain: each step skipped when the caller's level is below its level; `clear` clears first; `pause` waits for a key (new `State::SeqPause`). Steps: `SYSOPNOTICES` and `NMAILSCAN` → the existing notes (mail count, sysop notes); `FASTLOGIN` → the `FASTLOGN` menu (Run `FULL_LOGIN_SEQUENCE` continues; any GOTO ends the sequence on that menu); `LASTCALLS` → the existing last callers screen; `ONELINERS` → the existing oneliners wall and add prompt; `USERSTATS` → the existing info/stats screen; `DISPLAYFILE` → `<data>.ANS`; `RANDOMRUMOR` → the rumor line; `WHOISONLINE` → the existing who's online; anything else skipped. The end → MAIN.

- [ ] **Step 1: failing tests:**

```cpp
TEST_CASE("login sequence: steps run in order, a level-limited step is skipped, then MAIN") { /* LOGIN.SEQ DISPLAYFILE NEWS(pause) + DISPLAYFILE SECRET level 255; Rev level 20: "NEWS ART", key, no "SECRET", then MAIN's prompt */ }
TEST_CASE("fast login: 1 continues the sequence, 2 jumps to MAIN and skips the rest") { /* FASTLOGN.MNU 1 FULL_LOGIN_SEQUENCE, 2 GOTO:MAIN; "2": MAIN prompt, later step's art absent */ }
TEST_CASE("an unknown sequence step is skipped") { /* step=PRINTNEWS then DISPLAYFILE X: "X ART" shows */ }
```

(Each written out with real `CHECK`s.)

- [ ] **Step 2:** FAIL. **Step 3:** implement. **Step 4:** PASS + suite; firmware builds. **Step 5:** commit.

### Task 5: Starter front-door art and a hand test on board 2

**Files:** Modify `tools/menus/make_starter.py` (adds PDMATRIX, FASTLOGN with their lightbars from ViSiON/3's BAR files recoloured to AXIS orange; placeholder PDMATRIX.ANS, PRELOGON1-3.ANS, LOGIN.ANS with `|{P}`/`|{O}`, NEWUSER.ANS, FASTLOGN.ANS; `LOGIN.SEQ` = the template), `docs/FEATURES.md`, brain.

- [ ] **Step 1:** regenerate `art/menus`, upload to board 2.
- [ ] **Step 2:** telnet to board 2 (no password typed): matrix shows; arrows and Enter; Check Access on a handle reports its status; J shows pre-logon art then the LOGIN screen with the cursor at the handle spot; D disconnects. Screenshots to Rev.
- [ ] **Step 3:** FEATURES + brain; commit; push.
