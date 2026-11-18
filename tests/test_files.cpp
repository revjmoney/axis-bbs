// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bbs/features.h"
#if AXIS_FILES  // a build that leaves the file areas out has nothing here to test
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <string>
#include <vector>

#include "doctest.h"
#include "bbs/filebrowser.h"
#include "bbs/filelib.h"
#include "bbs/node.h"

using namespace axis::bbs;

namespace {
// a pretend file store: area id -> files (name, contents); FILES.BBS is generated from desc
struct FakeLibrary : FileLibrary {
    std::map<std::string, std::vector<FileEntry>> areas;
    std::map<std::string, std::string>            text;  // "area/name" -> contents
    std::vector<FileEntry> list(const std::string& area) override { return areas[area]; }
    bool read(const std::string& area, const std::string& name, std::string& out, size_t max) override {
        auto it = text.find(area + "/" + name);
        if (it == text.end()) return false;
        out = it->second.substr(0, max);
        return true;
    }
};

SysInfo info() { return SysInfo{}; }

BbsConfig twoAreas() {
    BbsConfig c;
    c.areas = {{"axis", "Axis Micro BBS Firmware"}, {"text", "Text Files"}};
    return c;
}

FakeLibrary library() {
    FakeLibrary l;
    l.areas["axis"] = {{"AXISFLSH.ZIP", 797000, "Ready to flash"}, {"README.TXT", 1200, "What it is"}};
    for (int i = 0; i < 20; i++) l.areas["text"].push_back({"LIST" + std::to_string(i) + ".TXT", 100u + i, "BBS list " + std::to_string(i)});
    l.text["axis/README.TXT"] = "Axis Micro BBS Firmware\nby Rev. J. Money\n";
    std::string longText;
    for (int i = 1; i <= 50; i++) longText += "line " + std::to_string(i) + "\n";
    l.text["text/LIST0.TXT"] = longText;
    return l;
}

std::string plain(const std::string& s) { return std::regex_replace(s, std::regex("\x1b\\[[0-9;]*m"), ""); }

void type(Node& n, const std::string& s) { n.input((const uint8_t*)s.data(), s.size(), 1000); }

Node inFiles(FakeLibrary& lib) {
    Node n(1, info, twoAreas(), &lib);
    n.begin(0);
    type(n, "Rev\r");
    n.takeOutput();
    return n;
}
}  // namespace

TEST_CASE("FILES.BBS order and descriptions merge with the folder") {
    std::string bbs = "README.TXT   What it is\r\nGONE.ZIP     Not in the folder\r\nAXIS.ZIP     The firmware\r\n";
    auto list = mergeFilesBbs(bbs, {{"axis.zip", 900}, {"FILES.BBS", 80}, {"readme.txt", 12}, {"EXTRA.BIN", 5}});
    REQUIRE(list.size() == 3);
    CHECK(list[0].name == "readme.txt");  // the folder's spelling
    CHECK(list[0].desc == "What it is");
    CHECK(list[0].size == 12);
    CHECK(list[1].name == "axis.zip");
    CHECK(list[2].name == "EXTRA.BIN");  // not described: listed last, no description
    CHECK(list[2].desc.empty());
}

TEST_CASE("file sizes read like a 90s file list") {
    CHECK(fileSizeText(512) == "512");
    CHECK(fileSizeText(48 * 1024) == "48K");
    CHECK(fileSizeText(21972477) == "21.0M");
}

TEST_CASE("a movie pack keeps the word movies inside a short file row") {
    const char* desc = "Blocktronics: Detention Block AA-23, Star Wars (2017): ANSI movies inside [16colo.rs]";
    std::string row = fileAbout(desc, 48);
    CHECK(row.find("ANSI movies") == 0);
    CHECK(row.find("Detention Block AA-23") != std::string::npos);
    CHECK(row.size() <= 48);
    CHECK(fileAbout("Blocktronics: 1980 (2012)", 40) == "Blocktronics: 1980 (2012)");
    CHECK(fileAbout("Short ANSI movies", 48) == "Short ANSI movies");
}

TEST_CASE("menu shows Files") {
    FakeLibrary lib = library();
    Node        n(1, info, twoAreas(), &lib);
    n.begin(0);
    type(n, "Rev\r");
    CHECK(plain(n.takeOutput()).find("[F]") != std::string::npos);
}

TEST_CASE("F lists the file areas with their file counts") {
    FakeLibrary lib = library();
    Node        n = inFiles(lib);
    type(n, "f");
    std::string out = n.takeOutput();
    CHECK(out.find("Axis Micro BBS Firmware") != std::string::npos);
    CHECK(out.find("Text Files") != std::string::npos);
    CHECK(out.find("20") != std::string::npos);
    CHECK(n.state() == Node::State::Files);
}

