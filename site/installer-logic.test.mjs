// Tests for site/installer-logic.js. Run: node --test site/installer-logic.test.mjs
import { test } from "node:test";
import assert from "node:assert/strict";
import { createRequire } from "node:module";
import { readFileSync } from "node:fs";
import { createHash } from "node:crypto";

const require = createRequire(import.meta.url);
const L = require("./installer-logic.js");
const root = new URL("../", import.meta.url);
const readJson = (p) => JSON.parse(readFileSync(new URL(p, root), "utf8"));
const features = readJson("installer/features.json");
const presets = readJson("installer/presets.json");
const plans = readJson("installer/test-fixtures/flash-plans.json");
const hex = (s) => Uint8Array.from(Buffer.from(s, "hex"));

function deviceFrom(c) {
  return {
    chip: c.chip,
    partTable: hex(c.part_table_hex),
    appHeader: hex(c.app_header_hex),
    bootloaderSha256: c.bootloader_sha256 || null,
  };
}

function targetFrom(t) {
  return { kind: t.kind, chip: t.chip, project: t.project, layout: t.layout, partTable: hex(t.part_table_hex), images: t.images };
}

test("selection: a preset gives its off list, sorted", () => {
  assert.deepEqual(L.selection("no-doors", presets), ["dosprompt", "lord", "scripts"]);
  assert.deepEqual(L.selection("everything", presets), []);
  assert.deepEqual(L.selection(["web", "lord", "web"], presets), ["lord", "web"]);
  assert.throws(() => L.selection("no-such-preset", presets), /unknown preset/);
});

test("selection: every preset passes the dependency check", () => {
  for (const p of presets) assert.equal(L.checkDeps(L.selection(p.id, presets), features), null, p.id);
});

test("matchPreset finds the preset with the same off list", () => {
  assert.equal(L.matchPreset(["scripts", "lord", "dosprompt"], presets), "no-doors");
  assert.equal(L.matchPreset([], presets), "everything");
  assert.equal(L.matchPreset(["lord"], presets), null);
});

test("checkDeps refuses a broken dependency with the reason", () => {
  const r = L.checkDeps(["files"], features);
  assert.match(r, /ansiview needs files, which is off/);
  assert.match(r, /transfers needs files, which is off/);
  assert.equal(L.checkDeps(["files", "ansiview", "transfers"], features), null);
  assert.match(L.checkDeps(["gpio"], features), /reserved/);
  assert.match(L.checkDeps(["nope"], features), /unknown feature/);
});

test("fingerprint equals the Python one (Task 1 fixture)", async () => {
  const cases = readJson("installer/test-fixtures/fingerprints.json");
  assert.ok(cases.length >= 4);
  for (const c of cases) {
    assert.equal(await L.fingerprint(c.off, c.release), c.fingerprint, JSON.stringify(c));
    assert.equal(await L.fingerprint([...c.off].reverse(), c.release), c.fingerprint);
  }
});

test("parseTable reads the real AXIS partition table", () => {
  const t = L.parseTable(hex(plans.targets["axis-board"].part_table_hex));
  assert.deepEqual(t.map((e) => [e.label, e.offset, e.size]), [
    ["nvs", 0x9000, 0x5000], ["otadata", 0xe000, 0x2000], ["app0", 0x10000, 0x240000], ["spiffs", 0x250000, 0x1b0000],
  ]);
  assert.equal(L.parseTable(new Uint8Array(64).fill(0xff)), null);
  assert.equal(L.parseTable(null), null);
});

test("parseAppHeader names the program in the app", () => {
  const c = plans.cases.find((x) => x.name === "board-update");
  assert.equal(L.parseAppHeader(hex(c.device.app_header_hex)).project, "axis-bbs");
  assert.equal(L.parseAppHeader(new Uint8Array(256).fill(0xff)), null);
});

test("planFlash: update writes the app only, at 0x10000", () => {
  const c = plans.cases.find((x) => x.name === "board-update");
  const p = L.planFlash(deviceFrom(c.device), targetFrom(plans.targets[c.target]));
  assert.equal(p.kind, "update");
  assert.deepEqual(p.regions.map((r) => [r.role, r.offset]), [["app", 0x10000]]);
});

test("planFlash: a blank or different table is a first install (bootloader, partitions, app)", () => {
  for (const name of ["board-first-blank", "board-first-stock", "board-first-old-layout"]) {
    const c = plans.cases.find((x) => x.name === name);
    const p = L.planFlash(deviceFrom(c.device), targetFrom(plans.targets[c.target]));
    assert.equal(p.kind, "first", name);
    const roles = p.regions.map((r) => r.role);
    for (const r of ["bootloader", "partitions", "app"]) assert.ok(roles.includes(r), name + " " + r);
  }
});

