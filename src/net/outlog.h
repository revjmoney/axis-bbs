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

// The file holds one entry per line, line 0 being index `base`. The base lives in a small side
// file (saved only when a trim moves it). Call open() once the storage is ready.
class FileOutLog : public OutLog {
public:
    struct Io {
        std::function<bool(const std::string& line)> append;  // add one line (newline added by the host)
        // Line n (0 based) without its newline; false past the end. Reads are mostly sequential
        // (n, n+1, ...), so the host should remember its last position.
        std::function<bool(uint32_t n, std::string& line)> readLine;
        std::function<uint32_t()> lineCount;          // lines in the file now
        std::function<bool(uint32_t skip)> dropFront;  // rewrite the file without its first skip lines
        std::function<std::string()> loadBase;         // side file text ("" if none)
        std::function<void(const std::string&)> saveBase;
    };
    explicit FileOutLog(Io io) : io_(std::move(io)) {}
    void open();

    uint32_t append(const std::string& entry) override;
    bool read(uint32_t index, std::string& entry) override;
    uint32_t first() const override { return base_; }
    uint32_t next() const override { return base_ + count_; }
    void trimTo(uint32_t keepLast) override;

private:
    Io io_;
    uint32_t base_ = 0, count_ = 0;
};

}  // namespace axis::net