TEST_CASE("choosing an area lists its files with size and description") {
    FakeLibrary lib = library();
    Node        n = inFiles(lib);
    type(n, "f1\r");
    std::string out = n.takeOutput();
    CHECK(out.find("AXISFLSH.ZIP") != std::string::npos);
    CHECK(out.find("778K") != std::string::npos);
    CHECK(out.find("Ready to flash") != std::string::npos);
}

TEST_CASE("long areas page with N and P") {
    FakeLibrary lib = library();
    Node        n = inFiles(lib);
    type(n, "f2\r");
    std::string first = n.takeOutput();
    CHECK(first.find("LIST0.TXT") != std::string::npos);
    CHECK(first.find("LIST19.TXT") == std::string::npos);
    type(n, "n");
    std::string second = n.takeOutput();
    CHECK(second.find("LIST19.TXT") != std::string::npos);
    type(n, "p");
    CHECK(n.takeOutput().find("LIST0.TXT") != std::string::npos);
}

TEST_CASE("V shows a text file") {
    FakeLibrary lib = library();
    Node        n = inFiles(lib);
    type(n, "f1\r");
    n.takeOutput();
    type(n, "vreadme.txt\r");
    std::string out = n.takeOutput();
    CHECK(out.find("by Rev. J. Money\r\n") != std::string::npos);  // bare LF becomes CR LF
}

TEST_CASE("a long text file pages, and Q stops it") {
    FakeLibrary lib = library();
    Node        n = inFiles(lib);
    type(n, "f2\r");
    n.takeOutput();
    type(n, "vLIST0.TXT\r");
    std::string out = n.takeOutput();
    CHECK(out.find("line 1\r\n") != std::string::npos);
    CHECK(out.find("line 50") == std::string::npos);
    CHECK(out.find("more") != std::string::npos);
    type(n, "q");
    CHECK(n.takeOutput().find("LIST0.TXT") != std::string::npos);  // back at the file list
}

TEST_CASE("viewing a binary file explains downloads") {
    FakeLibrary lib = library();
    Node        n = inFiles(lib);
    type(n, "f1\r");
    n.takeOutput();
    type(n, "vAXISFLSH.ZIP\r");
    CHECK(plain(n.takeOutput()).find(AXIS_TRANSFERS ? "ownload" : "[I]nfo shows what is in it") != std::string::npos);
}

TEST_CASE("Q leaves the list and Enter leaves the areas") {
    FakeLibrary lib = library();
    Node        n = inFiles(lib);
    type(n, "f1\r");
    type(n, "q");
    CHECK(n.state() == Node::State::Files);
    type(n, "q");  // Q from the area lightbar leaves; Enter opens the highlighted area
    CHECK(n.state() == Node::State::Menu);
}

TEST_CASE("files without a library say the areas are offline") {
    Node n(1, info, twoAreas());
    n.begin(0);
    type(n, "Rev\r");
    n.takeOutput();
    type(n, "f");
    CHECK(n.takeOutput().find("offline") != std::string::npos);
    CHECK(n.state() == Node::State::Menu);
}

#if AXIS_TRANSFERS  // (downloads and uploads)
TEST_CASE("D asks for a file and a protocol, then shows the download window and requests it") {
    FakeLibrary lib = library();
    Node        n = inFiles(lib);
    type(n, "f1\r");
    n.takeOutput();
    type(n, "dAXISFLSH.ZIP\r");
    CHECK(plain(n.takeOutput()).find("[Z]MODEM") != std::string::npos);  // the protocol menu
    type(n, "z");
    std::string out = plain(n.takeOutput());
    CHECK(out.find("D O W N L O A D") != std::string::npos);
    CHECK(n.state() == Node::State::Transfer);
    CHECK_FALSE(n.transfer().upload);
    CHECK(n.transfer().name == "AXISFLSH.ZIP");
    CHECK(n.transfer().area == "axis");
    CHECK(n.transfer().proto == 'Z');
}

TEST_CASE("U asks for a description and a protocol, then requests the upload") {
    FakeLibrary lib = library();
    Node        n = inFiles(lib);
    type(n, "f2\r");
    n.takeOutput();
    type(n, "uA list of boards\rz");
    CHECK(n.state() == Node::State::Transfer);
    CHECK(n.transfer().upload);
    CHECK(n.transfer().desc == "A list of boards");
    CHECK(n.transfer().area == "text");
}

TEST_CASE("an XMODEM upload also asks for the file name") {
    FakeLibrary lib = library();
    Node        n = inFiles(lib);
    type(n, "f2\r");
    type(n, "unotes\rknotes.txt\r");
    CHECK(n.state() == Node::State::Transfer);
    CHECK(n.transfer().proto == 'K');
    CHECK(n.transfer().name == "notes.txt");
}

TEST_CASE("after a transfer the caller sees how it went and is back at the list") {
    FakeLibrary lib = library();
    Node        n = inFiles(lib);
    type(n, "f1\rdREADME.TXT\rz");
    n.takeOutput();
    n.transferDone(true, 1200, 2000, "", 0);
    std::string out = plain(n.takeOutput());
    CHECK(out.find("C O M P L E T E") != std::string::npos);
    CHECK(n.state() == Node::State::Files);
    CHECK(out.find("ownload") != std::string::npos);  // the list prompt is back
}

