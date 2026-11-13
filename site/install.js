// AXIS Micro BBS installer page: the DOM, the downloads and the serial port.
// The decisions (what to write, dependencies, fingerprints) live in installer-logic.js (window.AxisLogic).
// The flasher is esptool-js, pinned, loaded from jsDelivr only when INSTALL is pressed.

const L = window.AxisLogic;
const ESPTOOL_URL = "https://cdn.jsdelivr.net/npm/esptool-js@0.5.7/bundle.js";
const REPO = "https://github.com/revjmoney/axis-bbs";
const FLASH_BAUD = 460800;
const CONSOLE_BAUD = 115200;
const PROJECT = { board: "axis-bbs", radio: "lfd3k_sm" };
const RADIO_ENVS = { v4: ["heltec_v4", "heltec_v4_single"], v3: ["heltec_v3_single"] };
const KIT_NOTE = "The public kit holds only the card files kept in the AXIS repo (the scripts folder and a README). Theme packs and LORD's game data are not in it.";

const $ = (id) => document.getElementById(id);
const params = new URLSearchParams(location.search);
const isLocal = ["localhost", "127.0.0.1", "[::1]"].includes(location.hostname);
// A simulated board for checking this page without hardware; honoured on a local server only.
const SIM = isLocal ? params.get("sim") : null;

const state = {
  features: [], presets: [], radio: { version: "" }, index: null,
  release: null, costs: null,
  target: "board", board: "cyd", radioModel: "v4",
  build: { type: "preset", id: "everything" },
  custom: null, // { fp, off, release, manifest } from ?build=
  busy: false,
};

// == small helpers ==

async function getJson(url) {
  const r = await fetch(url, { cache: "no-cache" });
  if (!r.ok) throw new Error(url + ": HTTP " + r.status);
  return r.json();
}

async function getBytes(url) {
  const r = await fetch(url, { cache: "no-cache" });
  if (!r.ok) throw new Error(url + ": HTTP " + r.status);
  return new Uint8Array(await r.arrayBuffer());
}

async function sha256Hex(bytes) {
  return L.toHex(new Uint8Array(await crypto.subtle.digest("SHA-256", bytes)));
}

function el(tag, attrs, ...kids) {
  const e = document.createElement(tag);
  for (const k in attrs || {}) {
    if (k === "class") e.className = attrs[k];
    else if (k === "text") e.textContent = attrs[k];
    else e.setAttribute(k, attrs[k]);
  }
  for (const k of kids) if (k != null) e.append(k);
  return e;
}

function icon(id, scale) {
  return el("img", { class: "ico x" + (scale || 1), src: "icons/" + id + ".svg", alt: "", width: 16 * (scale || 1), height: 16 * (scale || 1) });
}

function sdBadge() {
  return el("span", { class: "sd-badge", title: "needs the SD card", text: "SD" });
}

function kb(bytes) {
  return Math.round(bytes / 1024).toLocaleString("en-US") + " KB";
}

function mb(bytes) {
  return (bytes / 1048576).toFixed(2) + " MB";
}

function bstr(bytes) {
  let s = "";
  for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
  return s;
}

const releaseBase = () => "releases/" + state.release + "/";
const featureById = (id) => state.features.find((f) => f.id === id);
const selectable = () => state.features.filter((f) => !f.reserved);

// == the log, the bar and the lights ==

const log = {
  line(text, cls) {
    const box = $("log");
    const t = new Date().toTimeString().slice(0, 8);
    const row = el("div", cls ? { class: cls } : {}, "[" + t + "] " + text);
    box.append(row);
    box.scrollTop = box.scrollHeight;
  },
  clear() { $("log").textContent = ""; },
};

function setBar(percent, done, total) {
  $("bar").textContent = L.blockBar(percent, 40);
  if (total) $("stage").textContent = L.stageName(percent) + " ... " + kb(done) + " of " + kb(total);
}

