// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Callers' accounts: handle, salted password hash, the new user application's answers, and whether
// the sysop has let them in. Saved as plain text ([user] blocks of key=value lines) in internal
// flash, so accounts work on a board with no SD card.
#pragma once
#include <cstdint>
#include <string>
#include <map>
#include <utility>
#include <vector>

namespace axis::bbs {

struct User {
    enum class Status { Pending, Validated, Denied };
    int         id = 0;
    std::string handle, salt, hash, realName, location, email;
    Status      status = Status::Pending;
    int         security = 10;
    uint32_t    calls = 0, created = 0, lastCall = 0;  // unix time (0 = clock not set)
    uint32_t    uploads = 0, downloads = 0;            // bytes
    std::vector<std::pair<std::string, std::string>> answers;  // the application: question, answer
};

class UserStore {
public:
    void        parse(const std::string& text);
    std::string format() const;
    User*       find(const std::string& handle);  // any case
    User*       byId(int id);
    User&       add(User u);  // gives it the next id
    User&       upsert(const User& u);  // by handle: replace (keeping this board's id) or add
    const std::vector<User>& all() const { return users_; }
    std::vector<User>&       all() { return users_; }
    size_t      pending() const;

private:
    std::vector<User> users_;
};

const char* statusName(User::Status s);  // "pending", "validated", "denied"

// One user as a users.dat block, for the cluster link. The handle is a user's identity across
// boards; ids are each board's own.
std::string formatUser(const User& u);
bool        parseUser(const std::string& record, User& out);

// Which users changed since the last call (by handle), so a board sends only those over the link.
class UserSync {
public:
    std::vector<std::string> changed(const UserStore& s);  // records to send; remembers them as sent
    void                     received(const std::string& record);  // came in from the link: don't echo it

private:
    std::map<std::string, std::string> sent_;  // lower-case handle -> last record sent or received
};

}  // namespace axis::bbs