TEST_CASE("a download of a missing file is refused before any protocol starts") {
    FakeLibrary lib = library();
    Node        n = inFiles(lib);
    type(n, "f1\rdNOPE.ZIP\r");
    CHECK(n.state() == Node::State::Files);
    CHECK(n.takeOutput().find("No file by that name") != std::string::npos);
}
#endif  // AXIS_TRANSFERS

namespace {
FakeLibrary withZip() {
    FakeLibrary   l = library();
    std::ifstream in(std::string(AXIS_TEST_DATA) + "/pack.zip", std::ios::binary);
    std::string   zip((std::istreambuf_iterator<char>(in)), {});
    l.areas["axis"].push_back({"PACK.ZIP", (uint32_t)zip.size(), "Test art pack"});
    l.text["axis/PACK.ZIP"] = zip;
    return l;
}
}  // namespace

TEST_CASE("[I] shows a ZIP's full archive listing while browsing") {
    FakeLibrary lib = withZip();
    Node        n = inFiles(lib);
    type(n, "f1\r");
    std::string list = plain(n.takeOutput());
    CHECK(list.find("[I]") != std::string::npos);
    type(n, "ipack.zip\r");
    std::string out = plain(n.takeOutput());
    CHECK(out.find("PACK.ZIP") != std::string::npos);
    CHECK(out.find("Test art pack") != std::string::npos);
    CHECK(out.find("REV-AXIS.ANS") != std::string::npos);
    CHECK(out.find("FILE_ID.DIZ") != std::string::npos);
    CHECK(out.find("PROGRAM.EXE") != std::string::npos);
    CHECK(out.find("3 files") != std::string::npos);
    CHECK(out.find("3703") != std::string::npos);  // REV-AXIS.ANS unpacked size
}

TEST_CASE("[I] lists a TAR's files too, and says the board only lists that kind") {
    FakeLibrary lib = library();
    std::string h(512, '\0'), body = "greetz to the underground\n";
    std::memcpy(&h[0], "GREETZ.TXT", 10);
    std::memcpy(&h[100], "0000644", 7);
    std::snprintf(&h[124], 12, "%011o", (unsigned)body.size());
    h[156] = '0';
    std::memset(&h[148], ' ', 8);
    unsigned sum = 0;
    for (unsigned char c : h) sum += c;
    std::snprintf(&h[148], 8, "%06o", sum);
    std::string tar = h + body + std::string(512 - body.size(), '\0') + std::string(1024, '\0');
    lib.areas["axis"].push_back({"GREETZ.TAR", (uint32_t)tar.size(), "Greetz"});
    lib.text["axis/GREETZ.TAR"] = tar;
    Node n = inFiles(lib);
    type(n, "f1\r");
    n.takeOutput();
    type(n, "igreetz.tar\r");
    std::string out = plain(n.takeOutput());
    CHECK(out.find("TAR, 1 files") != std::string::npos);
    CHECK(out.find("GREETZ.TXT") != std::string::npos);
    CHECK(out.find("1 files, 26 bytes") != std::string::npos);
}

TEST_CASE("[I] on a file that isn't an archive shows what is known about it") {
    FakeLibrary lib = withZip();
    Node        n = inFiles(lib);
    type(n, "f1\r");
    n.takeOutput();
    type(n, "iREADME.TXT\r");
    std::string out = plain(n.takeOutput());
    CHECK(out.find("README.TXT") != std::string::npos);
    CHECK(out.find("What it is") != std::string::npos);
    type(n, "inope.zip\r");
    CHECK(plain(n.takeOutput()).find("No file by that name") != std::string::npos);
}

TEST_CASE("a node says where its caller is, for the sysop's activity screen") {
    FakeLibrary lib = library();
    Node        n = inFiles(lib);
    CHECK(n.activityText() == "main menu");
    type(n, "f");
    CHECK(n.activityText() == "files: area list");
    type(n, "2\r");
    CHECK(n.activityText() == "files: Text Files");
    if (!AXIS_TRANSFERS) return;  // (no downloads in this firmware)
    type(n, "d");
    type(n, "LIST1.TXT\r");
    type(n, "z");
    CHECK(n.activityText() == "downloading LIST1.TXT");
}

TEST_CASE("[N] at the area list shows every area's files newer than the caller's last call") {
    FakeLibrary lib = library();
    lib.areas["axis"][0].date = 2000;  // AXISFLSH.ZIP: new since the last call
    lib.areas["axis"][1].date = 500;   // README.TXT: old
    lib.areas["text"][3].date = 3000;  // LIST3.TXT: new
    std::string out;
    BbsConfig   c = twoAreas();
    FileBrowser b(c.areas, lib, out);
    b.setNewSince(1000);
    b.begin();
    out.clear();
    b.key('n');
    std::string p = plain(out);
    CHECK(p.find("AXISFLSH.ZIP") != std::string::npos);
    CHECK(p.find("LIST3.TXT") != std::string::npos);
    CHECK(p.find("README.TXT") == std::string::npos);
    CHECK(p.find("2 new files") != std::string::npos);
}

