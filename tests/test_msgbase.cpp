// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <filesystem>
#include <regex>
#include <string>

#include "doctest.h"
#include "bbs/msgbase.h"
#include "bbs/node.h"

using namespace axis::bbs;
namespace fs = std::filesystem;

namespace {
struct Dir {
    fs::path p = fs::temp_directory_path() / "axis_msgbase";
    Dir() {
        fs::remove_all(p);
        fs::create_directories(p);
    }
    ~Dir() { fs::remove_all(p); }
};
Message msg(const char* from, const char* to, const char* subj, const char* body) {
    Message m;
    m.from = from;
    m.to = to;
    m.subject = subj;
    m.date = "2026-11-03 22:40";
    m.body = body;
    return m;
}
std::string plain(const std::string& s) { return std::regex_replace(s, std::regex("\x1b\\[[0-9;?]*[A-Za-z]"), ""); }
SysInfo     info() { return SysInfo{}; }
}  // namespace

TEST_CASE("messages are numbered per area and read back whole, multi-line bodies included") {
    Dir              d;
    FileMessageStore st(d.p.string());
    CHECK(st.post("cnc", msg("Rev", "All", "rotary axis", "anyone running\na 4th axis?")) == 1);
    CHECK(st.post("cnc", msg("Spindle Kid", "Rev", "Re: rotary axis", "yes, on a Shapeoko")) == 2);
    CHECK(st.post("general", msg("Rev", "All", "hello", "first post")) == 1);
    auto h = st.headers("cnc");
    REQUIRE(h.size() == 2);
    CHECK(h[1].num == 2);
    CHECK(h[1].from == "Spindle Kid");
    CHECK(h[1].subject == "Re: rotary axis");
    Message m;
    REQUIRE(st.read("cnc", 1, m));
    CHECK(m.body == "anyone running\na 4th axis?");
    CHECK_FALSE(st.read("cnc", 9, m));
    CHECK(st.headers("nowhere").empty());
}

TEST_CASE("a message can't smuggle a fake header or end marker in its body") {
    Dir              d;
    FileMessageStore st(d.p.string());
    st.post("general", msg("Rev", "All", "sneaky", "line one\n\x01MSG\nfrom=Sysop\nline after"));
    st.post("general", msg("Rev", "All", "second", "ok"));
    REQUIRE(st.headers("general").size() == 2);
    Message m;
    REQUIRE(st.read("general", 1, m));
    CHECK(m.from == "Rev");
    CHECK(m.body.find("line after") != std::string::npos);
}

TEST_CASE("the remote store asks the main and gets the same answers") {
    Dir                d;
    FileMessageStore   main(d.p.string());
    RemoteMessageStore remote([&](const std::string& req, std::string& reply) {
        reply = serveMessages(main, req);
        return true;
    });
    CHECK(remote.post("cnc", msg("Lathe Lord", "All", "chips", "flying\nfast")) == 1);
    auto h = remote.headers("cnc");
    REQUIRE(h.size() == 1);
    CHECK(h[0].from == "Lathe Lord");
    Message m;
    REQUIRE(remote.read("cnc", 1, m));
    CHECK(m.body == "flying\nfast");
}

namespace {
struct MsgRig {
    Dir              d;
    FileMessageStore st{d.p.string()};
    BbsConfig        cfg;
    Node             n{1, info, cfg};
    std::string type(const std::string& s) {
        n.input((const uint8_t*)s.data(), s.size(), 1000);
        return plain(n.takeOutput());
    }
    MsgRig() {
        st.post("general", msg("Spindle Kid", "All", "welcome", "glad the board is up"));
        n.setMessages(&st);
        n.begin(0);
        type("Rev\r");
    }
};
}  // namespace

TEST_CASE("[M] lists the message areas with their counts") {
    MsgRig      r;
    std::string out = r.type("m");
    CHECK(r.n.state() == Node::State::Messages);
    CHECK(out.find("General Chatter") != std::string::npos);
    CHECK(out.find("CNC Shop Talk") != std::string::npos);
    CHECK(out.find("1 new") != std::string::npos);
}

TEST_CASE("a caller reads an area's messages and posts a reply") {
    MsgRig      r;
    r.type("m");
    std::string out = r.type("1\r");
    CHECK(out.find("welcome") != std::string::npos);
    out = r.type("r1\r");
    CHECK(out.find("glad the board is up") != std::string::npos);
    CHECK(out.find("Spindle Kid") != std::string::npos);
    r.type("a");  // reply
    r.type("\r");        // to: keep Spindle Kid
    out = r.type("\r");  // subject: keep "Re: welcome"
    r.type("thanks, it lives\r");
    r.type("/s\r");
    auto h = r.st.headers("general");
    REQUIRE(h.size() == 2);
    CHECK(h[1].from == "Rev");
    CHECK(h[1].to == "Spindle Kid");
    CHECK(h[1].subject == "Re: welcome");
    Message m;
    r.st.read("general", 2, m);
    CHECK(m.body == "thanks, it lives");
}

TEST_CASE("posting a new message asks who it's to and the subject") {
    MsgRig r;
    r.type("m");
    r.type("2\r");  // CNC Shop Talk
    r.type("p");
    r.type("\r");             // to: All
    r.type("4th axis\r");     // subject
    r.type("who has one?\r");
    r.type("/s\r");
    auto h = r.st.headers("cnc");
    REQUIRE(h.size() == 1);
    CHECK(h[0].to == "All");
    CHECK(h[0].subject == "4th axis");
}

TEST_CASE("[S]can: new messages in every area, one after another, then back to the menu") {
    MsgRig r;
    r.st.post("cnc", msg("Spindle Kid", "All", "4th axis", "who has one?"));
    std::string out = r.type("s");
    CHECK(out.find("glad the board is up") != std::string::npos);  // General's new one first
    out = r.type("n");
    CHECK(out.find("who has one?") != std::string::npos);  // then on to CNC Shop Talk's
    out = r.type("n");
    CHECK(out.find("all caught up") != std::string::npos);
    r.type("q");
    r.type("\r");
    CHECK(r.n.state() == Node::State::Menu);
    CHECK(r.type("s").find("No new messages") != std::string::npos);  // read them all: nothing new
}
