// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// axis_netkey: makes and reads Sysop Net signing seeds on the PC.
//   axis_netkey new <file>   make a 32 byte seed from the OS random source, write it as hex, print pub and board
//   axis_netkey pub <file>   read a seed file, print pub and board
// The seed itself is never printed. Keep the files offline and out of git.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#include "net/sign.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

void wipe(void* p, size_t n) {
    volatile uint8_t* v = static_cast<volatile uint8_t*>(p);
    while (n--) *v++ = 0;
}

int fail(const char* msg) {
    std::fprintf(stderr, "axis_netkey: %s\n", msg);
    return 1;
}

// Creates path only if it does not exist (O_EXCL), writes the bytes, closes. Returns true only if the file was
// created and fully written. created is set true only when this call made the file, so the caller removes the
// file after a failure only in that case and never one it did not make.
bool writeNew(const char* path, const char* data, size_t len, bool& created) {
    created = false;
#ifdef _WIN32
    int fd = _open(path, _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
    if (fd < 0) return false;
    created = true;
    bool ok = _write(fd, data, (unsigned)len) == (int)len;
    return _close(fd) == 0 && ok;
#else
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) return false;
    created = true;
    bool ok = write(fd, data, len) == (ssize_t)len;
    return close(fd) == 0 && ok;
#endif
}

// Reads up to n bytes of a file with no stdio buffer, so the seed text is never left in a FILE buffer.
// Returns the byte count, or -1 if the file cannot be opened.
long readRaw(const char* path, char* buf, size_t n) {
#ifdef _WIN32
    int fd = _open(path, _O_RDONLY | _O_BINARY);
    if (fd < 0) return -1;
    int got = _read(fd, buf, (unsigned)n);
    _close(fd);
#else
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t got = read(fd, buf, n);
    close(fd);
#endif
    return got < 0 ? 0 : (long)got;
}

bool randomBytes(uint8_t* out, size_t n) {
#ifdef _WIN32
    return BCryptGenRandom(nullptr, out, (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
    FILE* f = std::fopen("/dev/urandom", "rb");
    if (!f) return false;
    bool ok = std::fread(out, 1, n, f) == n;
    std::fclose(f);
    return ok;
#endif
}

// Prints the two public lines for a seed.
void show(const uint8_t seed[32]) {
    axis::net::KeyPair k = axis::net::keyFromSeed(seed);
    std::string        pub = axis::net::toHex(k.pub, 32);
    std::printf("pub %s\nboard %s\n", pub.c_str(), axis::net::boardId(pub).c_str());
    wipe(&k, sizeof k);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3 || (std::strcmp(argv[1], "new") != 0 && std::strcmp(argv[1], "pub") != 0))
        return fail("usage: axis_netkey new <file> | axis_netkey pub <file>");
    const std::string path = argv[2];
    namespace fs = std::filesystem;
    std::error_code ec;
    uint8_t         seed[32];
    char            text[80];
    int             rc = 1;

    if (std::strcmp(argv[1], "new") == 0) {
        fs::path parent = fs::path(path).parent_path();
        if (fs::exists(fs::path(path), ec)) return fail("that file already exists, not overwriting it");
        if (!parent.empty() && !fs::is_directory(parent, ec)) return fail("the folder does not exist");
        if (!randomBytes(seed, 32)) return fail("the OS random source failed");
        {
            std::string h = axis::net::toHex(seed, 32);  // hex copies are wiped right after use
            std::memcpy(text, h.data(), 64);
            wipe(&h[0], h.size());
        }
        text[64] = '\n';
        bool created = false;
        bool ok = writeNew(path.c_str(), text, 65, created);
        wipe(text, sizeof text);
        if (!ok) {
            if (created) std::remove(path.c_str());  // only a file this run made
            wipe(seed, sizeof seed);
            return fail("could not create or write the file");
        }
        show(seed);
        rc = 0;
    } else {
        long got = readRaw(path.c_str(), text, sizeof text - 1);
        if (got < 0) return fail("could not open the file");
        size_t n = (size_t)got;
        text[n] = 0;
        while (n && (text[n - 1] == '\n' || text[n - 1] == '\r' || text[n - 1] == ' ')) text[--n] = 0;
        if (n != 64 || !axis::net::fromHex(text, seed, 32)) {
            wipe(text, sizeof text);
            wipe(seed, sizeof seed);
            return fail("the file is not a 64 character hex seed");
        }
        show(seed);
        rc = 0;
    }
    wipe(seed, sizeof seed);
    wipe(text, sizeof text);
    return rc;
}