TEST_CASE("/ at the area list searches every area's names and descriptions") {
    FakeLibrary lib = library();
    std::string out;
    BbsConfig   c = twoAreas();
    FileBrowser b(c.areas, lib, out);
    b.begin();
    out.clear();
    b.key('/');
    for (char ch : std::string("flash\r")) b.key(ch);  // "Ready to flash" is AXISFLSH.ZIP's description
    std::string p = plain(out);
    CHECK(p.find("AXISFLSH.ZIP") != std::string::npos);
    CHECK(p.find("Axis Micro BBS Firmware") != std::string::npos);  // which area it's in
    CHECK(p.find("1 found") != std::string::npos);
    out.clear();
    b.key('/');
    for (char ch : std::string("list1\r")) b.key(ch);  // names too: LIST1.TXT, LIST10 ... LIST19
    CHECK(plain(out).find("LIST15.TXT") != std::string::npos);
}

static void down(FileBrowser& b) {
    b.key('\x1b');
    b.key('[');
    b.key('B');
}

TEST_CASE("file lightbar: Enter opens the highlighted area and then the highlighted file") {
    FakeLibrary lib = library();
    BbsConfig   cfg = twoAreas();
    std::string out;
    FileBrowser b(cfg.areas, lib, out);
    b.begin();
    down(b);  // Text Files
    b.key('\r');
    CHECK(b.where() == "Text Files");
    out.clear();
    b.key('\r');  // LIST0.TXT, a text file
    CHECK(out.find("line 1") != std::string::npos);
    CHECK_FALSE(b.wantsTransfer());
}

TEST_CASE("file areas are a classic list: title bar, columns, and the highlight's status line") {
    FakeLibrary lib = library();
    BbsConfig   cfg = twoAreas();
    std::string out;
    FileBrowser b(cfg.areas, lib, out);
    b.begin();
    std::string p = plain(out);
    CHECK(p.find("FILE AREAS") != std::string::npos);
    CHECK(p.find("Files") != std::string::npos);
    CHECK(out.find("\x1b[21;1H") != std::string::npos);
    out.clear();
    down(b);
    CHECK(out.find("\x1b[21;1H") != std::string::npos);
    CHECK(plain(out).find("Text Files") != std::string::npos);
}

#if AXIS_TRANSFERS
TEST_CASE("file lightbar: Enter on a binary starts that file's download, and Q goes back") {
    FakeLibrary lib = library();
    BbsConfig   cfg = twoAreas();
    std::string out;
    FileBrowser b(cfg.areas, lib, out);
    b.begin();
    b.key('\r');  // Axis area
    CHECK(b.where() == "Axis Micro BBS Firmware");
    b.key('\r');  // AXISFLSH.ZIP
    CHECK(plain(out).find("[Z]MODEM") != std::string::npos);
    b.key('z');
    CHECK(b.wantsTransfer());
    CHECK(b.transfer().name == "AXISFLSH.ZIP");
    CHECK(b.transfer().area == "axis");
}
#else
TEST_CASE("file lightbar without transfers: Enter on a binary shows what is known about it, no protocol") {
    FakeLibrary lib = library();
    BbsConfig   cfg = twoAreas();
    std::string out;
    FileBrowser b(cfg.areas, lib, out);
    b.begin();
    b.key('\r');  // Axis area
    out.clear();
    b.key('\r');  // AXISFLSH.ZIP
    CHECK(plain(out).find("[Z]MODEM") == std::string::npos);
    CHECK(plain(out).find("AXISFLSH.ZIP") != std::string::npos);
    CHECK_FALSE(b.wantsTransfer());
}
#endif

TEST_CASE("file lightbar: moving past the page shows the next files, and an empty area's Enter does nothing") {
    FakeLibrary lib = library();
    BbsConfig   cfg = twoAreas();
    std::string out;
    FileBrowser b(cfg.areas, lib, out);
    b.begin();
    down(b);
    b.key('\r');
    out.clear();
    for (int i = 0; i < 15; i++) down(b);  // off the end of the first page
    CHECK(plain(out).find("LIST15.TXT") != std::string::npos);
    CHECK(b.key('q'));
    CHECK(b.where() == "area list");
    CHECK_FALSE(b.key('q'));

    FakeLibrary empty;
    empty.areas["axis"] = {};
    std::string           o2;
    std::vector<FileArea> emptyAreas{{"axis", "Empty Area"}};
    FileBrowser           e(emptyAreas, empty, o2);
    e.begin();
    e.key('\r');
    CHECK(e.where() == "Empty Area");
    CHECK(e.key('\r'));
    CHECK_FALSE(e.wantsTransfer());
}

