// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The User Net membership tally. See members.h; the rule itself is in statusIn and grow.
#include "net/members.h"

#include <algorithm>
#include <cstddef>
#include <tuple>

#include "net/crc.h"

namespace axis::net {

namespace {

// A board id: exactly 16 lowercase hex digits.
bool parseId(const std::string& s, uint64_t& out) {
    if (s.size() != 16) return false;
    uint64_t v = 0;
    for (char c : s) {
        int d;
        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (c >= 'a' && c <= 'f')
            d = c - 'a' + 10;
        else
            return false;
        v = (v << 4) | static_cast<uint64_t>(d);
    }
    out = v;
    return true;
}

std::string hex16(uint64_t v) {
    static const char* d = "0123456789abcdef";
    std::string s(16, '0');
    for (int i = 15; i >= 0; i--, v >>= 4) s[static_cast<size_t>(i)] = d[v & 15];
    return s;
}

uint32_t lineCrc(const std::string& line) {
    return crc32(reinterpret_cast<const uint8_t*>(line.data()), line.size());
}

// The notice's app seq (admit and deny carry it in decimal); 0 if none.
uint32_t noticeSeq(const NetNotice& n) {
    if (n.value.empty() || n.value.size() > 10) return 0;
    uint64_t v = 0;
    for (char c : n.value) {
        if (c < '0' || c > '9') return 0;
        v = v * 10 + static_cast<uint64_t>(c - '0');
    }
    return v > 0xFFFFFFFFull ? 0 : static_cast<uint32_t>(v);
}

}  // namespace

NetMembers::NetMembers(const NetState& st, std::function<bool(const std::string& board)> isRev)
    : st_(st), isRev_(std::move(isRev)) {}

const NetMembers::App* NetMembers::findApp(uint64_t board) const {
    for (const auto& a : apps_)
        if (a.board == board) return &a;
    return nullptr;
}

// Rev has sealed this app: a veto of the board, or an admit or deny naming this app seq.
bool NetMembers::sealed(uint64_t board, uint32_t appSeq) const {
    const NetNotice* n = st_.memberNotice(hex16(board));
    if (!n) return false;
    if (n->kind == "veto") return true;
    return (n->kind == "admit" || n->kind == "deny") && noticeSeq(*n) == appSeq;
}

// Step 4's seed: Rev's boards and boards Rev admitted, minus banned (a vetoed board has no admit).
bool NetMembers::seedMember(uint64_t board) const {
    const std::string id = hex16(board);
    if (st_.banned(id)) return false;
    if (isRev_ && isRev_(id)) return true;
    const NetNotice* n = st_.memberNotice(id);
    return n && n->kind == "admit";
}

// Step 3's deny: it holds unless the board's held app is newer than the app it named (it applied
// again). With no app held, or an older one, the deny stands.
bool NetMembers::deniedByNotice(uint64_t board) const {
    const NetNotice* n = st_.memberNotice(hex16(board));
    if (!n || n->kind != "deny") return false;
    const App* a = findApp(board);
    return !(a && a->seq > noticeSeq(*n));
}

bool NetMembers::inM(const std::vector<uint64_t>& m, uint64_t board) const {
    return std::binary_search(m.begin(), m.end(), board);
}

// Step 4. M starts as the seed members among the known boards (applicants and voters) and only
// grows, so the loop ends and the result does not depend on the order anything is visited in.
std::vector<uint64_t> NetMembers::grow() const {
    std::vector<uint64_t> known;
    known.reserve(apps_.size() + votes_.size() + outsiders_.size());
    for (const auto& a : apps_) known.push_back(a.board);
    for (const auto& v : votes_) known.push_back(v.voter);
    for (const auto& v : outsiders_) known.push_back(v.voter);
    std::sort(known.begin(), known.end());
    known.erase(std::unique(known.begin(), known.end()), known.end());

    std::vector<uint64_t> m;
    for (uint64_t b : known)
        if (seedMember(b)) m.push_back(b);  // known is sorted, so m is too

    std::vector<const App*> cand;
    for (const auto& a : apps_) {
        if (inM(m, a.board)) continue;
        const std::string id = hex16(a.board);
        if (st_.banned(id) || (isRev_ && isRev_(id)) || deniedByNotice(a.board)) continue;
        const NetNotice* n = st_.memberNotice(id);
        if (n && n->kind == "veto") continue;
        cand.push_back(&a);
    }
    const uint32_t need = st_.votesYes();
    for (bool changed = true; changed;) {
        changed = false;
        for (const App* a : cand) {
            if (inM(m, a->board)) continue;
            if (count(m, *a).yes >= need) {
                m.insert(std::lower_bound(m.begin(), m.end(), a->board), a->board);
                changed = true;
            }
        }
    }
    return m;
}

NetMembers::Tally NetMembers::count(const std::vector<uint64_t>& m, const App& a) const {
    Tally t{0, 0};
    auto add = [&](const std::vector<Vote>& pool) {
        for (const auto& v : pool) {
            if (v.app != a.board || v.appSeq != a.seq || v.voter == a.board || !inM(m, v.voter)) continue;
            if (v.yes)
                t.yes++;
            else
                t.no++;
        }
    };
    add(votes_);
    add(outsiders_);
    return t;
}

// The rule, in one place:
//  1. banned -> None.  2. Rev's board -> Member.
//  3. member notice: veto -> Vetoed; admit (any app seq) -> Member; deny for its latest app seq (or with
//     no newer app held) -> Denied; deny for an older seq falls through (it applied again).
//  4. M (see grow).  5. in M -> Member; no votes from M on its latest app >= votesNo -> Denied;
//     app held -> Pending; otherwise None.
// Yes wins when both counts are reached.
NetMembers::Status NetMembers::statusIn(const std::vector<uint64_t>& m, uint64_t board) const {
    const std::string id = hex16(board);
    if (st_.banned(id)) return Status::None;
    if (isRev_ && isRev_(id)) return Status::Member;
    if (const NetNotice* n = st_.memberNotice(id)) {
        if (n->kind == "veto") return Status::Vetoed;
        if (n->kind == "admit") return Status::Member;
        if (deniedByNotice(board)) return Status::Denied;
    }
    if (inM(m, board)) return Status::Member;
    const App* a = findApp(board);
    if (!a) return Status::None;
    if (count(m, *a).no >= st_.votesNo()) return Status::Denied;
    return Status::Pending;
}

NetMembers::Status NetMembers::status(const std::string& board) const {
    uint64_t b;
    if (!parseId(board, b)) return Status::None;
    return statusIn(grow(), b);
}

NetMembers::Tally NetMembers::tally(const std::string& board) const {
    uint64_t b;
    const App* a = parseId(board, b) ? findApp(b) : nullptr;
    if (!a) return Tally{0, 0};
    return count(grow(), *a);
}

bool NetMembers::takeApp(const NetApp& a, std::string& line) {
    line.clear();
    uint64_t board;
    if (!parseId(a.board, board) || a.seq == 0) return false;
    // Refused only on notice ordered grounds (Rev's board, Rev's admit, Rev's veto). A board that is a
    // member by the tally alone may still send a newer app, and the higher seq wins. Accepted edge: its
    // newer app is taken in either order, but until Rev seals, membership is decided on the newest app.
    if (isRev_ && isRev_(a.board)) return false;
    if (const NetNotice* n = st_.memberNotice(a.board))
        if (n->kind == "admit" || n->kind == "veto") return false;
    App* held = nullptr;
    for (auto& x : apps_)
        if (x.board == board) held = &x;
    if (held && held->seq > a.seq) return false;
    std::string enc = encode(a);
    if (enc.empty()) return false;
    const uint32_t crc = lineCrc(enc);
    if (held && held->seq == a.seq && crc <= held->crc) return false;  // same app, or the losing twin
    if (!verifyApp(a)) return false;

    if (!held && apps_.size() >= kMaxOpenApps) {
        // Make room only from apps Rev has sealed, the oldest (date, then board id) first.
        size_t victim = apps_.size();
        for (size_t i = 0; i < apps_.size(); i++) {
            const App& x = apps_[i];
            if (!sealed(x.board, x.seq)) continue;
            if (victim == apps_.size() || x.date < apps_[victim].date ||
                (x.date == apps_[victim].date && x.board < apps_[victim].board))
                victim = i;
        }
        if (victim == apps_.size()) return false;
        const App gone = apps_[victim];
        apps_.erase(apps_.begin() + static_cast<std::ptrdiff_t>(victim));
        auto drop = [&](std::vector<Vote>& pool) {
            pool.erase(std::remove_if(pool.begin(), pool.end(),
                                      [&](const Vote& v) { return v.app == gone.board && v.appSeq <= gone.seq; }),
                       pool.end());
        };
        drop(votes_);
        drop(outsiders_);
    }
    if (held) {
        held->seq = a.seq;
        held->date = a.date;
        held->crc = crc;
    } else {
        apps_.push_back(App{board, a.seq, a.date, crc});
    }
    // Votes on this board's older apps no longer count for anything.
    auto stale = [&](std::vector<Vote>& pool) {
        pool.erase(std::remove_if(pool.begin(), pool.end(),
                                  [&](const Vote& v) { return v.app == board && v.appSeq < a.seq; }),
                   pool.end());
    };
    stale(votes_);
    stale(outsiders_);
    line = std::move(enc);
    return true;
}

bool NetMembers::takeVote(const NetVote& v, std::string& line) {
    line.clear();
    uint64_t voter, app;
    if (!parseId(v.voter, voter) || !parseId(v.app, app) || voter == app || v.appSeq == 0) return false;
    if (const App* a = findApp(app))
        if (a->seq > v.appSeq) return false;  // on an app this board has already replaced
    std::string enc = encode(v);
    if (enc.empty()) return false;
    const uint32_t crc = lineCrc(enc) & 0x7FFFFFFFu;

    auto findIn = [&](std::vector<Vote>& pool) -> Vote* {
        for (auto& x : pool)
            if (x.voter == voter && x.app == app && x.appSeq == v.appSeq) return &x;
        return nullptr;
    };
    Vote* inMembers = findIn(votes_);
    Vote* inOutsiders = findIn(outsiders_);
    const Vote* old = inMembers ? inMembers : inOutsiders;
    if (old && (v.seq < old->seq || (v.seq == old->seq && crc <= old->crc))) return false;
    if (!verifyVote(v)) return false;

    Vote nv{};
    nv.voter = voter;
    nv.app = app;
    nv.appSeq = v.appSeq;
    nv.seq = v.seq;
    nv.date = v.date;
    nv.crc = crc;
    nv.yes = v.yes ? 1u : 0u;

    const bool member = statusIn(grow(), voter) == Status::Member;
    std::vector<Vote>& pool = member ? votes_ : outsiders_;
    Vote* same = member ? inMembers : inOutsiders;
    if (same) {
        *same = nv;
        line = std::move(enc);
        return true;
    }
    if (member && pool.size() >= kMaxVotes) {
        // Only votes on apps Rev has sealed make room, the oldest first.
        size_t victim = pool.size();
        for (size_t i = 0; i < pool.size(); i++) {
            const Vote& x = pool[i];
            if (!sealed(x.app, x.appSeq)) continue;
            if (victim == pool.size()) {
                victim = i;
                continue;
            }
            const Vote& y = pool[victim];
            if (std::make_tuple(x.date, x.voter, x.app, x.appSeq) < std::make_tuple(y.date, y.voter, y.app, y.appSeq))
                victim = i;
        }
        if (victim == pool.size()) return false;
        pool.erase(pool.begin() + static_cast<std::ptrdiff_t>(victim));
    } else if (!member && pool.size() >= kMaxOutsiderVotes) {
        // The oldest outsider vote goes, by date then fields; if that is the new one, it is refused.
        auto key = [](const Vote& x) {
            return std::make_tuple(x.date, x.voter, x.app, x.appSeq, x.seq, static_cast<uint32_t>(x.crc));
        };
        size_t victim = 0;
        for (size_t i = 1; i < pool.size(); i++)
            if (key(pool[i]) < key(pool[victim])) victim = i;
        if (key(nv) < key(pool[victim])) return false;
        pool.erase(pool.begin() + static_cast<std::ptrdiff_t>(victim));
    }
    // The old vote, if any, sits in the other pool: drop it now that the new one has room.
    if (old) {
        std::vector<Vote>& other = member ? outsiders_ : votes_;
        for (size_t i = 0; i < other.size(); i++)
            if (other[i].voter == voter && other[i].app == app && other[i].appSeq == v.appSeq) {
                other.erase(other.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
    }
    pool.push_back(nv);
    line = std::move(enc);
    return true;
}

std::vector<std::string> NetMembers::members() const {
    std::vector<std::string> rev, others;
    st_.forEachCard([&](const NetCard& c) {
        if (isRev_ && isRev_(c.board) && !st_.banned(c.board)) rev.push_back(c.board);
    });
    st_.forEachMemberNotice([&](const std::string& board, const NetNotice& n) {
        if (n.kind == "admit" && !st_.banned(board) && !(isRev_ && isRev_(board))) others.push_back(board);
    });
    for (uint64_t b : grow()) {
        const std::string id = hex16(b);
        if (isRev_ && isRev_(id))
            rev.push_back(id);
        else
            others.push_back(id);
    }
    for (auto* list : {&rev, &others}) {
        std::sort(list->begin(), list->end());
        list->erase(std::unique(list->begin(), list->end()), list->end());
    }
    rev.insert(rev.end(), others.begin(), others.end());
    return rev;
}

std::vector<std::string> NetMembers::waitingFor(const std::string& voter) const {
    std::vector<std::string> out;
    uint64_t me;
    if (!parseId(voter, me)) return out;
    const std::vector<uint64_t> m = grow();
    std::vector<const App*> list;
    auto voted = [&](const App& a) {
        for (const auto* pool : {&votes_, &outsiders_})
            for (const auto& v : *pool)
                if (v.voter == me && v.app == a.board && v.appSeq == a.seq) return true;
        return false;
    };
    for (const auto& a : apps_)
        if (a.board != me && statusIn(m, a.board) == Status::Pending && !voted(a)) list.push_back(&a);
    std::sort(list.begin(), list.end(), [](const App* x, const App* y) {
        return std::make_tuple(x->seq, x->date, x->board) < std::make_tuple(y->seq, y->date, y->board);
    });
    for (const App* a : list) out.push_back(hex16(a->board));
    return out;
}

std::vector<std::pair<std::string, NetMembers::Status>> NetMembers::sealsDue() const {
    std::vector<std::pair<std::string, Status>> out;
    const std::vector<uint64_t> m = grow();
    for (const auto& a : apps_) {
        const std::string id = hex16(a.board);
        if (isRev_ && isRev_(id)) continue;
        const Status s = statusIn(m, a.board);
        if (s != Status::Member && s != Status::Denied) continue;
        // Due when no notice decided it: none at all, or a deny for an older app (it applied again).
        const NetNotice* n = st_.memberNotice(id);
        if (n && !(n->kind == "deny" && !deniedByNotice(a.board))) continue;
        out.emplace_back(id, s);
    }
    std::sort(out.begin(), out.end(),
              [](const std::pair<std::string, Status>& x, const std::pair<std::string, Status>& y) {
                  return x.first < y.first;
              });
    return out;
}

// pool: 0 apps, 1 member votes, 2 outsider votes.
bool NetMembers::locate(const std::string& line, int& pool, size_t& index) const {
    if (line.empty()) return false;
    if (line[0] == static_cast<char>(NetKind::App)) {
        NetApp a;
        uint64_t board;
        if (!decode(line, a) || !parseId(a.board, board)) return false;
        const std::string enc = encode(a);
        if (enc.empty()) return false;
        const uint32_t crc = lineCrc(enc);
        for (size_t i = 0; i < apps_.size(); i++)
            if (apps_[i].board == board && apps_[i].seq == a.seq && apps_[i].crc == crc) {
                pool = 0;
                index = i;
                return true;
            }
        return false;
    }
    if (line[0] == static_cast<char>(NetKind::Vote)) {
        NetVote v;
        uint64_t voter, app;
        if (!decode(line, v) || !parseId(v.voter, voter) || !parseId(v.app, app)) return false;
        const std::string enc = encode(v);
        if (enc.empty()) return false;
        const uint32_t crc = lineCrc(enc) & 0x7FFFFFFFu;
        const std::vector<Vote>* pools[2] = {&votes_, &outsiders_};
        for (int p = 0; p < 2; p++)
            for (size_t i = 0; i < pools[p]->size(); i++) {
                const Vote& x = (*pools[p])[i];
                if (x.voter == voter && x.app == app && x.appSeq == v.appSeq && x.seq == v.seq && x.crc == crc &&
                    (x.yes != 0) == v.yes) {
                    pool = p + 1;
                    index = i;
                    return true;
                }
            }
    }
    return false;
}

bool NetMembers::holds(const std::string& line) const {
    int pool;
    size_t index;
    return locate(line, pool, index);
}

namespace {

// Calls f on each line of text, without a trailing carriage return.
template <typename F>
void eachLine(const std::string& text, F f) {
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        size_t len = end - pos;
        if (len && text[pos + len - 1] == '\r') len--;
        if (len) f(text.substr(pos, len));
        pos = end + 1;
    }
}

}  // namespace

std::string NetMembers::save(const std::string& oldText) const {
    std::string out;
    std::vector<bool> done[3] = {std::vector<bool>(apps_.size()), std::vector<bool>(votes_.size()),
                                 std::vector<bool>(outsiders_.size())};
    eachLine(oldText, [&](const std::string& line) {
        int pool;
        size_t index;
        if (!locate(line, pool, index) || done[pool][index]) return;
        done[pool][index] = true;
        out += line;
        out += '\n';
    });
    return out;
}

void NetMembers::load(const std::string& text) {
    apps_.clear();
    votes_.clear();
    outsiders_.clear();
    std::string line;
    // Applications first, then votes, so a vote never waits on an app later in the file.
    eachLine(text, [&](const std::string& l) {
        NetApp a;
        if (l[0] == static_cast<char>(NetKind::App) && decode(l, a)) takeApp(a, line);
    });
    eachLine(text, [&](const std::string& l) {
        NetVote v;
        if (l[0] == static_cast<char>(NetKind::Vote) && decode(l, v)) takeVote(v, line);
    });
    // A voter can turn out a member only after its own app's votes are in: move its votes over.
    const std::vector<uint64_t> m = grow();
    for (size_t i = 0; i < outsiders_.size() && votes_.size() < kMaxVotes;) {
        if (inM(m, outsiders_[i].voter)) {
            votes_.push_back(outsiders_[i]);
            outsiders_.erase(outsiders_.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            i++;
        }
    }
}

}  // namespace axis::net
