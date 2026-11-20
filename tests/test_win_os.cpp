// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The Windows half of axispc's OS layer: a listening socket, the data folder, and the queries the waiting screen uses.
// Built only for the Windows test binary (CMakeLists.pc.txt).
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <cstdio>
#include <direct.h>
#include <string>

#include "doctest.h"
#include "platform/linux/paths.h"
#include "platform/os/os.h"

TEST_CASE("windows listen accepts one loopback caller") {
    REQUIRE(axis::os::netStart());
    axis::os::ListenOut lst = axis::os::listenAny(0);
    REQUIRE(lst.fd != axis::os::kBadSocket);
    sockaddr_in bound{};
    int         bl = sizeof bound;
    REQUIRE(getsockname((SOCKET)lst.fd, (sockaddr*)&bound, &bl) == 0);
    REQUIRE(bound.sin_port != 0);
    SOCKET cli = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    REQUIRE(cli != INVALID_SOCKET);
    sockaddr_in to = bound;
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    REQUIRE(connect(cli, (sockaddr*)&to, sizeof to) == 0);
    axis::os::Accepted got{};
    for (int i = 0; i < 50 && got.fd == axis::os::kBadSocket; i++) {
        got = axis::os::acceptOne(lst.fd);
        if (got.fd == axis::os::kBadSocket && !got.retry) Sleep(10);
    }
    REQUIRE(got.fd != axis::os::kBadSocket);
    CHECK(got.ip == axis::os::ipv4("127.0.0.1"));
    char    byte = 'A';
    REQUIRE(send(cli, &byte, 1, 0) == 1);
    char  back = 0;
    u_long nb = 1;
    ioctlsocket((SOCKET)got.fd, FIONBIO, &nb);
    int n = 0;
    for (int i = 0; i < 50 && n != 1; i++) {
        n = recv((SOCKET)got.fd, &back, 1, 0);
        if (n < 0) Sleep(10);
    }
    CHECK(n == 1);
    CHECK(back == 'A');
    axis::os::closeSocket(got.fd);
    axis::os::closeSocket(lst.fd);
    closesocket(cli);
}

TEST_CASE("windows data folder and the host queries") {
    char  tmp[MAX_PATH];
    DWORD n = GetTempPathA(sizeof tmp, tmp);
    REQUIRE(n > 0);
    REQUIRE(n < sizeof tmp);
    char dir[MAX_PATH];
    bool made = false;
    for (int i = 0; i < 100 && !made; i++) {
        std::snprintf(dir, sizeof dir, "%saxis-data-%lu-%d", tmp, (unsigned long)GetCurrentProcessId(), i);
        if (_mkdir(dir) == 0) made = true;
    }
    REQUIRE(made);
    std::string why;
    REQUIRE(axisDataSetup(dir, "", why));
    CHECK(axisDataDir().find("axis-data-") != std::string::npos);
    CHECK(axis::os::makeDir(std::string(axisDataDir()) + "/sd/extra", 0777) == 0);
    CHECK(axis::os::diskFree(axisDataDir()) > 0);
    CHECK(axis::os::memAvailable() > 0);
    CHECK(axis::os::osMachine().find("Windows") == 0);
    CHECK(axis::os::ipv4("127.0.0.2") != 0);
    CHECK(axis::os::ipv4("nope") == 0);
}