TEST_CASE("file lightbars off keep the area-number prompt, and a browser only lists areas it was given") {
    FakeLibrary lib = library();
    BbsConfig   cfg = twoAreas();
    std::string out;
    FileBrowser b(cfg.areas, lib, out);
    b.setLightbars(false);
    b.begin();
    CHECK(out.find("Area number") != std::string::npos);
    CHECK_FALSE(b.key('\r'));

    std::string           one;
    std::vector<FileArea> justText{cfg.areas[1]};
    FileBrowser           only(justText, lib, one);
    only.begin();
    CHECK(one.find("Text Files") != std::string::npos);
    CHECK(one.find("Axis Micro BBS Firmware") == std::string::npos);
    only.key('\r');
    CHECK(only.where() == "Text Files");
}

TEST_CASE("Esc on the file-area lightbar leaves after a short pause") {
    FakeLibrary lib = library();
    Node        n = inFiles(lib);
    type(n, "f");
    type(n, "\x1b");
    n.poll(1000);
    CHECK(n.state() == Node::State::Files);
    n.poll(1400);
    CHECK(n.state() == Node::State::Menu);
}

TEST_CASE("G-code is text: a caller can view it") {
    for (const char* n : {"PAWN4X.NC", "revj.gc", "PART.NGC", "x.gcode", "y.TAP", "z.cnc", "README.TXT"}) CHECK(axis::bbs::isTextFile(n));
    for (const char* n : {"GAME.ZIP", "LORD.EXE", "MODEL.STL", "NOEXT"}) CHECK_FALSE(axis::bbs::isTextFile(n));
}

#if AXIS_NET_FILES
// Net Files in the caller's browser, with a host view that records what it was asked.
namespace {
struct FakeNet {
    struct Row {
        std::string id, name, desc, home;
        uint32_t    size;
    };
    std::vector<Row> rows;
    int              pages = 0, begins = 0, polls = 0, releases = 0, unreachables = 0;
    int              shortOfMemory = 0;  // page answers kShort
    int32_t          bps = -1;
    std::map<std::string, std::string> hostless;  // id -> the unreachable text
    int              beginAnswer = NetFilesView::kStarted;
    std::string      ownArea, ownName;  // begin's kOwn: the local file
    std::vector<int> pollAnswers;       // 0 connecting, 1 ready, -1 failed; the last repeats
    uint32_t         pollSize = 0;
    const char*      pollWord = "stall";
    std::string      begun;

    NetFilesView view(uint32_t limitKb = 64) {
        NetFilesView v;
        v.page = [this](int skip, int max, std::vector<FileEntry>& out, std::vector<std::string>& ids, std::vector<std::string>& homes) {
            pages++;
            if (shortOfMemory) return (int)NetFilesView::kShort;
            int i = skip;
            for (; i < (int)rows.size() && i < skip + max; i++) {
                out.push_back({rows[(size_t)i].name, rows[(size_t)i].size, rows[(size_t)i].desc, 0});
                ids.push_back(rows[(size_t)i].id);
                homes.push_back(rows[(size_t)i].home);
            }
            return i < (int)rows.size() ? i : -1;
        };
        v.unreachable = [this](const std::string& id) {
            unreachables++;
            auto it = hostless.find(id);
            return it == hostless.end() ? std::string() : it->second;
        };
        v.callerRadioBps = [this] { return bps; };
        v.begin = [this](const std::string& id, std::string& area, std::string& name) {
            begins++;
            begun = id;
            if (beginAnswer == NetFilesView::kOwn) {
                area = ownArea;
                name = ownName;
            }
            return beginAnswer;
        };
        v.poll = [this](uint32_t& size, const char*& word) {
            int r = pollAnswers.empty() ? 0 : pollAnswers[std::min((size_t)polls, pollAnswers.size() - 1)];
            polls++;
            if (r > 0) size = pollSize;
            if (r < 0) word = pollWord;
            return r;
        };
        v.release = [this] { releases++; };
        v.radioLimitKb = limitKb;
        return v;
    }
};

FakeNet::Row netRow(int i, uint32_t size = 340 * 1024, const std::string& home = "Far Board") {
    return {"0123456789abcdef-" + std::to_string(i), "NET" + std::to_string(i) + ".ZIP", "Net file " + std::to_string(i), home, size};
}

// A typed (no lightbar) browser over two local areas with the Net Files row set, open at the Net Files list.
struct NetBrowser {
    FakeLibrary lib = library();
    BbsConfig   cfg = twoAreas();
    std::string out;
    FakeNet     net;
    FileBrowser b{cfg.areas, lib, out};
    explicit NetBrowser(int rows = 3, uint32_t limitKb = 64) {
        for (int i = 0; i < rows; i++) net.rows.push_back(netRow(i));
        b.setLightbars(false);
        b.setNetFiles(net.view(limitKb));
        b.begin();
        typeIn("3\r");  // areas + 1: Net Files
    }
    void typeIn(const std::string& s) {
        for (char c : s) b.key(c);
    }
    std::string take() {
        std::string s = plain(out);
        out.clear();
        return s;
    }
    void pick(const std::string& name) {  // D, the name, Enter
        typeIn("d" + name + "\r");
    }
};
}  // namespace

