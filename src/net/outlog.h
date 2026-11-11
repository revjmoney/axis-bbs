// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The net's outbound log: one numbered entry per record to pass on, read by each link peer from its
// own cursor. Indexes only grow and are never reused. MemOutLog keeps it in RAM (tests, the PC
// peer); FileOutLog keeps it in a text file through callbacks (the main: /net/outlog.txt).
#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <string>

namespace axis::net {

constexpr uint32_t kOutLogFail = 0xFFFFFFFFu;  // append could not store the entry

class OutLog {
public:
    // Adds an entry (one line, no newline) and returns its index, or kOutLogFail.
    virtual uint32_t append(const std::string& entry) = 0;
    virtual bool read(uint32_t index, std::string& entry) = 0;  // false if trimmed or not yet written
    virtual uint32_t first() const = 0;                          // the oldest index kept
    virtual uint32_t next() const = 0;                           // the index the next append gets
    virtual void trimTo(uint32_t keepLast) = 0;                  // keep only the newest keepLast entries
    virtual bool ok() const { return true; }  // false while the log cannot be used (cursors stay put)
    virtual ~OutLog() = default;
};

class MemOutLog : public OutLog {
public:
    uint32_t append(const std::string& entry) override;
    bool read(uint32_t index, std::string& entry) override;
    uint32_t first() const override { return first_; }
    uint32_t next() const override { return first_ + (uint32_t)entries_.size(); }
    void trimTo(uint32_t keepLast) override;

private:
    std::deque<std::string> entries_;
    uint32_t first_ = 0;
};

// The file's first line is "#<base>", the index of the entry on the next line; then one entry per
// line. The base lives in the same file, so a trim is one file swap: the host writes the new file
// whole, then puts it in place of the old one. Call open() once the storage is ready.
class FileOutLog : public OutLog {
public:
    struct Io {
        std::function<bool(const std::string& line)> append;  // add one line (newline added by the host)
        // File line n (0 based, the header is line 0) without its newline; false past the end.
        // Reads are mostly sequential (n, n+1, ...), so the host should remember its last position.
        std::function<bool(uint32_t n, std::string& line)> readLine;
        std::function<uint32_t()> lineCount;  // lines in the file now (0 if it is missing)
        // Write a new file: `header`, then the old file's lines from line `from` on; then swap it in
        // for the old one (write a temp file, then rename it over the old file). If the board can
        // stop between removing the old file and the rename, the host should take a leftover temp
        // file as the log when it opens it.
        std::function<bool(const std::string& header, uint32_t from)> replace;
    };
    explicit FileOutLog(Io io) : io_(std::move(io)) {}
    // Reads the header. False if the file exists but its first line could not be read (the file is
    // left untouched); until open succeeds, append fails and read and trimTo do nothing. The host
    // can call open again later.
    bool open();
    bool ok() const override { return ok_; }

    uint32_t append(const std::string& entry) override;
    bool read(uint32_t index, std::string& entry) override;
    uint32_t first() const override { return base_; }
    uint32_t next() const override { return base_ + count_; }
    void trimTo(uint32_t keepLast) override;

private:
    Io io_;
    uint32_t base_ = 0, count_ = 0;
    bool header_ = false;  // the file starts with its "#<base>" line
    bool ok_ = false;      // open succeeded
};

}  // namespace axis::net
