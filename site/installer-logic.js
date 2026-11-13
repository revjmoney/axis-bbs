/* AXIS Micro BBS installer: the pure logic (no DOM, no serial port).
 * Loaded by install.html as a classic script (window.AxisLogic) and by
 * site/installer-logic.test.mjs through require(). The flash decision here is
 * tested against installer/test-fixtures/flash-plans.json, the same file the
 * PC scripts' Python test reads, so the page and the scripts agree on a board.
 */
(function (root, factory) {
  var api = factory();
  if (typeof module === "object" && module.exports) module.exports = api;
  else root.AxisLogic = api;
})(typeof self !== "undefined" ? self : this, function () {
  "use strict";

  var BOARD_NAME = "AXIS Micro BBS";
  var MODEM_NAME = "LRA FLOWTRON DATAMASTER 3000 SERIAL MODEM - LFD3K-SM";
  var FIRST_INSTALL_WARNING = "This replaces everything on this board. Whatever it runs now, and its settings, will be gone.";
  var MULTIMODE_MARKER = "picker_nvs"; // only the LFD3K-SM multimode layout has this partition
  var STAGES = ["BL4Z1NG BYT3S", "FL0WTR0N ENGAGED", "S0N1C B00M", "DATAMASTER WRITING"];
  var FIRST_ROLES = ["bootloader", "partitions", "otadata", "app"];

  // == selections and dependencies (same rules as tools/buildopts.py) ==

  function sortedUnique(list) {
    var seen = {}, out = [];
    for (var i = 0; i < list.length; i++) {
      if (!seen[list[i]]) { seen[list[i]] = true; out.push(list[i]); }
    }
    return out.sort();
  }

  /** selection("no-doors", presets) or selection(["lord", "web"], presets): the sorted off list. */
  function selection(choice, presets) {
    if (Array.isArray(choice)) return sortedUnique(choice);
    for (var i = 0; i < presets.length; i++) {
      if (presets[i].id === choice) return sortedUnique(presets[i].off);
    }
    throw new Error("unknown preset: " + choice);
  }

  function sameList(a, b) {
    a = sortedUnique(a); b = sortedUnique(b);
    if (a.length !== b.length) return false;
    for (var i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
    return true;
  }

  /** The id of the preset whose off list equals this one, or null. */
  function matchPreset(off, presets) {
    for (var i = 0; i < presets.length; i++) if (sameList(presets[i].off, off)) return presets[i].id;
    return null;
  }

  /** null when the off list is fine, otherwise the reason (worded like buildopts.resolve). */
  function checkDeps(off, features) {
    var byId = {}, offSet = {}, i, j;
    for (i = 0; i < features.length; i++) byId[features[i].id] = features[i];
    off = sortedUnique(off);
    for (i = 0; i < off.length; i++) {
      if (!byId[off[i]]) return "unknown feature: " + off[i];
      if (byId[off[i]].reserved) return "feature " + off[i] + " is reserved and cannot be selected yet";
      offSet[off[i]] = true;
    }
    var broken = [];
    for (i = 0; i < features.length; i++) {
      var f = features[i];
      if (offSet[f.id] || f.reserved) continue;
      var needs = f.needs || [];
      for (j = 0; j < needs.length; j++) {
        if (offSet[needs[j]]) {
          broken.push(f.id + " needs " + needs[j] + ", which is off (turn " + f.id + " off too, or keep " + needs[j] + " on)");
        }
      }
    }
    return broken.length ? broken.join("; ") : null;
  }

  function toHex(bytes) {
    var s = "";
    for (var i = 0; i < bytes.length; i++) s += (bytes[i] < 16 ? "0" : "") + bytes[i].toString(16);
    return s;
  }

  /** First 12 hex of sha256(release + "|" + sorted off joined by ","), as tools/buildopts.py does. */
  function fingerprint(off, release) {
    var data = new TextEncoder().encode(release + "|" + sortedUnique(off).join(","));
    return globalThis.crypto.subtle.digest("SHA-256", data).then(function (buf) {
      return toHex(new Uint8Array(buf)).slice(0, 12);
    });
  }

  function validFingerprint(fp) {
    return typeof fp === "string" && /^[0-9a-f]{12}$/.test(fp);
  }

  // == what is on the board ==

  function u32(b, i) {
    return (b[i] | (b[i + 1] << 8) | (b[i + 2] << 16) | (b[i + 3] << 24)) >>> 0;
  }

  function cString(b, start, len) {
    var s = "";
    for (var i = start; i < start + len && i < b.length; i++) {
      if (b[i] === 0) break;
      if (b[i] < 32 || b[i] > 126) return null;
      s += String.fromCharCode(b[i]);
    }
    return s;
  }

  /** The partition table (the bytes at 0x8000) as entries, or null when there is none. */
  function parseTable(bytes) {
    if (!bytes) return null;
    var out = [];
    for (var i = 0; i + 32 <= bytes.length; i += 32) {
      if (bytes[i] !== 0xaa || bytes[i + 1] !== 0x50) break; // 0xEBEB (the MD5 entry) or 0xFFFF ends it
      out.push({
        type: bytes[i + 2],
        subtype: bytes[i + 3],
        offset: u32(bytes, i + 4),
        size: u32(bytes, i + 8),
        label: cString(bytes, i + 12, 16) || "",
        flags: u32(bytes, i + 28)
      });
    }
    return out.length ? out : null;
  }

  function tablesEqual(a, b) {
    if (!a || !b || a.length !== b.length) return false;
    for (var i = 0; i < a.length; i++) {
      var x = a[i], y = b[i];
      if (x.type !== y.type || x.subtype !== y.subtype || x.offset !== y.offset || x.size !== y.size || x.label !== y.label) return false;
    }
    return true;
  }

  function isMultimode(entries) {
    if (!entries) return false;
    for (var i = 0; i < entries.length; i++) if (entries[i].label === MULTIMODE_MARKER) return true;
    return false;
  }

  /** The program in an app image: { project, version } from esp_app_desc_t, or null. */
  function parseAppHeader(bytes) {
    if (!bytes || bytes.length < 0x70 || bytes[0] !== 0xe9) return null;
    if (u32(bytes, 0x20) !== 0xabcd5432) return null;
    var project = cString(bytes, 0x50, 32), version = cString(bytes, 0x30, 32);
    if (!project) return null;
    return { project: project, version: version || "" };
  }

  /** Which LFD3K-SM image a radio gets: the multimode slot only on a V4 that has that layout. */
  function radioEnv(model, tableBytes) {
    if (model === "v3") return "heltec_v3_single";
    return isMultimode(parseTable(tableBytes)) ? "heltec_v4" : "heltec_v4_single";
  }

  // == the flash decision ==

  function chipWhat(chip) {
    if (chip === "ESP32") return "a CYD or bare ESP32 BBS board";
    if (chip === "ESP32-S3") return "a Heltec V3 or V4 radio";
    return "another kind of board";
  }

  function refuse(code, reason) {
    return { kind: "refuse", code: code, regions: [], reason: reason + " Nothing was written." };
  }

  function copyImage(img) {
    var o = {};
    for (var k in img) if (Object.prototype.hasOwnProperty.call(img, k)) o[k] = img[k];
    return o;
  }

  /**
   * planFlash(device, target)
   *   device: { chip ("ESP32" | "ESP32-S3", as esptool names it), partTable (bytes read at 0x8000),
   *             appHeader (first bytes read at the target's app offset), bootloaderSha256 (optional) }
   *   target: { kind "board" | "radio", chip, project (name in the app header), layout "single" | "multimode",
   *             partTable (the image's own table, bytes), images [{ role, file, offset, size, sha256 }] }
   * Returns { kind "update" | "first" | "refuse", code, regions [images to write, in order], reason }.
   * An update writes the app image only. A multimode V4 only ever gets its modem slot.
   */
  function planFlash(device, target) {
    var name = target.kind === "radio" ? MODEM_NAME : BOARD_NAME;
    if (device.chip !== target.chip) {
      return refuse("chip", "This is an " + device.chip + " (" + chipWhat(device.chip) + "). The " + name +
        " image is for an " + target.chip + " (" + chipWhat(target.chip) + ").");
    }
    var want = parseTable(target.partTable);
    if (!want) throw new Error("the image has no partition table");
    var app = null, i;
    for (i = 0; i < target.images.length; i++) if (target.images[i].role === "app") app = target.images[i];
    if (!app) throw new Error("the image has no app");
    var part = null;
    for (i = 0; i < want.length; i++) if (want[i].type === 0 && want[i].offset === app.offset) part = want[i];
    if (!part) throw new Error("the app offset is not an app partition in the image's table");
    if (app.size > part.size) {
      return refuse("too-big", "The firmware is " + app.size + " bytes and its partition holds " + part.size + ".");
    }
    var have = parseTable(device.partTable);
    var same = tablesEqual(have, want);

    if (target.layout === "multimode") {
      if (same) {
        return { kind: "update", code: "update", regions: [copyImage(app)],
          reason: "This V4 has the multimode layout: only the modem slot is written. The picker, the other modes and the modem's settings stay." };
      }
      if (isMultimode(have)) {
        return refuse("slot", "This V4's multimode layout puts the modem slot somewhere other than this image expects. Set up the current multimode layout with the steps in the lfd3k-sm repo first.");
      }
      return refuse("no-multimode", "This V4 does not have the multimode layout, so the modem slot image cannot go on it. Pick the single-mode modem, or set up the multimode layout with the steps in the lfd3k-sm repo first.");
    }

    if (isMultimode(have)) {
      return refuse("multimode-layout", "This radio has the multimode layout (the picker and its modes). A single-mode image would wipe it. Pick Heltec V4 and the installer writes only the modem slot.");
    }

    var hdr = parseAppHeader(device.appHeader);
    if (same && hdr && hdr.project === target.project) {
      return { kind: "update", code: "update", regions: [copyImage(app)],
        reason: "This board already runs " + name + " (" + (hdr.version || "unknown version") + ") with the same partition layout: only the firmware is written. Users, messages and settings stay." };
    }

    var why;
    if (!have) why = "This board has no partition table (it is blank or new).";
    else if (!same) why = "This board has a different partition layout" + (hdr ? " and runs " + hdr.project : "") + ".";
    else why = "This board runs " + (hdr ? hdr.project : "something that is not " + name) + ", not " + name + ".";
    var regions = [];
    var imgs = target.images.slice().sort(function (a, b) { return a.offset - b.offset; });
    for (i = 0; i < imgs.length; i++) {
      var img = imgs[i];
      if (FIRST_ROLES.indexOf(img.role) < 0) continue;
      if (img.role === "partitions" && same) continue; // already this table
      if (img.role === "bootloader" && device.bootloaderSha256 &&
          String(device.bootloaderSha256).toLowerCase() === String(img.sha256).toLowerCase()) continue; // already this boot loader
      regions.push(copyImage(img));
    }
    return { kind: "first", code: "first", regions: regions, reason: why + " " + FIRST_INSTALL_WARNING };
  }

  /**
   * Last check right before anything is written. Throws "refused: ..." unless:
   *   update: exactly one app image, at appOffset, inside an app partition of the board's own table;
   *   first: only boot loader, partition table, otadata and app images.
   * So an update can never touch NVS (0x9000), otadata, LittleFS (0x250000), or a multimode V4's other slots.
   */
  function guardRegions(plan, deviceTable, appOffset) {
    if (!plan || (plan.kind !== "update" && plan.kind !== "first")) throw new Error("refused: nothing to write");
    var r, i, j;
    if (plan.kind === "update") {
      if (plan.regions.length !== 1) throw new Error("refused: an update writes one image");
      r = plan.regions[0];
      if (r.role !== "app" || r.offset !== appOffset) throw new Error("refused: an update writes the app only, at its own offset");
      var ok = false;
      for (j = 0; deviceTable && j < deviceTable.length; j++) {
        var p = deviceTable[j];
        if (p.type === 0 && p.offset === r.offset && r.size <= p.size) ok = true;
      }
      if (!ok) throw new Error("refused: the app does not fit an app partition on this board");
      return true;
    }
    for (i = 0; i < plan.regions.length; i++) {
      if (FIRST_ROLES.indexOf(plan.regions[i].role) < 0) throw new Error("refused: unexpected image " + plan.regions[i].role);
    }
    return true;
  }

  // == progress ==

  function stageName(percent) {
    var i = Math.floor(percent / 25);
    return STAGES[Math.max(0, Math.min(3, i))];
  }

  function blockBar(percent, width) {
    var p = Math.max(0, Math.min(100, Math.floor(percent)));
    var full = Math.round(p * width / 100);
    var pct = String(p);
    while (pct.length < 3) pct = " " + pct;
    return "[" + repeat(String.fromCharCode(0x2588), full) + repeat(String.fromCharCode(0x2591), width - full) + "] " + pct + "%";
  }

  function repeat(ch, n) {
    var s = "";
    for (var i = 0; i < n; i++) s += ch;
    return s;
  }

  function hexOffset(n) {
    return "0x" + n.toString(16).toUpperCase();
  }

  // == the PC command (Task 9: install.ps1 / install.sh) ==

  /** picks: { target "board" | "radio" | "both", board "cyd" | "esp32", preset, off, radio "v3" | "v4", release } */
  function buildCommand(picks, os, base) {
    var win = os === "windows";
    var args = [];
    function flag(name, value) {
      if (win) args.push("-" + name.charAt(0).toUpperCase() + name.slice(1));
      else args.push("-" + "-" + name);
      if (value !== undefined) args.push(value);
    }
    if (picks.target === "board" || picks.target === "both") {
      var off = picks.off ? sortedUnique(picks.off) : null;
      if (picks.preset || !off || !off.length) flag("board", picks.preset || "everything");
      else flag("off", off.join(","));
      flag("target", picks.board);
    }
    if (picks.target === "radio" || picks.target === "both") flag("radio", picks.radio);
    if (picks.release) flag("release", picks.release);
    flag("flash");
    if (win) return "& ([scriptblock]::Create((irm " + base + "install.ps1))) " + args.join(" ");
    return "curl -fsSL " + base + "install.sh | sh -s " + "-" + "- " + args.join(" ");
  }

  // == MD5 (esptool-js checks each write against the chip's own MD5) ==

  /** MD5 of a binary string (one byte per char), as hex. */
  function md5(str) {
    var K = [], S = [7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21], i;
    for (i = 0; i < 64; i++) K[i] = Math.floor(Math.abs(Math.sin(i + 1)) * 4294967296) >>> 0;
    var n = str.length, words = ((n + 8) >>> 6) + 1, M = new Uint32Array(words * 16);
    for (i = 0; i < n; i++) M[i >> 2] |= (str.charCodeAt(i) & 0xff) << ((i % 4) * 8);
    M[n >> 2] |= 0x80 << ((n % 4) * 8);
    M[words * 16 - 2] = (n * 8) >>> 0;
    M[words * 16 - 1] = Math.floor(n / 536870912) >>> 0;
    var a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;
    for (var blk = 0; blk < words * 16; blk += 16) {
      var A = a0, B = b0, C = c0, D = d0;
      for (i = 0; i < 64; i++) {
        var F, g;
        if (i < 16) { F = (B & C) | (~B & D); g = i; }
        else if (i < 32) { F = (D & B) | (~D & C); g = (5 * i + 1) % 16; }
        else if (i < 48) { F = B ^ C ^ D; g = (3 * i + 5) % 16; }
        else { F = C ^ (B | ~D); g = (7 * i) % 16; }
        F = (F + A + K[i] + M[blk + g]) >>> 0;
        A = D; D = C; C = B;
        var sh = S[(i >> 4) * 4 + (i % 4)];
        B = (B + ((F << sh) | (F >>> (32 - sh)))) >>> 0;
      }
      a0 = (a0 + A) >>> 0; b0 = (b0 + B) >>> 0; c0 = (c0 + C) >>> 0; d0 = (d0 + D) >>> 0;
    }
    var out = "", w = [a0, b0, c0, d0];
    for (i = 0; i < 4; i++) {
      for (var k = 0; k < 4; k++) {
        var byte = (w[i] >>> (k * 8)) & 0xff;
        out += (byte < 16 ? "0" : "") + byte.toString(16);
      }
    }
    return out;
  }

  return {
    BOARD_NAME: BOARD_NAME,
    MODEM_NAME: MODEM_NAME,
    FIRST_INSTALL_WARNING: FIRST_INSTALL_WARNING,
    selection: selection,
    matchPreset: matchPreset,
    checkDeps: checkDeps,
    fingerprint: fingerprint,
    validFingerprint: validFingerprint,
    parseTable: parseTable,
    tablesEqual: tablesEqual,
    isMultimode: isMultimode,
    parseAppHeader: parseAppHeader,
    radioEnv: radioEnv,
    planFlash: planFlash,
    guardRegions: guardRegions,
    stageName: stageName,
    blockBar: blockBar,
    hexOffset: hexOffset,
    buildCommand: buildCommand,
    md5: md5,
    toHex: toHex
  };
});