TEST_CASE("Net Files: the caller's area list has the row only while User Net: Files is open to the caller") {
    for (int level : {0, 255}) {
        CAPTURE(level);
        FakeLibrary lib = library();
        BbsConfig   c = twoAreas();
        c.msgAreas.push_back({"un_files", "User Net: Files", level});
        Node    n(1, info, c, &lib);
        FakeNet net;
        n.setNetFiles(net.view());
        n.begin(0);
        type(n, "Rev\r");
        n.takeOutput();
        type(n, "f");
        std::string p = plain(n.takeOutput());
        CHECK(p.find("Text Files") != std::string::npos);
        CHECK((p.find("Net Files (User Net)") != std::string::npos) == (level == 0));
    }
}

TEST_CASE("Net Files: the row opens the list, with home boards, pages of 15, and an empty or short page's text") {
    NetBrowser t(20);
    std::string p = t.take();
    CHECK(t.b.where() == "Net Files (User Net)");
    CHECK(p.find("NET0.ZIP") != std::string::npos);
    CHECK(p.find("Far Board") != std::string::npos);
    CHECK(p.find("page 1, more") != std::string::npos);
    CHECK(p.find("NET15.ZIP") == std::string::npos);
    t.typeIn("n");
    p = t.take();
    CHECK(p.find("NET15.ZIP") != std::string::npos);
    CHECK(p.find("page 2") != std::string::npos);
    CHECK(p.find("more") == std::string::npos);
    t.typeIn("n");  // no more: stays
    CHECK(t.take().find("NET15.ZIP") != std::string::npos);
    t.typeIn("p");
    CHECK(t.take().find("NET0.ZIP") != std::string::npos);
    t.typeIn("p");  // never below the start
    CHECK(t.take().find("NET0.ZIP") != std::string::npos);
    // V, I and U do nothing here
    int pages = t.net.pages;
    t.typeIn("viu");
    CHECK(t.net.pages == pages);
    CHECK_FALSE(t.b.wantsTransfer());

    NetBrowser e(0);
    CHECK(e.take().find("(nothing here yet)") != std::string::npos);
    NetBrowser s(3);
    s.net.shortOfMemory = 1;
    s.typeIn("q3\r");
    CHECK(s.take().find("Net Files are busy right now, try again in a minute") != std::string::npos);
}

TEST_CASE("Net Files: the radio question gives 340 KB at 3906 bps as about 12 minutes") {
    NetBrowser t;
    t.net.bps = 3906;
    t.take();
    t.pick("NET0.ZIP");
    std::string p = t.take();
    CHECK(p.find("This file is 340 KB. Over the radio link at 3906 bps that's about 12 minutes. Fetch it anyway? (Y/N)") !=
          std::string::npos);
    CHECK(t.net.begins == 0);
    t.typeIn("x");  // other keys are ignored
    CHECK(t.take().find("[Z]MODEM") == std::string::npos);
    t.typeIn("y");
    CHECK(t.take().find("[Z]MODEM") != std::string::npos);
    t.typeIn("z");
    CHECK(t.net.begins == 1);
    CHECK(t.net.begun == "0123456789abcdef-0");
    // N and Enter go back to the list
    NetBrowser n;
    n.net.bps = 3906;
    n.pick("NET1.ZIP");
    n.take();
    n.typeIn("n");
    CHECK(n.net.begins == 0);
    n.pick("NET1.ZIP");
    n.take();
    n.typeIn("\r");
    CHECK(n.net.begins == 0);
    CHECK(n.take().find("[Z]MODEM") == std::string::npos);
}

TEST_CASE("Net Files: the radio question's rounding, and when there is none") {
    struct Case {
        uint32_t size;
        int32_t  bps;
        uint32_t limitKb;
        const char* want;  // "" for no question
    };
    // 89 s and 90 s at 1000 bps are 11125 and 11250 bytes.
    const Case cases[] = {
        {11125, 1000, 0, "This file is 11 KB. Over the radio link at 1000 bps that's about 1 minute. Fetch"},
        {11250, 1000, 0, "about 2 minutes"},
        {100, 62500, 0, "This file is 1 KB. Over the radio link at 62500 bps that's about 1 minute."},  // at least 1; KB rounded up
        {340 * 1024, -1, 64, ""},       // not radio
        {340 * 1024, 0, 64, ""},        // no rate
        {64 * 1024, 3906, 64, ""},      // at the limit
        {64 * 1024 + 1, 3906, 64, "This file is 65 KB."},
    };
    for (const Case& c : cases) {
        CAPTURE(c.size);
        CAPTURE(c.bps);
        NetBrowser t(0, c.limitKb);
        t.net.rows.push_back(netRow(0, c.size));
        t.net.bps = c.bps;
        t.typeIn("q3\r");
        t.take();
        t.pick("NET0.ZIP");
        std::string p = t.take();
        if (*c.want) {
            CHECK(p.find(c.want) != std::string::npos);
            CHECK(p.find("[Z]MODEM") == std::string::npos);
        } else {
            CHECK(p.find("radio link") == std::string::npos);
            CHECK(p.find("[Z]MODEM") != std::string::npos);
        }
    }
}

