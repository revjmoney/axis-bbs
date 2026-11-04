// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "cluster/pooledlib.h"

#include <algorithm>
#include <cctype>

namespace axis::cluster {

namespace {
uint32_t hashName(const std::string& n) {  // FNV-1a over the name in capitals (names match in any case)
    uint32_t h = 2166136261u;
    for (char c : n) h = (h ^ (uint8_t)std::toupper((unsigned char)c)) * 16777619u;
    return h;
}
bool sameName(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (std::toupper((unsigned char)a[i]) != std::toupper((unsigned char)b[i])) return false;
    return true;
}
bool usable(const PoolCard& c) { return c.alive && c.files; }
}  // namespace

std::vector<PooledFileLibrary::Placed> PooledFileLibrary::locate(const std::string& areaId, const std::vector<PoolCard>& cards) {
    std::vector<Placed>   out;
    std::vector<uint32_t> hashes;  // one per entry in out: a cheap first test before comparing names
    for (const PoolCard& c : cards) {
        if (!usable(c)) continue;
        for (bbs::FileEntry& e : c.files->list(areaId)) {
            uint32_t h = hashName(e.name);
            size_t   at = out.size();
            for (size_t i = 0; i < hashes.size(); i++)
                if (hashes[i] == h && sameName(out[i].entry.name, e.name)) {
                    at = i;
                    break;
                }
            if (at == out.size()) {  // new to the list: it goes after everything the earlier cards had
                hashes.push_back(h);
                out.push_back({std::move(e), {c.id}});
                continue;
            }
            Placed& p = out[at];
            if (e.size > p.entry.size) {  // a bigger copy: that one is listed
                std::string desc = std::move(p.entry.desc);
                p.entry = std::move(e);
                if (p.entry.desc.empty()) p.entry.desc = std::move(desc);
                p.holders.insert(p.holders.begin(), c.id);
            } else {
                if (p.entry.desc.empty()) p.entry.desc = e.desc;
                p.holders.push_back(c.id);
            }
        }
    }
    return out;
}

std::vector<bbs::FileEntry> PooledFileLibrary::list(const std::string& areaId) {
    std::vector<PoolCard> cs = cards();
    std::vector<Placed>   placed = locate(areaId, cs);
    knownArea_ = areaId;
    knownIds_.clear();
    known_.clear();
    for (const PoolCard& c : cs)
        if (usable(c) && knownIds_.size() < 32) knownIds_.push_back(c.id);
    known_.reserve(placed.size());
    std::vector<bbs::FileEntry> out;
    out.reserve(placed.size());
    for (Placed& p : placed) {
        Known k{hashName(p.entry.name), 0, 0xFF};
        for (const std::string& h : p.holders)
            for (size_t i = 0; i < knownIds_.size(); i++)
                if (knownIds_[i] == h) {
                    k.holders |= 1u << i;
                    if (k.first == 0xFF) k.first = (uint8_t)i;
                }
        known_.push_back(k);
        out.push_back(std::move(p.entry));
    }
    return out;
}

// The cards to try for one file, best first: where it was just put, then the card whose copy was
// listed, then the others that hold it, then every other card (this board's first).
std::vector<PoolCard*> PooledFileLibrary::order(std::vector<PoolCard>& cs, const std::string& areaId, const std::string& name) {
    std::string recent;
    for (const Where& w : recent_)
        if (w.area == areaId && sameName(w.name, name)) {
            recent = w.card;
            break;
        }
    const Known* k = nullptr;
    if (areaId == knownArea_) {
        uint32_t h = hashName(name);
        for (const Known& x : known_)
            if (x.hash == h) {
                k = &x;
                break;
            }
    }
    auto rank = [&](const PoolCard& c) {
        if (!recent.empty() && c.id == recent) return 0;
        if (!k) return 3;
        for (size_t i = 0; i < knownIds_.size(); i++)
            if (knownIds_[i] == c.id) return i == k->first ? 1 : (k->holders >> i & 1) ? 2 : 3;
        return 3;
    };
    std::vector<PoolCard*> v;
    for (PoolCard& c : cs)
        if (usable(c)) v.push_back(&c);
    std::stable_sort(v.begin(), v.end(), [&](PoolCard* a, PoolCard* b) { return rank(*a) < rank(*b); });
    return v;
}

void PooledFileLibrary::placed(const std::string& areaId, const std::string& name, const std::string& card) {
    for (size_t i = 0; i < recent_.size();)
        if (recent_[i].area == areaId && sameName(recent_[i].name, name)) recent_.erase(recent_.begin() + (long)i);
        else i++;
    if (!card.empty()) recent_.insert(recent_.begin(), {areaId, name, card});
    if (recent_.size() > 4) recent_.pop_back();
}

bool PooledFileLibrary::read(const std::string& areaId, const std::string& name, std::string& out, size_t max) {
    std::vector<PoolCard> cs = cards();
    for (PoolCard* c : order(cs, areaId, name))
        if (c->files->read(areaId, name, out, max)) return true;
    out.clear();
    return false;
}

std::unique_ptr<bbs::RandomFile> PooledFileLibrary::open(const std::string& areaId, const std::string& name) {
    std::vector<PoolCard> cs = cards();
    for (PoolCard* c : order(cs, areaId, name))
        if (auto f = c->files->open(areaId, name)) return f;
    return nullptr;
}

std::unique_ptr<bbs::WriteFile> PooledFileLibrary::create(const std::string& areaId, const std::string& name) {
    std::vector<PoolCard>  cs = cards();
    std::vector<PoolCard*> v;
    for (PoolCard& c : cs)
        if (usable(c)) v.push_back(&c);
    std::stable_sort(v.begin(), v.end(), [](PoolCard* a, PoolCard* b) { return a->freeBytes > b->freeBytes; });  // the roomiest first
    for (PoolCard* c : v)
        if (auto w = c->files->create(areaId, name)) {
            placed(areaId, name, c->id);
            return w;
        }
    return nullptr;
}

bool PooledFileLibrary::describe(const std::string& areaId, const std::string& name, const std::string& desc) {
    std::vector<PoolCard> cs = cards();
    for (PoolCard* c : order(cs, areaId, name))  // the card holding it, as far as we know (a listing merges them anyway)
        if (c->files->describe(areaId, name, desc)) return true;
    return false;
}

bool PooledFileLibrary::remove(const std::string& areaId, const std::string& name) {
    std::vector<PoolCard> cs = cards();
    bool                  any = false;
    for (PoolCard& c : cs)
        if (usable(c) && c.files->remove(areaId, name)) any = true;
    placed(areaId, name, "");
    return any;
}

bool PooledFileLibrary::rename(const std::string& areaId, const std::string& from, const std::string& to) {
    std::vector<PoolCard> cs = cards();
    std::string           where;
    for (PoolCard& c : cs)
        if (usable(c) && c.files->rename(areaId, from, to)) where = where.empty() ? c.id : where;
    if (where.empty()) return false;
    placed(areaId, from, "");
    placed(areaId, to, where);
    return true;
}

}  // namespace axis::cluster
