// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The message base: areas of numbered messages (from, to, subject, date, body). The main keeps one
// text file per area (<root>/<area>.msg: a "\x01MSG" line, then key=value header lines, a blank
// line, the body); a follower asks the main over the cluster link (RemoteMessageStore). FidoNet
// JAM/PKT can sit on top of this later.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace axis::bbs {

struct Message {
    int         num = 0;
    std::string from, to, subject, date, body;  // body lines separated by '\n'
    // A Sysop Net post: its encoded NetPost line (one line, fields joined by 0x1F). Empty for local
    // posts. read() gives the whole line; headers() give only its start, "P" 0x1F "<post id>", to save
    // memory. scanHeaders gives a Net Files listing ("L", at most 430 bytes) whole; headers() cut it too, since
    // they hold up to 200 at once. The file store writes it as a "net=" header line only when it is non-empty.
    std::string net;
};

class MessageStore {
public:
    static constexpr size_t kMaxHeaders = 200;  // headers() returns at most the newest 200
    virtual std::vector<Message> headers(const std::string& area) = 0;  // without bodies, oldest first
    virtual bool                 read(const std::string& area, int num, Message& out) = 0;
    virtual int                  post(const std::string& area, const Message& m) = 0;  // its number, or 0
    virtual int drop(const std::string&, const std::function<bool(const Message&)>&) { return 0; }  // the sysop removes posts
    // Every header of an area, oldest first, one at a time (as headers() gives them: no body, net cut short).
    // each returns false to stop. This default walks headers(), so it sees only the newest kMaxHeaders;
    // FileMessageStore streams the whole file and never holds more than one message.
    virtual void scanHeaders(const std::string& area, const std::function<bool(const Message&)>& each) {
        for (const Message& m : headers(area))
            if (!each(m)) return;
    }
    // scanHeaders from pos (0: the start), pos left where the header each refused is (a later call sees it again),
    // or at the end. FileMessageStore keeps a byte offset (the scan reads only from there); this default counts
    // headers and walks scanHeaders from the start.
    virtual void scanHeadersFrom(const std::string& area, uint32_t& pos, const std::function<bool(const Message&)>& each) {
        uint32_t n = 0;
        scanHeaders(area, [&](const Message& m) {
            if (n >= pos && !each(m)) return false;
            n++;
            return true;
        });
        pos = n;
    }
    virtual ~MessageStore() = default;
};

class FileMessageStore : public MessageStore {
public:
    explicit FileMessageStore(std::string root) : root_(std::move(root)) {}
    std::vector<Message> headers(const std::string& area) override;
    bool                 read(const std::string& area, int num, Message& out) override;
    int                  post(const std::string& area, const Message& m) override;
    // the sysop: rewrite the area without the posts `gone` picks (numbers stay); how many went
    int drop(const std::string& area, const std::function<bool(const Message&)>& gone) override;
    void scanHeaders(const std::string& area, const std::function<bool(const Message&)>& each) override;
    void scanHeadersFrom(const std::string& area, uint32_t& pos, const std::function<bool(const Message&)>& each) override;

private:
    std::string path(const std::string& area) const;
    std::string root_;
};

// The main's side of the link: one request ("M" + operation + fields) answered.
std::string serveMessages(MessageStore& store, const std::string& request);

class RemoteMessageStore : public MessageStore {
public:
    using Ask = std::function<bool(const std::string& request, std::string& reply)>;
    explicit RemoteMessageStore(Ask ask) : ask_(std::move(ask)) {}
    std::vector<Message> headers(const std::string& area) override;
    bool                 read(const std::string& area, int num, Message& out) override;
    int                  post(const std::string& area, const Message& m) override;

private:
    Ask ask_;
};

}  // namespace axis::bbs