TEST_CASE("Net Files: an unreachable home board's text is shown and nothing is begun") {
    NetBrowser t;
    t.net.hostless["0123456789abcdef-1"] = "that board can't be reached right now";
    t.take();
    t.pick("NET1.ZIP");
    CHECK(t.take().find("that board can't be reached right now") != std::string::npos);
    CHECK(t.net.begins == 0);
    CHECK_FALSE(t.b.wantsTransfer());
}

TEST_CASE("Net Files: the download gate runs first with the listing's size; a refusal stops everything") {
    NetBrowser t;
    uint32_t   asked = 0;
    t.b.setDownloadGate([&](uint32_t size) {
        asked = size;
        return std::string("upload something first");
    });
    t.net.bps = 3906;
    t.take();
    t.pick("NET2.ZIP");
    CHECK(asked == 340u * 1024);
    CHECK(t.take().find("upload something first") != std::string::npos);
    CHECK(t.net.unreachables == 0);
    CHECK(t.net.begins == 0);
    CHECK(t.net.polls == 0);
}

TEST_CASE("Net Files: begin's refusals and poll's failures read as the caller's texts") {
    struct Case {
        int         begin;
        const char* want;
    };
    for (const Case& c : {Case{NetFilesView::kBusy, "that board is busy, try again in a minute"},
                          Case{NetFilesView::kGone, "that file is no longer there"},
                          Case{NetFilesView::kUnreachable, "that board can't be reached right now"},
                          Case{NetFilesView::kOff, "that board can't be reached right now"},
                          Case{NetFilesView::kLevel, "that file is in an area closed to your account"}}) {
        CAPTURE(c.want);
        NetBrowser t;
        t.net.beginAnswer = c.begin;
        t.pick("NET0.ZIP");
        t.typeIn("z");
        CHECK(t.take().find(c.want) != std::string::npos);
        CHECK_FALSE(t.b.wantsTransfer());
        CHECK(t.net.polls == 0);
    }
    struct Fail {
        const char* word;
        const char* want;
    };
    for (const Fail& f : {Fail{"gone", "that file is no longer there"}, Fail{"size", "that file is no longer there"},
                          Fail{"busy", "that board is busy, try again in a minute"},
                          Fail{"stall", "that board can't be reached right now"},
                          Fail{"crc", "that board can't be reached right now"}}) {
        CAPTURE(f.word);
        NetBrowser t;
        t.net.pollAnswers = {0, 0, -1};
        t.net.pollWord = f.word;
        t.pick("NET0.ZIP");
        t.typeIn("z");
        CHECK(t.take().find("Connecting to Far Board...") != std::string::npos);
        for (uint32_t ms = 0; ms < 5; ms++) t.b.tick(ms, 0);
        CHECK(t.take().find(f.want) != std::string::npos);
        CHECK(t.net.releases == 1);
        CHECK_FALSE(t.b.wantsTransfer());
    }
}

TEST_CASE("Net Files: a radio caller's download window has no 24 KB/s time; other callers keep it") {
    for (int32_t bps : {-1, 3906}) {
        CAPTURE(bps);
        NetBrowser t;
        t.net.bps = bps;
        t.net.pollAnswers = {1};
        t.net.pollSize = 340 * 1024;
        t.pick("NET0.ZIP");
        t.typeIn(bps > 0 ? "yz" : "z");
        t.b.tick(0, 0);
        REQUIRE(t.b.wantsTransfer());
        std::string p = t.take();
        CHECK(p.find("D O W N L O A D") != std::string::npos);
        CHECK(p.find("348160 bytes") != std::string::npos);
        CHECK((p.find("24 KB/s") != std::string::npos) == (bps < 0));
    }
}

TEST_CASE("Net Files: a ready poll opens the download window with the polled size; a key while connecting gives it up") {
    {
        NetBrowser t;
        t.net.pollAnswers = {0, 1};
        t.net.pollSize = 123456;
        t.pick("NET0.ZIP");
        t.typeIn("z");
        t.b.tick(0, 0);
        CHECK_FALSE(t.b.wantsTransfer());
        t.b.tick(1, 0);
        CHECK(t.b.wantsTransfer());
        CHECK(t.b.transfer().net);
        CHECK(t.b.transfer().name == "NET0.ZIP");
        CHECK(t.b.transfer().proto == 'Z');
        std::string p = t.take();
        CHECK(p.find("NET0.ZIP") != std::string::npos);
        CHECK(p.find("123456 bytes") != std::string::npos);
        CHECK(t.net.releases == 0);
    }
    {
        NetBrowser t;
        t.pick("NET0.ZIP");
        t.typeIn("z");
        t.typeIn("\r\n");  // an Enter after the protocol letter is not a stop
        t.b.key('\0');
        CHECK(t.net.releases == 0);
        t.typeIn(" ");
        CHECK(t.net.releases == 1);
        t.b.tick(5, 0);
        CHECK(t.net.polls == 0);  // back at the list: no more polls
        CHECK_FALSE(t.b.wantsTransfer());
    }
}

