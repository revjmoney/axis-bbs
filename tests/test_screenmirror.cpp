#include "door/screenmirror.h"

#include "doctest.h"

using axis::door::ScreenMirror;

namespace {
std::vector<uint8_t> blank() {
    std::vector<uint8_t> p(4000);
    for (size_t i = 0; i < p.size(); i += 2) p[i] = ' ', p[i + 1] = 0x07;
    return p;
}
void put(std::vector<uint8_t>& p, int row, int col, const char* s, uint8_t attr) {
    for (int i = 0; s[i]; i++) p[(row * 80 + col + i) * 2] = (uint8_t)s[i], p[(row * 80 + col + i) * 2 + 1] = attr;
}
}  // namespace

TEST_CASE("the first frame clears the screen and draws the text page") {
    ScreenMirror m;
    auto         p = blank();
    put(p, 2, 10, "LORDCFG", 0x0E);  // yellow on black
    std::string out = m.update(p);
    CHECK(out.find("\x1b[2J") != std::string::npos);
    CHECK(out.find("LORDCFG") != std::string::npos);
    CHECK(out.find("\x1b[3;11H") != std::string::npos);  // row 3, column 11
    CHECK(out.find("1;33") != std::string::npos);        // DOS yellow is bright brown: bold + ANSI yellow
}

TEST_CASE("later frames send only what changed") {
    ScreenMirror m;
    auto         p = blank();
    put(p, 0, 0, "Hello", 0x07);
    m.update(p);
    put(p, 5, 3, "X", 0x04);  // DOS red is ANSI 31
    std::string out = m.update(p);
    CHECK(out.find("Hello") == std::string::npos);
    CHECK(out.find("\x1b[6;4H") != std::string::npos);
    CHECK(out.find("31") != std::string::npos);
    CHECK(out.find('X') != std::string::npos);
    CHECK(m.update(p).empty());  // nothing changed, nothing sent
}

TEST_CASE("the bottom-right cell is never written (it would scroll the caller's screen)") {
    ScreenMirror m;
    auto         p = blank();
    m.update(p);
    put(p, 24, 79, "Z", 0x07);
    CHECK(m.update(p).find('Z') == std::string::npos);
}

#include <cstdlib>
#include <filesystem>
#include "door/bytestream.h"
#include "door/session.h"
#include "platform/pc/stdhostfs.h"

TEST_CASE("LORDCFG's menu, registration included, comes through the mirror") {
    const char* src = std::getenv("AXIS_LORD_DIR");
    if (!src) return;  // skipped without the LORD folder
    namespace fs = std::filesystem;
    fs::path d = fs::temp_directory_path() / "axis_lordcfg_mirror";
    fs::remove_all(d);
    fs::copy(src, d, fs::copy_options::recursive);
    axis::pc::StdHostFs   fsys(d);
    axis::door::LockTable locks;
    axis::door::ScriptStream keys;
    axis::door::DoorConfig cfg;
    cfg.dir = d.string();
    cfg.exe = "LORDCFG.EXE";
    axis::door::DoorSession s(cfg, fsys, locks, keys, 1);
    ScreenMirror m;
    std::string  all;
    std::string  scr;
    bool         answered = false;
    for (int t = 0; t < 8000 && scr.find("Register Lord") == std::string::npos; t += 50) {
        if (!s.step(50)) break;
        all += m.update(s.textPage());
        scr.clear();
        for (auto& l : s.screen()) scr += l + "\n";
        if (!answered && scr.find("Create a NEW LORD.DAT") != std::string::npos) keys.push("A"), answered = true;
    }
    CHECK(scr.find("Register Lord") != std::string::npos);  // the menu is up
    CHECK(all.size() > 2000);                               // and it went out to the caller (drawn in pieces)
}
