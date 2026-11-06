// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <filesystem>
#include <map>
#include <regex>
#include <string>
#include <vector>

#include "doctest.h"
#include "bbs/msgbase.h"
#include "bbs/msgreader.h"
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
    CHECK(m.body.find("thanks, it lives") != std::string::npos);  // under the quote
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
    r.type("q");  // list, then the area lightbar; Enter would open the highlighted area
    CHECK(r.n.state() == Node::State::Menu);
    CHECK(r.type("s").find("No new messages") != std::string::npos);  // read them all: nothing new
}

TEST_CASE("a reply starts with the message quoted, initials in front, like the old boards") {
    MsgRig r;
    r.type("m");
    r.type("1\r");
    r.type("r1\r");  // read "welcome" from Spindle Kid
    r.type("a");    // reply
    r.type("\r");   // to: Spindle Kid
    r.type("\r");   // subject: Re: welcome
    r.type("right on\r");
    r.type("/s\r");
    Message m;
    REQUIRE(r.st.read("general", 2, m));
    CHECK(m.body.find("SK> glad the board is up") != std::string::npos);
    CHECK(m.body.find("right on") != std::string::npos);
}

TEST_CASE("the sysop can drop posts from an area and the rest stay whole") {
    Dir              d;
    FileMessageStore st(d.p.string());
    st.post("cnc", msg("Rev", "All", "keep me", "line one\nline two"));
    st.post("cnc", msg("Spammer", "All", "buy stuff", "spam"));
    st.post("cnc", msg("Kid", "All", "keep me too", "ok"));
    CHECK(st.drop("cnc", [](const Message& m) { return m.num == 2; }) == 1);
    auto h = st.headers("cnc");
    REQUIRE(h.size() == 2);
    CHECK(h[0].subject == "keep me");
    CHECK(h[1].num == 3);  // numbers don't change
    Message m;
    REQUIRE(st.read("cnc", 1, m));
    CHECK(m.body == "line one\nline two");
    CHECK(st.post("cnc", msg("Rev", "All", "new", "x")) == 4);  // and new ones keep counting up
    CHECK(st.drop("nowhere", [](const Message&) { return true; }) == 0);
}

TEST_CASE("message lightbar: Enter opens the highlighted area and reads the highlighted header") {
    Dir              d;
    FileMessageStore st(d.p.string());
    st.post("general", msg("Spindle Kid", "All", "welcome", "glad the board is up"));
    st.post("general", msg("Troll", "All", "noise", "ignore me"));
    st.post("cnc", msg("Rev", "All", "chips", "flying"));
    std::string              out;
    std::map<std::string, int> last;
    std::vector<FileArea>    areas{{"general", "General Chatter"}, {"cnc", "CNC Shop Talk"}};
    MessageReader            r(areas, st, out, "Rev", last, [] { return std::string("now"); });
    r.setHidden([](const std::string& from) { return from == "Troll"; });
    r.begin();
    CHECK(out.find("General Chatter") != std::string::npos);
    r.key('\r');
    CHECK(out.find("welcome") != std::string::npos);
    CHECK(out.find("Troll") == std::string::npos);
    r.key('q');
    r.key('\x1b');
    r.key('[');
    r.key('B');
    r.key('\r');
    CHECK(r.where().find("CNC") != std::string::npos);
    out.clear();
    r.key('\r');
    CHECK(out.find("flying") != std::string::npos);
    CHECK(out.find("ignore me") == std::string::npos);
}

TEST_CASE("message lightbar: page up reaches an older header, Q returns, text mode still takes a number") {
    Dir              d;
    FileMessageStore st(d.p.string());
    for (int i = 1; i <= 20; i++) st.post("general", msg("Rev", "All", ("note " + std::to_string(i)).c_str(), "body"));
    std::string                out;
    std::map<std::string, int> last;
    std::vector<FileArea>      one{{"general", "General"}};
    MessageReader              r(one, st, out, "Rev", last, [] { return std::string("now"); });
    r.begin();
    r.key('\r');
    out.clear();
    r.key('\x1b');
    r.key('[');
    r.key('A');  // already at the top of the newest window: older page
    CHECK(out.find("note 1") != std::string::npos);
    CHECK(r.key('q'));
    CHECK(r.where() == "message areas");
    CHECK_FALSE(r.key('q'));

    std::string                text;
    std::map<std::string, int> last2;
    MessageReader              plain(one, st, text, "Rev", last2, [] { return std::string("now"); });
    plain.setLightbars(false);
    plain.begin();
    CHECK(text.find("Area number") != std::string::npos);
    CHECK_FALSE(plain.key('\r'));
}