function startLights() {
  const leds = document.querySelectorAll("#lights .led");
  const still = matchMedia("(prefers-reduced-motion: reduce)");
  leds.forEach((l, i) => l.classList.toggle("on", i % 3 === 0));
  setInterval(() => {
    if (still.matches) return;
    leds.forEach((l, i) => {
      const fast = state.busy && (i === 2 || i === 3); // SD and RD chatter while flashing
      if (Math.random() < (fast ? 0.8 : i === 0 || i === 7 ? 0.15 : 0.45)) l.classList.toggle("on");
    });
  }, 220);
}

// == loading the lists ==

async function load() {
  if (!("serial" in navigator) && !SIM) $("browser-note").hidden = false;
  try {
    [state.features, state.presets] = await Promise.all([getJson("installer/features.json"), getJson("installer/presets.json")]);
  } catch (e) {
    log.line("Could not load the feature lists: " + e.message, "err");
    return;
  }
  try { state.radio = await getJson("installer/radio.json"); } catch (e) { state.radio = { version: "" }; }
  try { state.index = await getJson("releases/index.json"); } catch (e) { state.index = null; }

  const fp = params.get("build");
  if (fp !== null) await loadCustom(fp);

  fillReleases();
  setupRadioChoices();
  renderFeatures();
  await selectRelease(state.release);
  bindInputs();
  update();
}

async function loadCustom(fp) {
  if (!L.validFingerprint(fp)) {
    log.line("That build link is not valid (a build is 12 letters and digits, 0 to 9 and a to f).", "err");
    return;
  }
  try {
    const m = await getJson("builds/" + fp + "/manifest.json");
    const check = await L.fingerprint(m.off || [], m.release || "");
    if (check !== fp || m.fingerprint !== fp) throw new Error("its fingerprint does not match");
    state.custom = { fp, off: L.selection(m.off || [], state.presets), release: m.release, manifest: m, base: "builds/" + fp + "/" };
    state.build = { type: "custom" };
    log.line("Custom build " + fp + " for release " + m.release + " loaded.", "note");
  } catch (e) {
    log.line("Custom build " + fp + " could not be loaded (" + e.message + "). If it was just requested, it may still be building.", "err");
  }
}

function fillReleases() {
  const sel = $("release");
  sel.textContent = "";
  const idx = state.index;
  const tags = [];
  if (state.custom) tags.push([state.custom.release, state.custom.release + " (your custom build)"]);
  else if (idx && idx.latest) {
    tags.push([idx.latest, idx.latest + " (latest)"]);
    if (idx.previous) tags.push([idx.previous, idx.previous + " (previous)"]);
  }
  if (!tags.length) {
    sel.append(el("option", { text: "no release yet" }));
    sel.disabled = true;
    $("release-note").textContent = "No AXIS Micro BBS release has been published to this site yet.";
    state.release = null;
    return;
  }
  for (const [v, t] of tags) sel.append(el("option", { value: v, text: t }));
  sel.disabled = tags.length < 2;
  state.release = tags[0][0];
  $("release-note").textContent = state.custom ? "" : "The previous release is here if the latest one misbehaves.";
}

async function selectRelease(tag) {
  state.release = tag;
  state.costs = null;
  if (tag) {
    try { state.costs = await getJson(releaseBase() + "costs.json"); } catch (e) { state.costs = null; }
  }
  document.getElementById("marquee").textContent =
    (tag || "AXIS") + " AXIS MICRO BBS  ///  BL4Z3B4ND ULTR4M0D3M  ///  S0N1C B00M R4D10 PR0 MAX  ///  plug in, pick a build, press INSTALL  ///  your users, messages and settings stay  ///  AXIS UNDERGROUND CNC  ///";
  renderPresets();
  update();
}

function setupRadioChoices() {
  const ready = !!state.radio.version;
  document.querySelectorAll("[data-radio]").forEach((c) => {
    c.classList.toggle("disabled", !ready);
    c.querySelector("input").disabled = !ready;
  });
  $("radio-wait").hidden = ready;
}

// == step 2: presets and custom ==

