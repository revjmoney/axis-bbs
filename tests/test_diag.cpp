// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <sstream>
#include <string>

#include "doctest.h"
#include "bbs/diag.h"

using namespace axis::bbs;

TEST_CASE("status diagnostics name the reset reason and the last door in one token each") {
    DiagSample s;
    s.minHeap = 41000;
    s.stackWords = 900;
    s.doorPeakKb = 72;
    s.doorStackWords = 640;
    s.door = DoorDiag::OutOfMemory;
    s.resetCode = 9;
    std::string fields = diagFields(s);
    CHECK(fields.find(" min=41000") == 0);
    CHECK(fields.find(" stack=900") != std::string::npos);
    CHECK(fields.find(" door_peak=72") != std::string::npos);
    CHECK(fields.find(" door=oom") != std::string::npos);
    CHECK(fields.find(" door_stack=640") != std::string::npos);
    CHECK(fields.find(" reset=brownout") != std::string::npos);

    std::string line = "id=aa role=main nodes=1 on=0 heap=50000 largest=30000 up=3 rssi=-40 sd_mb=10 ip=10.0.0.2" + fields;
    std::istringstream in(line);
    std::string        tok;
    int                n = 0;
    while (in >> tok) {
        n++;
        CHECK(tok.find('=') != std::string::npos);
        CHECK(tok.find('=') == tok.rfind('='));
        CHECK(tok.find("password") == std::string::npos);
        CHECK(tok.find("webhook") == std::string::npos);
    }
    CHECK(n == 16);  // the ten original fields plus min, stack, door_peak, door, door_stack, reset
}

TEST_CASE("status diagnostics use a single word for every reset and door result") {
    CHECK(std::string(resetToken(0)) == "unknown");
    CHECK(std::string(resetToken(1)) == "poweron");
    CHECK(std::string(resetToken(3)) == "sw");
    CHECK(std::string(resetToken(4)) == "panic");
    CHECK(std::string(resetToken(5)) == "intwdt");
    CHECK(std::string(resetToken(6)) == "taskwdt");
    CHECK(std::string(resetToken(7)) == "wdt");
    CHECK(std::string(resetToken(8)) == "deepsleep");
    CHECK(std::string(resetToken(10)) == "sdio");
    CHECK(std::string(resetToken(99)) == "unknown");
    CHECK(std::string(doorDiagToken(DoorDiag::None)) == "none");
    CHECK(std::string(doorDiagToken(DoorDiag::Running)) == "running");
    CHECK(std::string(doorDiagToken(DoorDiag::Quit)) == "quit");
    CHECK(std::string(doorDiagToken(DoorDiag::Hangup)) == "hangup");
    CHECK(std::string(doorDiagToken(DoorDiag::TimeLimit)) == "time");
    CHECK(std::string(doorDiagToken(DoorDiag::LoadError)) == "load");
}
