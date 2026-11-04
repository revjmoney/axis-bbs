// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The message base: areas of numbered messages (from, to, subject, date, body). The main keeps one
// text file per area (<root>/<area>.msg: a "\x01MSG" line, then key=value header lines, a blank
// line, the body); a follower asks the main over the cluster link (RemoteMessageStore). FidoNet
// JAM/PKT can sit on top of this later.
#pragma once
#include <functional>
#include <string>
#include <vector>

namespace axis::bbs {

struct Message {
    int         num = 0;
    std::string from, to, subject, date, body;  // body lines separated by '\n'
};

class MessageStore {
public:
    static constexpr size_t kMaxHeaders = 200;  // headers() returns at most the newest 200
    virtual std::vector<Message> headers(const std::string& area) = 0;  // without bodies, oldest first
    virtual bool                 read(const std::string& area, int num, Message& out) = 0;
    virtual int                  post(const std::string& area, const Message& m) = 0;  // its number, or 0
    virtual ~MessageStore() = default;
};

class FileMessageStore : public MessageStore {
public:
    explicit FileMessageStore(std::string root) : root_(std::move(root)) {}
    std::vector<Message> headers(const std::string& area) override;
    bool                 read(const std::string& area, int num, Message& out) override;
    int                  post(const std::string& area, const Message& m) override;

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