function renderPresets() {
  const table = $("ptable");
  table.querySelectorAll(".prow:not(.phead)").forEach((r) => r.remove());
  const rows = state.costs ? state.costs.presets : [];
  let maxRam = 0;
  for (const p of state.presets) {
    const c = rows.find((r) => r.id === p.id);
    if (c) maxRam = Math.max(maxRam, c.dram_static_freed);
    const on = selectable().filter((f) => !p.off.includes(f.id));
    const mini = el("div", { class: "mini" });
    for (const f of on) {
      mini.append(el("span", { title: f.label + (f.needs_sd ? " (needs the SD card)" : "") }, icon(f.id, 1), f.needs_sd ? sdBadge() : null));
    }
    const input = el("input", { type: "radio", name: "preset", value: p.id, "aria-label": p.label });
    const label = el("div", { class: "pname" }, input, icon(p.id, 2),
      el("span", {}, el("b", { text: p.label }),
        p.id === "no-sd" ? el("span", { class: "small-note", text: "for a board that runs alone with no SD card" }) : null));
    const spikes = c ? (c.spikes_gone.length
      ? c.spikes_gone.map((s) => (featureById(s.id) || { label: s.id }).label + " " + s.peak_kb + " KB").join(", ") + (c.spikes_gone.length > 1 ? " (" + c.spikes_gone_kb + " KB)" : "")
      : "none") : "";
    const spikeWhy = c ? c.spikes_gone.map((s) => s.what).join("; ") : "";
    const row = el("label", { class: "prow", "data-id": p.id },
      label,
      el("div", {}, el("span", { text: p.about }), mini),
      el("div", { class: "r", text: c ? mb(c.flash) + " (" + c.flash_pct + "%)" : "not measured" }),
      el("div", { class: "r", text: c ? (c.flash_freed ? kb(c.flash_freed) : "none") : "" }),
      el("div", { text: spikes, title: spikeWhy }));
    input.addEventListener("change", () => { state.build = { type: "preset", id: p.id }; update(); });
    table.append(row);
  }
  $("cost-note").textContent = state.costs
    ? "Measured by the release build of " + state.release + ". Flash used is the firmware's size in its 2.25 MB app partition. Static RAM changes by at most " + kb(maxRam) + " between builds."
    : state.release ? "The cost table for " + state.release + " is missing." : "";
}

function renderFeatures() {
  const grid = $("feature-grid");
  grid.textContent = "";
  for (const f of selectable()) {
    const box = el("input", { type: "checkbox", value: f.id });
    box.checked = true;
    box.addEventListener("change", customChanged);
    grid.append(el("label", { class: "feat" }, box, icon(f.id, 2),
      el("span", {}, el("b", { text: f.label }), " ", f.needs_sd ? sdBadge() : null, el("span", { class: "about", text: f.about }))));
  }
  if (state.custom) {
    grid.querySelectorAll("input").forEach((b) => { b.checked = !state.custom.off.includes(b.value); });
  }
}

function customOff() {
  return [...$("feature-grid").querySelectorAll("input")].filter((b) => !b.checked).map((b) => b.value);
}