TEST_CASE("Net Files: this board's own listing downloads from its local area, as an ordinary download") {
    NetBrowser t;
    t.net.beginAnswer = NetFilesView::kOwn;
    t.net.ownArea = "axis";
    t.net.ownName = "AXISFLSH.ZIP";
    t.pick("NET0.ZIP");
    t.typeIn("z");
    REQUIRE(t.b.wantsTransfer());
    CHECK_FALSE(t.b.transfer().net);
    CHECK(t.b.transfer().area == "axis");
    CHECK(t.b.transfer().name == "AXISFLSH.ZIP");
    CHECK(t.net.polls == 0);
}

TEST_CASE("Net Files: a long home name and file name are cut on the connect line and the window") {
    FakeLibrary lib = library();
    BbsConfig   cfg = twoAreas();
    std::string out;
    FakeNet     net;
    net.rows.push_back({"0123456789abcdef-1", std::string(60, 'N'), "long", std::string(40, 'H'), 1000});
    net.pollAnswers = {1};
    FileBrowser b(cfg.areas, lib, out);
    b.setNetFiles(net.view());
    b.begin();
    down(b);
    down(b);
    b.key('\r');  // the Net Files row, on the lightbar
    CHECK(b.where() == "Net Files (User Net)");
    b.key('\r');  // its first file
    out.clear();
    b.key('z');
    std::string p = plain(out);
    out.clear();
    CHECK(p.find("Connecting to " + std::string(30, 'H') + "...") != std::string::npos);
    CHECK(p.find(std::string(31, 'H')) == std::string::npos);
    b.tick(0, 0);
    p = plain(out);
    CHECK(p.find(std::string(45, 'N')) != std::string::npos);
    CHECK(p.find(std::string(46, 'N')) == std::string::npos);
}

TEST_CASE("strict area levels (Rev): this board's own listing in an area above the caller's level is refused") {
    std::vector<FileArea> areas = {{"public", "Public", 0}, {"members", "Members", 50}, {"sysop", "Sysop", 255}};
    using V = NetFilesView;
    CHECK(V::ownAnswer(areas, true, "members", 49) == V::kLevel);
    CHECK(V::ownAnswer(areas, true, "members", 50) == V::kOwn);  // at the level
    CHECK(V::ownAnswer(areas, true, "members", 90) == V::kOwn);  // above it
    CHECK(V::ownAnswer(areas, true, "sysop", 254) == V::kLevel);
    CHECK(V::ownAnswer(areas, true, "sysop", 255) == V::kOwn);  // the sysop passes
    CHECK(V::ownAnswer(areas, true, "public", 0) == V::kOwn);
    CHECK(V::ownAnswer(areas, true, "members", -1) == V::kLevel);  // a follower's b with no level (main.cpp reads it as -1)
    CHECK(V::ownAnswer(areas, false, "members", 255) == V::kGone);  // no longer shared
    CHECK(V::ownAnswer(areas, true, "elsewhere", 255) == V::kGone);  // no longer here
    // The caller sees the level text, and no fetch is polled.
    NetBrowser t;
    t.net.beginAnswer = V::ownAnswer(areas, true, "members", 10);
    t.pick("NET0.ZIP");
    t.typeIn("z");
    CHECK(t.take().find("that file is in an area closed to your account") != std::string::npos);
    CHECK_FALSE(t.b.wantsTransfer());
}
TEST_CASE("Net Files: Enter on a text file downloads it (it lives on another board); a number past the row redraws the areas") {
    FakeLibrary lib = library();
    BbsConfig   cfg = twoAreas();
    std::string out;
    FakeNet     net;
    net.rows.push_back({"0123456789abcdef-1", "NOTES.TXT", "Some notes", "Far Board", 2000});
    FileBrowser b(cfg.areas, lib, out);
    b.setNetFiles(net.view());
    b.begin();
    down(b);
    down(b);
    b.key('\r');
    b.key('\r');  // NOTES.TXT
    std::string p = plain(out);
    CHECK(p.find("[Z]MODEM") != std::string::npos);
    CHECK(net.unreachables == 1);

    NetBrowser t;
    t.typeIn("q");
    t.take();
    t.typeIn("4\r");  // areas + 2: no such row
    CHECK(t.b.where() == "area list");
    CHECK(t.take().find("Net Files (User Net)") != std::string::npos);
}

#endif  // AXIS_NET_FILES

#endif  // AXIS_FILES
