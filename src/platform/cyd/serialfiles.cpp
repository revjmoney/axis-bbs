// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "platform/cyd/serialfiles.h"

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
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) {
        Serial.println("@ERR open");
        return;
    }
    static uint8_t blk[512];
    Serial.println("@READY");
    Serial.setTimeout(3000);
    while (done < size) {
        size_t want = min(size - done, sizeof blk);
        if (Serial.readBytes(blk, want) != want) break;
        if (fwrite(blk, 1, want, f) != want) break;
        for (size_t i = 0; i < want; i++) sum += blk[i];
        done += want;
        Serial.println("@K");  // the PC waits for this, so the 256 byte RX buffer never overflows
    }
    fclose(f);
    Serial.printf(done == size ? "@DONE %lu\n" : "@ERR short %lu\n", (unsigned long)(done == size ? sum : done));
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
    } else {
        return false;
    }
    return true;
}