let customSeq = 0;
async function customChanged() {
  const seq = ++customSeq;
  const off = customOff();
  const status = $("custom-status");
  const actions = $("custom-actions");
  actions.textContent = "";
  status.className = "custom-status";
  const why = L.checkDeps(off, state.features);
  if (why) {
    status.classList.add("bad");
    status.textContent = "That combination cannot be built: " + why + ".";
    return;
  }
  const preset = L.matchPreset(off, state.presets);
  if (preset) {
    const p = state.presets.find((x) => x.id === preset);
    status.textContent = "That is the " + p.label + " build, already in the table.";
    const use = el("button", { type: "button", class: "btn-plain", text: "Use " + p.label });
    use.addEventListener("click", () => selectPreset(preset));
    actions.append(use);
    return;
  }
  const latest = state.index && state.index.latest;
  if (!latest) {
    status.textContent = "Custom builds start once the first release is out.";
    return;
  }
  const fp = await L.fingerprint(off, latest);
  if (seq !== customSeq) return;
  status.textContent = "Checking whether build " + fp + " exists...";
  let exists = false;
  try { await getJson("builds/" + fp + "/manifest.json"); exists = true; } catch (e) { exists = false; }
  if (seq !== customSeq) return;
  if (exists) {
    status.textContent = "This combination is already built (build " + fp + ", release " + latest + ").";
    const use = el("a", { class: "btn-plain", href: "install.html?build=" + fp, text: "Use build " + fp });
    actions.append(use);
    return;
  }
  const board = state.board === "esp32" ? "Bare ESP32 (no screen, headless)" : "CYD (the board with the screen)";
  const url = REPO + "/issues/new?template=build-request.yml&board=" + encodeURIComponent(board);
  status.textContent = "Not built yet. Ask GitHub to build it (for release " + latest + "). On the form, tick exactly these boxes:";
  const list = el("ul", { class: "ticklist" });
  for (const id of L.selection(off, state.presets)) {
    const f = featureById(id);
    list.append(el("li", { text: f.label + " (" + f.id + ")" }));
  }
  const ask = el("a", { class: "btn-plain", href: url, target: "_blank", rel: "noopener", text: "Open the build request form" });
  actions.append(list, ask, el("span", { class: "dim", text: "The robot answers on the issue with a link that opens this page set to your build." }));
}

function selectPreset(id) {
  state.build = { type: "preset", id };
  update();
}

// == the summary, the selection and the PC command ==

function bindInputs() {
  document.querySelectorAll("input[name=target]").forEach((r) => r.addEventListener("change", () => update()));
  document.querySelectorAll("input[name=board]").forEach((r) => r.addEventListener("change", () => { update(); if (!$("custom-panel").hidden) customChanged(); }));
  document.querySelectorAll("input[name=radio]").forEach((r) => r.addEventListener("change", () => update()));
  $("release").addEventListener("change", (e) => selectRelease(e.target.value));
  $("custom-btn").addEventListener("click", () => {
    const panel = $("custom-panel");
    panel.hidden = !panel.hidden;
    $("custom-btn").setAttribute("aria-expanded", String(!panel.hidden));
    if (!panel.hidden) customChanged();
  });
  $("install-btn").addEventListener("click", () => install());
}

function buildLabel() {
  if (state.build.type === "custom") return "custom " + state.custom.fp;
  const p = state.presets.find((x) => x.id === state.build.id);
  return '"' + (p ? p.label : state.build.id) + '"';
}

function readChoices() {
  const pick = (name, fallback) => {
    const r = document.querySelector("input[name=" + name + "]:checked");
    return r && !r.disabled ? r.value : fallback;
  };
  state.target = pick("target", "board");
  state.board = pick("board", "cyd");
  state.radioModel = pick("radio", "v4");
}

function update() {
  readChoices();
  const wantsBoard = state.target !== "radio";
  const wantsRadio = state.target !== "board";
  $("board-pick").classList.toggle("off", !wantsBoard);
  $("radio-pick").classList.toggle("off", !wantsRadio);
  $("step2").classList.toggle("pick-off", !wantsBoard);
  document.querySelectorAll(".prow:not(.phead)").forEach((r) => {
    const sel = state.build.type === "preset" && r.dataset.id === state.build.id;
    r.classList.toggle("sel", sel);
    r.querySelector("input").checked = sel;
  });

  const parts = [];
  if (wantsBoard) parts.push("AXIS Micro BBS on a " + (state.board === "esp32" ? "bare ESP32" : "CYD") + ", build " + buildLabel() + ", release " + (state.release || "none"));
  if (wantsRadio) parts.push("LRA FLOWTRON DATAMASTER 3000 SERIAL MODEM - LFD3K-SM on a Heltec " + state.radioModel.toUpperCase() + ", release " + (state.radio.version || "none yet"));
  $("summary").textContent = "You picked: " + parts.join("; then ") + ".";

  const radioReady = !!state.radio.version;
  const canFlash = ("serial" in navigator || !!SIM) && !state.busy &&
    (!wantsBoard || !!state.release) && (!wantsRadio || radioReady);
  $("install-btn").disabled = !canFlash;

  $("kit-line").textContent = "";
  if (state.release && wantsBoard) {
    $("kit-line").append("First install on a new board? Get the ",
      el("a", { href: releaseBase() + "sdcard-kit.zip", text: "SD card kit for " + state.release }),
      " and unzip it onto a FAT32 card. " + KIT_NOTE);
  }

  const picks = {
    target: state.target, board: state.board, radio: state.radioModel, release: state.release,
    preset: state.build.type === "preset" ? state.build.id : null,
    off: state.build.type === "custom" ? state.custom.off : null,
  };
  const base = new URL(".", location.href).href;
  $("cmd-win").textContent = L.buildCommand(picks, "windows", base);
  $("cmd-linux").textContent = L.buildCommand(picks, "linux", base);
}

