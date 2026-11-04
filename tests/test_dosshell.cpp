// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <filesystem>
#include <fstream>
#include <string>

#include "doctest.h"
#include "bbs/dosshell.h"
#include "bbs/password.h"
#include "door/bytestream.h"
#include "platform/posix/posixhostfs.h"

using namespace axis::bbs;
using axis::door::ScriptStream;
namespace fs = std::filesystem;

namespace {
const char* kSalt = "00112233445566778899aabbccddeeff";

struct Rig {
    fs::path     top = fs::temp_directory_path() / "axis_dosshell_test";
    fs::path     box = top / "dosbox";
    fs::path     bbs = top / "bbs";  // the board's own files, next to the sandbox
    ScriptStream line;
    DosGate      gate;
    uint32_t     now = 1000;
    std::unique_ptr<axis::PosixHostFs> hostFs;
    std::unique_ptr<DosShell>          shell;
    bool         running = true;

    explicit Rig(bool withPassword = true) {
        fs::remove_all(top);
        fs::create_directories(box);
        fs::create_directories(bbs);
        std::ofstream(bbs / "USERS.DAT") << "secret accounts";
        std::ofstream(box / "README.TXT") << "hello from the sandbox\r\n";
        hostFs.reset(new axis::PosixHostFs(box.string()));
        DosShellConfig c;
        c.handle = "Rev";
        if (withPassword) {
            c.salt = kSalt;
            c.hash = hashPassword(kSalt, "letmein");
        }
        c.quotaBytes = 4096;
        c.clockMs = [this] { return now; };
        shell.reset(new DosShell(*hostFs, line, gate, c));
    }
    ~Rig() {
        shell.reset();
        hostFs.reset();
        fs::remove_all(top);
    }
    std::string run(const std::string& keys) {
        line.push(keys);
        for (int i = 0; i < 50 && running; i++) running = shell->step();
        std::string out = line.output();
        line.clearOutput();
        return out;
    }
    std::string in() {  // log in
        shell->begin();
        return run("letmein\r");
    }
};
}  // namespace

TEST_CASE("the DOS door asks for its password and never echoes it") {
    Rig         r;
    r.shell->begin();
    std::string out = r.run("letmein\r");
    CHECK(out.find("letmein") == std::string::npos);
    CHECK(out.find("*******") != std::string::npos);
    CHECK(out.find("C:\\>") != std::string::npos);
}

TEST_CASE("three wrong passwords send the caller back without a prompt") {
    Rig r;
    r.shell->begin();
    std::string out = r.run("nope\rnope\rnope\r");
    CHECK(out.find("C:\\>") == std::string::npos);
    CHECK_FALSE(r.running);
    CHECK(r.shell->events().size() >= 1);
}

TEST_CASE("after five failures the door stays locked for everyone, even with the right password") {
    Rig r;
    r.shell->begin();
    r.run("a\rb\rc\r");
    r.running = true;
    r.shell.reset(new DosShell(*r.hostFs, r.line, r.gate, [&] {
        DosShellConfig c;
        c.salt = kSalt;
        c.hash = hashPassword(kSalt, "letmein");
        c.clockMs = [&r] { return r.now; };
        return c;
    }()));
    r.shell->begin();
    r.run("d\re\r");
    r.running = true;
    r.shell->begin();
    std::string out = r.run("letmein\r");
    CHECK(out.find("C:\\>") == std::string::npos);
    CHECK(out.find("locked") != std::string::npos);
    r.now += 11 * 60 * 1000;  // ten minutes later it opens again
    r.running = true;
    r.shell->begin();
    out = r.run("letmein\r");
    CHECK(out.find("C:\\>") != std::string::npos);
}

TEST_CASE("with no password set the door will not open at all") {
    Rig         r(false);
    r.shell->begin();
    std::string out = r.run("\r");
    CHECK(out.find("C:\\>") == std::string::npos);
    CHECK_FALSE(r.running);
}

TEST_CASE("DIR and TYPE show the sandbox") {
    Rig r;
    r.in();
    std::string out = r.run("dir\r");
    CHECK(out.find("README") != std::string::npos);
    out = r.run("type readme.txt\r");
    CHECK(out.find("hello from the sandbox") != std::string::npos);
}

TEST_CASE("nothing outside the sandbox can be reached") {
    Rig r;
    r.in();
    std::string out = r.run("type ..\\bbs\\users.dat\r");
    CHECK(out.find("secret") == std::string::npos);
    out = r.run("cd ..\\..\\..\r");
    out = r.run("dir\r");
    CHECK(out.find("USERS") == std::string::npos);
    CHECK(out.find("C:\\>") != std::string::npos);  // still at the sandbox root
    r.run("copy readme.txt ..\\bbs\\stolen.txt\r");
    CHECK_FALSE(fs::exists(r.bbs / "stolen.txt"));
    r.run("del ..\\bbs\\users.dat\r");
    CHECK(fs::exists(r.bbs / "USERS.DAT"));
    r.run("type d:\\users.dat\r");
}

TEST_CASE("MD, CD, COPY, REN, DEL and RD work inside the sandbox") {
    Rig r;
    r.in();
    r.run("md work\r");
    CHECK(fs::is_directory(r.box / "WORK"));
    std::string out = r.run("cd work\r");
    CHECK(out.find("C:\\WORK>") != std::string::npos);
    r.run("copy \\readme.txt a.txt\r");
    CHECK(fs::exists(r.box / "WORK" / "A.TXT"));
    r.run("ren a.txt b.txt\r");
    CHECK(fs::exists(r.box / "WORK" / "B.TXT"));
    r.run("del b.txt\r");
    CHECK_FALSE(fs::exists(r.box / "WORK" / "B.TXT"));
    r.run("cd ..\r");
    r.run("rd work\r");
    CHECK_FALSE(fs::exists(r.box / "WORK"));
}

TEST_CASE("COPY refuses to grow the sandbox past its quota") {
    Rig r;
    std::ofstream(r.box / "BIG.DAT") << std::string(3000, 'x');
    r.in();
    std::string out = r.run("copy big.dat big2.dat\r");
    CHECK_FALSE(fs::exists(r.box / "BIG2.DAT"));
    CHECK(out.find("full") != std::string::npos);
}

TEST_CASE("EXIT leaves the door, and so does a hang-up") {
    Rig r;
    r.in();
    r.run("exit\r");
    CHECK_FALSE(r.running);
    Rig h;
    h.in();
    h.line.hangUp();
    h.run("");
    CHECK_FALSE(h.running);
}

TEST_CASE("an unknown command says so") {
    Rig         r;
    r.in();
    std::string out = r.run("frobnicate\r");
    CHECK(out.find("Bad command or file name") != std::string::npos);
}
