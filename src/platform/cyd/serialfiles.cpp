// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "platform/cyd/serialfiles.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <dirent.h>
#include <sys/stat.h>

#include <cstdio>

#include "platform/cyd/storage.h"

namespace {
String full(const String& path) { return String(storageRoot()) + (path.startsWith("/") ? "" : "/") + path; }

void mkdirs(const String& path) {  // every folder along the way
    for (int i = 1; i <= (int)path.length(); i++)
        if (i == (int)path.length() || path[i] == '/') ::mkdir(path.substring(0, i).c_str(), 0777);
}

void put(const String& args) {
    int      sp = args.lastIndexOf(' ');
    String   path = full(args.substring(0, sp));
    size_t   size = args.substring(sp + 1).toInt(), done = 0;
    uint32_t sum = 0;
    if (sp < 1) {
        Serial.println("@ERR usage");
        return;
    }
    mkdirs(path.substring(0, path.lastIndexOf('/')));
    int f = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);  // a descriptor, like every other file here
    if (f < 0) {
        Serial.printf("@ERR open %d [%s]\n", errno, path.c_str());
        return;
    }
    static uint8_t blk[512];
    Serial.println("@READY");
    Serial.setTimeout(3000);
    while (done < size) {
        size_t want = min(size - done, sizeof blk);
        if (Serial.readBytes(blk, want) != want) break;
        if (::write(f, blk, want) != (ssize_t)want) break;
        for (size_t i = 0; i < want; i++) sum += blk[i];
        done += want;
        Serial.println("@K");  // the PC waits for this, so the 256 byte RX buffer never overflows
    }
    ::close(f);
    Serial.printf(done == size ? "@DONE %lu\n" : "@ERR short %lu\n", (unsigned long)(done == size ? sum : done));
}

void typeFile(const String& args) {  // a small text file, so a node file can be patched without a full rewrite
    String path = args;
    path.trim();
    if (!path.length()) {
        Serial.println("@ERR usage");
        return;
    }
    path = full(path);
    int f = ::open(path.c_str(), O_RDONLY);
    if (f < 0) {
        Serial.printf("@ERR open %d\n", errno);
        return;
    }
    struct stat st;
    if (::fstat(f, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 || st.st_size > 8192) {
        ::close(f);
        Serial.println("@ERR");
        return;
    }
    Serial.printf("@READY %lu\n", (unsigned long)st.st_size);
    static uint8_t blk[512];
    size_t left = (size_t)st.st_size;
    while (left) {
        size_t want = left > sizeof blk ? sizeof blk : left;
        ssize_t got = ::read(f, blk, want);
        if (got <= 0) break;
        Serial.write(blk, (size_t)got);
        left -= (size_t)got;
    }
    ::close(f);
    if (left) Serial.println("\n@ERR short");
}
}  // namespace

bool serialFileCommand(const String& cmd) {
    if (cmd.startsWith("@PUT ")) {
        put(cmd.substring(5));
    } else if (cmd.startsWith("@LS ")) {
        String dir = full(cmd.substring(4));
        if (DIR* d = opendir(dir.c_str())) {
            while (dirent* e = readdir(d)) {
                struct stat st;
                String      name = e->d_name;
                if (name == "." || name == "..") continue;
                bool ok = ::stat((dir + "/" + name).c_str(), &st) == 0;
                Serial.printf("%s %u%s\n", name.c_str(), ok ? (unsigned)st.st_size : 0u, ok && S_ISDIR(st.st_mode) ? "/" : "");
            }
            closedir(d);
        }
        Serial.println("@END");
    } else if (cmd.startsWith("@RM ")) {
        Serial.println(::remove(full(cmd.substring(4)).c_str()) == 0 ? "@OK" : "@ERR");
    } else if (cmd.startsWith("@MKDIR ")) {
        mkdirs(full(cmd.substring(7)));
        Serial.println("@OK");
    } else if (cmd.startsWith("@BAUD ")) {
        Serial.println("@OK");
        Serial.flush();
        Serial.updateBaudRate(cmd.substring(6).toInt());
    } else if (cmd.startsWith("@TYPE ")) {
        typeFile(cmd.substring(6));
    } else {
        return false;
    }
    return true;
}
