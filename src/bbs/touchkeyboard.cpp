// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bbs/touchkeyboard.h"

#include <cctype>

namespace axis::bbs {

namespace {
const char* const kLetters[4] = {"1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm._@"};
const char* const kSymbols[4] = {"!@#$%^&*()", "~`+=[]{}\\|", ";:'\"<>?/,_", "0123456789"};
constexpr int     kKeyTop = 64, kKeyH = 30, kKeyW = 32, kBottom = 196;
}  // namespace

void TouchKeyboard::reset(const std::string& title, bool secret) {
    title_ = title;
    secret_ = secret;
    text_.clear();
    show_ = shift_ = symbols_ = done_ = cancelled_ = false;
}

std::vector<TouchKeyboard::Key> TouchKeyboard::keys() const {
    std::vector<Key> k;
    k.push_back({"BACK", 0, 0, 64, 24});
    if (secret_) k.push_back({"SHOW", 256, 0, 64, 24, show_});
    for (int r = 0; r < 4; r++) {
        const char* row = symbols_ ? kSymbols[r] : kLetters[r];
        for (int i = 0; row[i]; i++) {
            char c = row[i];
            if (shift_ && !symbols_) c = (char)std::toupper((unsigned char)c);
            k.push_back({std::string(1, c), i * kKeyW, kKeyTop + r * (kKeyH + 2), kKeyW - 2, kKeyH});
        }
    }
    k.push_back({"SHIFT", 0, kBottom, 62, 40, shift_});
    k.push_back({"#+=", 64, kBottom, 62, 40, symbols_});
    k.push_back({"SPACE", 128, kBottom, 62, 40});
    k.push_back({"DEL", 192, kBottom, 62, 40});
    k.push_back({"OK", 256, kBottom, 62, 40});
    return k;
}

std::string TouchKeyboard::shown() const {
    if (!secret_ || show_) return text_;
    return std::string(text_.size(), '*');
}

void TouchKeyboard::tap(int x, int y) {
    for (const Key& key : keys()) {
        if (x < key.x || x >= key.x + key.w || y < key.y || y >= key.y + key.h) continue;
        const std::string& l = key.label;
        if (l == "BACK") cancelled_ = true;
        else if (l == "SHOW") show_ = !show_;
        else if (l == "SHIFT") shift_ = !shift_;
        else if (l == "#+=") symbols_ = !symbols_;
        else if (l == "DEL") {
            if (!text_.empty()) text_.pop_back();
        } else if (l == "OK") done_ = true;
        else if (l == "SPACE") {
            if (text_.size() < max_) text_ += ' ';
        } else if (text_.size() < max_) {
            text_ += l;
            shift_ = false;  // one capital at a time, like a phone
        }
        return;
    }
}

}  // namespace axis::bbs