// == the device: esptool-js on a real port, or the local simulator ==

async function openDevice(port, job) {
  if (SIM) return simDevice(SIM, job);
  log.line("Loading the flasher (esptool-js 0.5.7) ...");
  const esp = await import(ESPTOOL_URL);
  const info = port.getInfo ? port.getInfo() : {};
  if (info.usbVendorId) log.line("USB device " + info.usbVendorId.toString(16).padStart(4, "0") + ":" + (info.usbProductId || 0).toString(16).padStart(4, "0"));
  const transport = new esp.Transport(port, false);
  const terminal = {
    clean() {},
    writeLine(s) { quiet(s) || log.line(s); },
    write(s) { if (s && s.trim()) quiet(s) || log.line(s.trim()); },
  };
  const loader = new esp.ESPLoader({ transport, baudrate: FLASH_BAUD, romBaudrate: CONSOLE_BAUD, terminal, debugLogging: false });
  log.line("Connecting to the boot loader (hold BOOT if it does not answer) ...");
  const desc = await loader.main("default_reset");
  const chip = loader.chip.CHIP_NAME;
  const mac = await loader.chip.readMac(loader);
  return {
    chip, mac, desc,
    read: (addr, len) => loader.readFlash(addr, len),
    async write(files, onProgress) {
      await loader.writeFlash({
        fileArray: files.map((f) => ({ data: bstr(f.data), address: f.offset })),
        flashSize: "keep", flashMode: "keep", flashFreq: "keep",
        eraseAll: false, compress: true,
        reportProgress: onProgress,
        calculateMD5Hash: (image) => L.md5(image),
      });
    },
    async reset() { await loader.after("hard_reset"); },
    async close() { try { await transport.disconnect(); } catch (e) { /* already closed */ } },
    async console(line, expect, ms) {
      await port.open({ baudRate: CONSOLE_BAUD });
      try {
        if (port.setSignals) await port.setSignals({ dataTerminalReady: false, requestToSend: false });
        return await talk(port, line, expect, ms);
      } finally {
        try { await port.close(); } catch (e) { /* closed */ }
      }
    },
  };
}

// esptool-js prints a line per block written; the bar shows those, the log keeps the rest.
function quiet(s) {
  return /^Writing at 0x/.test(s) || /^\s*$/.test(s);
}

// Send a line every few seconds until the board answers with `expect` or `ms` runs out.
async function talk(port, line, expect, ms) {
  const reader = port.readable.getReader();
  const writer = port.writable.getWriter();
  const enc = new TextEncoder(), dec = new TextDecoder();
  let seen = "", answer = null;
  const end = Date.now() + ms;
  const pump = (async () => {
    try {
      for (;;) {
        const { value, done } = await reader.read();
        if (done) break;
        seen += dec.decode(value, { stream: true });
        const m = seen.match(expect);
        if (m) { answer = m[0]; break; }
        if (seen.length > 20000) seen = seen.slice(-4000);
      }
    } catch (e) { /* port closed */ }
  })();
  try {
    while (!answer && Date.now() < end) {
      await writer.write(enc.encode("\r\n" + line + "\r\n"));
      await new Promise((r) => setTimeout(r, 3000));
    }
  } finally {
    try { await reader.cancel(); } catch (e) { /* done */ }
    await pump;
    reader.releaseLock();
    writer.releaseLock();
  }
  return answer;
}