test("planFlash: refuses a radio chip offered the board image, and a CYD offered a radio image", () => {
  for (const name of ["board-image-on-radio", "radio-image-on-cyd", "radio-multimode-on-cyd"]) {
    const c = plans.cases.find((x) => x.name === name);
    const p = L.planFlash(deviceFrom(c.device), targetFrom(plans.targets[c.target]));
    assert.equal(p.kind, "refuse", name);
    assert.deepEqual(p.regions, []);
    assert.ok(p.reason.length > 10);
  }
});

test("planFlash: a V4 with the multimode table gets only the modem slot", () => {
  const c = plans.cases.find((x) => x.name === "v4-multimode-update");
  const p = L.planFlash(deviceFrom(c.device), targetFrom(plans.targets[c.target]));
  assert.equal(p.kind, "update");
  assert.deepEqual(p.regions.map((r) => [r.role, r.offset]), [["app", 0x4a0000]]);
});

test("planFlash matches every case in the shared fixture (same file as Task 9's Python test)", () => {
  assert.ok(plans.cases.length >= 15);
  for (const c of plans.cases) {
    const p = L.planFlash(deviceFrom(c.device), targetFrom(plans.targets[c.target]));
    assert.equal(p.kind, c.expect.kind, c.name);
    assert.equal(p.code, c.expect.code, c.name);
    assert.deepEqual(p.regions.map((r) => ({ role: r.role, offset: r.offset })), c.expect.regions, c.name);
    if (p.kind === "refuse") assert.ok(p.reason, c.name);
  }
});

test("the first-install warning is the shared text", () => {
  assert.equal(L.FIRST_INSTALL_WARNING, plans.first_install_warning);
});

test("guardRegions refuses an update that would touch NVS, otadata, LittleFS or outside the slot", () => {
  const axis = L.parseTable(hex(plans.targets["axis-board"].part_table_hex));
  const ok = { kind: "update", regions: [{ role: "app", offset: 0x10000, size: 0x200000 }] };
  assert.doesNotThrow(() => L.guardRegions(ok, axis, 0x10000));
  for (const bad of [
    { role: "app", offset: 0x9000, size: 0x1000 },
    { role: "otadata", offset: 0xe000, size: 0x2000 },
    { role: "app", offset: 0x250000, size: 0x1000 },
    { role: "app", offset: 0x10000, size: 0x240001 },
    { role: "partitions", offset: 0x8000, size: 0xc00 },
  ]) {
    assert.throws(() => L.guardRegions({ kind: "update", regions: [bad] }, axis, 0x10000), /refused/, JSON.stringify(bad));
  }
  const mm = L.parseTable(hex(plans.targets["radio-v4-multimode"].part_table_hex));
  assert.doesNotThrow(() => L.guardRegions({ kind: "update", regions: [{ role: "app", offset: 0x4a0000, size: 1059504 }] }, mm, 0x4a0000));
  assert.throws(() => L.guardRegions({ kind: "update", regions: [{ role: "app", offset: 0x20000, size: 1000 }] }, mm, 0x4a0000), /refused/);
  assert.throws(() => L.guardRegions({ kind: "refuse", regions: [] }, axis, 0x10000), /refused/);
});

test("radioEnv picks the multimode slot only for a V4 that has the layout", () => {
  const mm = hex(plans.targets["radio-v4-multimode"].part_table_hex);
  const single = hex(plans.targets["radio-v4-single"].part_table_hex);
  assert.equal(L.radioEnv("v4", mm), "heltec_v4");
  assert.equal(L.radioEnv("v4", single), "heltec_v4_single");
  assert.equal(L.radioEnv("v4", new Uint8Array(64).fill(0xff)), "heltec_v4_single");
  assert.equal(L.radioEnv("v3", single), "heltec_v3_single");
  assert.equal(L.radioEnv("v3", mm), "heltec_v3_single");
});

test("stageName walks the four stages", () => {
  assert.equal(L.stageName(0), "BL4Z1NG BYT3S");
  assert.equal(L.stageName(24), "BL4Z1NG BYT3S");
  assert.equal(L.stageName(25), "FL0WTR0N ENGAGED");
  assert.equal(L.stageName(50), "S0N1C B00M");
  assert.equal(L.stageName(75), "DATAMASTER WRITING");
  assert.equal(L.stageName(100), "DATAMASTER WRITING");
});