// A pretend board for checking the page without hardware (?sim=update|first|wrongchip|multimode|fail, localhost only).
function simDevice(scenario, job) {
  const radio = job === "radio";
  const chip = scenario === "wrongchip" ? (radio ? "ESP32" : "ESP32-S3") : radio ? "ESP32-S3" : "ESP32";
  let ctx = { table: null, app: null, appOffset: 0x10000, multimode: null };
  const blank = (n) => new Uint8Array(n).fill(0xff);
  const wait = (ms) => new Promise((r) => setTimeout(r, ms));
  log.line("SIMULATED board (" + scenario + "): nothing real is written.", "note");
  return {
    chip, mac: radio ? "88:56:a6:00:00:01" : "8c:94:df:00:00:01", desc: chip + " (simulated)", sim: true,
    prime(c) { ctx = Object.assign(ctx, c); },
    async read(addr, len) {
      await wait(150);
      if (scenario === "update" && addr === 0x8000 && ctx.table) return ctx.table.slice(0, len);
      if (scenario === "update" && addr === ctx.appOffset && ctx.app) return ctx.app.slice(0, len);
      if (scenario === "multimode" && addr === 0x8000 && ctx.multimode) return ctx.multimode.slice(0, len);
      return blank(len);
    },
    async write(files, onProgress) {
      for (let i = 0; i < files.length; i++) {
        const total = Math.ceil(files[i].data.length * 0.6); // about what deflate gives
        onProgress(i, 0, total);
        for (let w = 0; w < total;) {
          await wait(25);
          w = Math.min(total, w + 0x4000);
          if (scenario === "fail" && i === files.length - 1 && w > total / 2) throw new Error("Timeout waiting for the chip (simulated)");
          onProgress(i, w, total);
        }
        log.line("Hash of data verified.");
      }
    },
    async reset() { await wait(100); },
    async close() {},
    async console() { await wait(800); return "axis: saved, restarting"; },
  };
}

// == installing ==

function confirmFirst(plan) {
  return new Promise((resolve) => {
    const box = $("warn-box"), check = $("warn-check"), go = $("warn-go"), cancel = $("warn-cancel");
    $("warn-text").textContent = L.FIRST_INSTALL_WARNING;
    $("warn-why").textContent = plan.reason.replace(L.FIRST_INSTALL_WARNING, "").trim() +
      " It writes: " + plan.regions.map((r) => r.role + " at " + L.hexOffset(r.offset)).join(", ") + ".";
    $("warn-kit").textContent = "";
    if (plan.kit) {
      $("warn-kit").append("After it boots, the board runs its setup wizard. For full functions it needs an SD card: the ",
        el("a", { href: plan.kit, text: "SD card kit" }), " has the starting files. " + KIT_NOTE);
    }
    check.checked = false;
    go.disabled = true;
    box.hidden = false;
    box.scrollIntoView({ block: "center", behavior: "smooth" });
    const onCheck = () => { go.disabled = !check.checked; };
    const done = (ok) => {
      box.hidden = true;
      check.removeEventListener("change", onCheck);
      go.removeEventListener("click", yes);
      cancel.removeEventListener("click", no);
      resolve(ok);
    };
    const yes = () => done(true);
    const no = () => done(false);
    check.addEventListener("change", onCheck);
    go.addEventListener("click", yes);
    cancel.addEventListener("click", no);
  });
}

async function boardTarget() {
  if (state.build.type === "custom") return { manifest: state.custom.manifest, base: state.custom.base };
  const base = releaseBase() + state.build.id + "/";
  return { manifest: await getJson(base + "manifest.json"), base };
}

async function radioTarget(env) {
  const site = new URL(state.radio.site, location.href).href;
  const base = site + state.radio.version + "/" + env + "/";
  return { manifest: await getJson(base + "manifest.json"), base };
}

async function fetchImages(manifest, base) {
  const out = [];
  for (const img of manifest.images) {
    const data = await getBytes(base + img.file);
    if (data.length !== img.size) throw new Error(img.file + " is " + data.length + " bytes, the manifest says " + img.size);
    const sum = await sha256Hex(data);
    if (sum !== String(img.sha256).toLowerCase()) throw new Error(img.file + " does not match its sha256");
    log.line("Downloaded " + img.file + " (" + kb(data.length) + "), sha256 checked");
    out.push(Object.assign({}, img, { data }));
  }
  return out;
}

async function tableOf(manifest, base, images) {
  const name = manifest.table || (manifest.images.find((i) => i.role === "partitions") || {}).file;
  if (!name) throw new Error("the manifest names no partition table");
  const have = images.find((i) => i.file === name);
  return have ? have.data : getBytes(base + name);
}

// One job: the board or the radio, on the port the visitor picked.
async function flashJob(job, port) {
  const name = job === "radio" ? L.MODEM_NAME : L.BOARD_NAME;
  $("flash-title").textContent = "FLASHING: " + (job === "radio" ? name + ", Heltec " + state.radioModel.toUpperCase() : name + ", " + buildLabel());
  setBar(0);
  $("stage").textContent = "connecting";
  const dev = await openDevice(port, job);
  let wrote = false;
  try {
    log.line("Chip: " + dev.desc + ", MAC " + dev.mac);
    log.line("Reading the partition table at 0x8000 ...");
    let devTable = await dev.read(0x8000, 0xc00);

    let t;
    if (job === "radio") {
      if (dev.sim && SIM === "multimode") {
        const mm = await radioTarget("heltec_v4");
        dev.prime({ multimode: await tableOf(mm.manifest, mm.base, []) });
        devTable = await dev.read(0x8000, 0xc00);
      }
      const env = L.radioEnv(state.radioModel, devTable);
      log.line("Radio image: " + env + (env === "heltec_v4" ? " (this V4 has the multimode layout: the modem slot only)" : ""));
      t = await radioTarget(env);
    } else {
      t = await boardTarget();
    }
    const m = t.manifest;
    const target = { kind: job, chip: m.chip, project: PROJECT[job], layout: m.layout || "single", partTable: null, images: m.images };
    const device = { chip: dev.chip, partTable: devTable, appHeader: null, bootloaderSha256: null };

    let plan = device.chip === target.chip ? null : L.planFlash(device, target);
    let images = [];
    if (!plan) {
      images = await fetchImages(m, t.base);
      target.partTable = await tableOf(m, t.base, images);
      const app = images.find((i) => i.role === "app");
      if (dev.prime) dev.prime({ table: target.partTable, app: app.data, appOffset: app.offset });
      if (dev.sim && SIM === "update") devTable = device.partTable = await dev.read(0x8000, 0xc00);
      device.appHeader = await dev.read(app.offset, 0x100);
      const hdr = L.parseAppHeader(device.appHeader);
      log.line("At " + L.hexOffset(app.offset) + ": " + (hdr ? hdr.project + " " + hdr.version : "no program"));
      const bl = images.find((i) => i.role === "bootloader");
      if (bl) device.bootloaderSha256 = await sha256Hex(await dev.read(bl.offset, bl.size));
      plan = L.planFlash(device, target);
    }
    log.line(plan.reason, plan.kind === "refuse" ? "err" : "note");
    if (plan.kind === "refuse") {
      $("stage").textContent = "REFUSED: nothing was written";
      await dev.reset(); // back to what it ran before
      return false;
    }
    if (plan.kind === "first") {
      if (job === "board" && state.release) plan.kit = releaseBase() + "sdcard-kit.zip";
      log.line("First install: waiting for your confirmation ...", "note");
      $("stage").textContent = "FIRST INSTALL: waiting for your confirmation";
      if (!(await confirmFirst(plan))) {
        log.line("Cancelled. Nothing was written.", "note");
        $("stage").textContent = "cancelled: nothing was written";
        await dev.reset();
        return false;
      }
      log.line("Confirmed: replacing everything on this board.", "note");
    }
    const appImg = plan.regions.find((r) => r.role === "app") || images.find((i) => i.role === "app");
    L.guardRegions(plan, L.parseTable(devTable), appImg.offset);
    const files = plan.regions.map((r) => images.find((i) => i.role === r.role && i.offset === r.offset));
    const total = files.reduce((n, f) => n + f.data.length, 0);
    const before = [];
    let sum = 0;
    for (const f of files) { before.push(sum); sum += f.data.length; }
    let lastTenth = -1, current = -1;
    state.busy = true;
    wrote = true;
    await dev.write(files, (i, written, size) => {
      if (i !== current) {
        current = i;
        log.line("Writing " + files[i].role + " (" + files[i].file + ", " + kb(files[i].data.length) + ") at " + L.hexOffset(files[i].offset) + " ...");
      }
      const done = before[i] + (size ? Math.round(files[i].data.length * written / size) : 0);
      const pct = total ? (100 * done) / total : 100;
      setBar(pct, done, total);
      const tenth = Math.floor(pct / 10);
      if (tenth !== lastTenth && tenth > 0) { lastTenth = tenth; log.line("Wrote " + kb(done) + " of " + kb(total) + " (" + Math.floor(pct) + "%)"); }
    });
    setBar(100, total, total);
    log.line("Resetting the " + (job === "radio" ? "radio" : "board") + " ...");
    await dev.reset();
    if (job === "board" && state.board === "esp32") {
      await dev.close();
      log.line("Bare ESP32: sending @HEADLESS on at " + CONSOLE_BAUD + " baud once it boots ...");
      const answer = await dev.console("@HEADLESS on", /axis: (saved, restarting|headless SAVE FAILED)/, 45000);
      if (answer && answer.indexOf("saved") >= 0) log.line("The board answered: " + answer + ". It stays headless through later flashes.", "note");
      else log.line("The board did not confirm headless. Open a serial terminal at 115200 baud and type @HEADLESS on", "err");
    }
    if (job === "board") {
      log.line(plan.kind === "update"
        ? "DONE. Your AXIS Micro BBS restarts with its users, messages and settings."
        : "DONE. Your AXIS Micro BBS starts its setup wizard: join its AXIS-SETUP hotspot to set it up.", "note");
    } else {
      log.line("DONE. The " + L.MODEM_NAME + " is restarting.", "note");
    }
    $("stage").textContent = "DONE: the " + (job === "radio" ? "radio" : "board") + " is restarting";
    return true;
  } catch (e) {
    log.line("STOPPED: " + e.message, "err");
    log.line(wrote
      ? "The write did not finish, so the board may not start. Unplug it, plug it back in and press INSTALL again."
      : "Nothing was written. Unplug the board, plug it back in and press INSTALL again.", "err");
    $("stage").textContent = "STOPPED";
    return false;
  } finally {
    state.busy = false;
    await dev.close();
  }
}

async function pickPort() {
  if (SIM) return null;
  return navigator.serial.requestPort();
}

async function install() {
  if (state.busy) return;
  let port;
  try {
    port = await pickPort(); // first, while the click still counts as a user gesture
  } catch (e) {
    log.line("No port picked. Plug the board in with a data USB cable and press INSTALL again.", "err");
    return;
  }
  state.busy = true;
  update();
  log.clear();
  try {
    const jobs = state.target === "both" ? ["board", "radio"] : [state.target];
    for (let n = 0; n < jobs.length; n++) {
      if (n > 0) {
        const ok = await waitNext();
        if (!ok) break;
        try { port = await pickPort(); } catch (e) { log.line("No port picked for the radio.", "err"); break; }
      }
      state.busy = true;
      const ok = await flashJob(jobs[n], port);
      if (!ok) break;
    }
  } finally {
    state.busy = false;
    update();
  }
}

function waitNext() {
  return new Promise((resolve) => {
    const box = $("next-box"), go = $("next-go");
    box.hidden = false;
    const yes = () => { box.hidden = true; go.removeEventListener("click", yes); resolve(true); };
    go.addEventListener("click", yes);
  });
}

startLights();
load();