test("blockBar draws whole blocks", () => {
  assert.equal(L.blockBar(0, 10), "[░░░░░░░░░░]   0%");
  assert.equal(L.blockBar(50, 10), "[█████░░░░░]  50%");
  assert.equal(L.blockBar(100, 4), "[████] 100%");
});

test("md5 of a binary string equals node's", () => {
  for (const len of [0, 1, 55, 56, 64, 1000, 70001]) {
    const bytes = Buffer.alloc(len);
    for (let i = 0; i < len; i++) bytes[i] = (i * 31 + 7) & 0xff;
    const bstr = String.fromCharCode(...bytes.subarray(0, Math.min(len, 60000))) + (len > 60000 ? String.fromCharCode(...bytes.subarray(60000)) : "");
    assert.equal(L.md5(bstr), createHash("md5").update(bytes).digest("hex"), "len " + len);
  }
});

test("buildCommand fills the Task 9 command for Windows and Linux", () => {
  const base = "https://revjmoney.github.io/axis-bbs/";
  const both = { target: "both", board: "esp32", preset: "no-doors", off: null, radio: "v4", release: "v0.60" };
  assert.equal(L.buildCommand(both, "linux", base),
    "curl -fsSL https://revjmoney.github.io/axis-bbs/install.sh | sh -s -- --board no-doors --target esp32 --radio v4 --release v0.60 --flash");
  assert.equal(L.buildCommand(both, "windows", base),
    "& ([scriptblock]::Create((irm https://revjmoney.github.io/axis-bbs/install.ps1))) -Board no-doors -Target esp32 -Radio v4 -Release v0.60 -Flash");
  const custom = { target: "board", board: "cyd", preset: null, off: ["web", "lord"], radio: "v3", release: "v0.60" };
  assert.equal(L.buildCommand(custom, "linux", base),
    "curl -fsSL https://revjmoney.github.io/axis-bbs/install.sh | sh -s -- --off lord,web --target cyd --release v0.60 --flash");
  const radio = { target: "radio", board: "cyd", preset: "everything", off: null, radio: "v3", release: "v0.60" };
  assert.equal(L.buildCommand(radio, "windows", base),
    "& ([scriptblock]::Create((irm https://revjmoney.github.io/axis-bbs/install.ps1))) -Radio v3 -Release v0.60 -Flash");
});

test("no em dash or double hyphen anywhere in the logic module (command flags excepted)", () => {
  const src = readFileSync(new URL("site/installer-logic.js", root), "utf8");
  assert.ok(!src.includes(String.fromCharCode(0x2014)), "em dash");
  const flags = ["--board", "--off", "--target", "--radio", "--release", "--flash", "sh -s --"];
  let rest = src;
  for (const f of flags) rest = rest.split(f).join("");
  assert.ok(!rest.includes("--"), "double hyphen at " + rest.indexOf("--"));
});

test("the page and its script show no em dash or double hyphen, and use the full names", () => {
  const html = readFileSync(new URL("site/install.html", root), "utf8");
  const js = readFileSync(new URL("site/install.js", root), "utf8");
  for (const [name, src] of [["install.html", html], ["install.js", js]]) {
    assert.ok(!src.includes(String.fromCharCode(0x2014)), name + ": em dash");
    assert.ok(!src.includes("--"), name + ": double hyphen");
    assert.ok(!/LORA FLOWTRON/i.test(src), name + ": old modem name");
  }
  assert.ok(html.includes("YOU NEED AN SD CARD FOR FULL FUNCTIONS"));
  assert.ok(html.includes("LRA FLOWTRON DATAMASTER 3000 SERIAL MODEM - LFD3K-SM"));
  assert.ok(html.includes("by Rev. J. Money / <a"));
  assert.equal((html.match(/registered trademark of Semtech/g) || []).length, 1);
  assert.ok(html.includes("Radio images come with the first LFD3K-SM release."));
  assert.match(js, /esptool-js@\d+\.\d+\.\d+\/bundle\.js/, "esptool-js pinned to an exact version");
  assert.ok(!/\bMac\b|macOS/.test(html + js), "no Mac instructions");
});

test("validFingerprint accepts 12 hex characters only", () => {
  assert.ok(L.validFingerprint("75f2a9d24a69"));
  for (const bad of ["75F2A9D24A69", "75f2a9d24a6", "../../x", "", null, "75f2a9d24a69/"]) assert.ok(!L.validFingerprint(bad), String(bad));
});

