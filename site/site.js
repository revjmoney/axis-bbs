/* Axis Micro BBS Firmware site: the wiki router, topic search, copy buttons, screenshot lightbox.
   Copyright (C) 2026 Rev. J. Money. GPL-3.0-or-later. */
(function () {
  "use strict";

  /* ---------- the docs wiki: one topic at a time, chosen by the hash ---------- */
  var wiki = document.querySelector(".wiki");
  if (wiki) {
    var hub = document.getElementById("hub");
    var topics = Array.prototype.slice.call(document.querySelectorAll("article.topic"));
    var toc = document.querySelector(".toc");
    var wide = window.matchMedia("(min-width: 961px)");
    var sum = toc ? toc.querySelector("summary") : null;
    var baseTitle = document.title;
    var fitToc = function () {
      if (!toc) return;
      if (wide.matches) { toc.setAttribute("open", ""); sum.setAttribute("tabindex", "-1"); }
      else { toc.removeAttribute("open"); sum.removeAttribute("tabindex"); }
    };
    fitToc();
    if (wide.addEventListener) wide.addEventListener("change", fitToc);

    var sideLinks = Array.prototype.slice.call(document.querySelectorAll(".side a[href^='#']"));
    var show = function (first) {
      var id = decodeURIComponent(location.hash.slice(1));
      var target = id && /^[A-Za-z0-9-]+$/.test(id) ? document.getElementById(id) : null;
      var topic = target ? target.closest("article.topic") : null;
      var inHub = target && !topic && hub.contains(target);
      if (target && !topic && !inHub) return; /* some other anchor (skip link): leave the view alone */
      if (inHub && target !== hub) { /* an area in the area list: the hub, scrolled to that area */
        topics.forEach(function (t) { t.hidden = true; });
        hub.hidden = false;
        document.title = baseTitle;
        target.scrollIntoView();
        return;
      }
      topics.forEach(function (t) { t.hidden = t !== topic; });
      hub.hidden = !!topic;
      sideLinks.forEach(function (a) {
        var on = topic ? a.getAttribute("href") === "#" + topic.id : a.classList.contains("side-hub");
        a.classList.toggle("active", on);
        if (on) a.setAttribute("aria-current", "page"); else a.removeAttribute("aria-current");
      });
      document.title = topic ? topic.querySelector("h2").textContent + ": " + baseTitle : baseTitle;
      if (toc && !wide.matches) toc.removeAttribute("open");
      if (topic && target !== topic) { /* a section inside a topic: open it and go there */
        for (var d = target; d && d !== topic; d = d.parentElement) if (d.tagName === "DETAILS") d.open = true;
        target.scrollIntoView();
      } else if (!first || topic) {
        var top = function () { window.scrollTo({ top: 0, left: 0, behavior: "instant" }); };
        top();
        setTimeout(top, 0); /* after the browser's own jump to the anchor */
        if (first) window.addEventListener("load", function () { setTimeout(top, 0); });
      }
      if (!first && topic) {
        var h = topic.querySelector("h2");
        h.setAttribute("tabindex", "-1");
        h.focus({ preventScroll: true });
      }
    };
    document.documentElement.classList.add("wiki-on");
    show(true);
    window.addEventListener("hashchange", function () { show(false); });

    /* search: filters the hub cards and the sidebar */
    var inputs = Array.prototype.slice.call(document.querySelectorAll(".doc-search"));
    var cards = Array.prototype.slice.call(document.querySelectorAll(".tcard"));
    var noHits = document.querySelector(".no-hits");
    var filter = function (q) {
      q = q.trim().toLowerCase();
      var words = q ? q.split(/\s+/) : [];
      var match = function (el) {
        var k = el.getAttribute("data-k") || "";
        return words.every(function (w) { return k.indexOf(w) >= 0; });
      };
      var hits = 0;
      cards.forEach(function (c) { var m = match(c); c.hidden = !m; if (m) hits++; });
      document.querySelectorAll(".hub-group").forEach(function (g) { g.hidden = !g.querySelector(".tcard:not([hidden])"); });
      sideLinks.forEach(function (a) { if (a.hasAttribute("data-k")) a.parentElement.hidden = !match(a); });
      document.querySelectorAll(".side-group").forEach(function (g) { g.hidden = !g.querySelector("li:not([hidden])"); });
      if (noHits) noHits.hidden = hits > 0;
    };
    inputs.forEach(function (inp) {
      inp.addEventListener("input", function () {
        inputs.forEach(function (o) { if (o !== inp) o.value = inp.value; });
        filter(inp.value);
      });
      inp.addEventListener("keydown", function (e) {
        if (e.key !== "Enter") return;
        var first = cards.filter(function (c) { return !c.hidden; })[0];
        if (first) location.hash = first.getAttribute("href");
      });
    });
  }

  var reduced = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  var BUY = "https://jmscnc.com/product/axis-underground-bbs-ready-to-plug-in/";
  var onDocs = !!wiki;

  /* ---------- connecting: the splash plays once a visit, then the matrix ---------- */
  var splash = document.querySelector(".term-splash");
  var matrix = document.querySelector(".term-matrix");
  if (splash && matrix && !reduced) {
    var seen = false;
    try { seen = sessionStorage.getItem("axis-connected") === "1"; sessionStorage.setItem("axis-connected", "1"); } catch (e) {}
    if (!seen) {
      var done = false, timer;
      var toMatrix = function () {
        if (done) return;
        done = true;
        clearTimeout(timer);
        splash.hidden = true;
        matrix.hidden = false;
        document.removeEventListener("keydown", skip, true);
        splash.removeEventListener("click", toMatrix);
      };
      var skip = function (e) { if (!e.ctrlKey && !e.metaKey && !e.altKey && e.key !== "Tab") { e.preventDefault(); e.stopPropagation(); toMatrix(); } };
      matrix.hidden = true;
      splash.hidden = false;
      timer = setTimeout(toMatrix, 4200);
      document.addEventListener("keydown", skip, true);
      splash.addEventListener("click", toMatrix);
    }
  }

  /* ---------- the matrix lightbar: arrows move it, Enter picks, hover lights ---------- */
  var bar = Array.prototype.slice.call(document.querySelectorAll(".lightbar a"));
  var lit = 0;
  var light = function (i, focus) {
    lit = (i + bar.length) % bar.length;
    bar.forEach(function (a, k) { a.classList.toggle("lit", k === lit); a.tabIndex = k === lit ? 0 : -1; });
    if (focus) bar[lit].focus();
  };
  if (bar.length) {
    light(0, false);
    bar.forEach(function (a, k) {
      a.addEventListener("mouseenter", function () { light(k, false); });
      a.addEventListener("focus", function () { light(k, false); });
    });
  }
  var barVisible = function () {
    if (!bar.length || bar[0].offsetParent === null) return false;
    var r = bar[0].getBoundingClientRect();
    return r.top < window.innerHeight && r.bottom > 0;
  };

  /* ---------- hotkeys: the letters in brackets really work ---------- */
  var typing = function (el) {
    return el && (el.isContentEditable || /^(INPUT|TEXTAREA|SELECT)$/.test(el.tagName));
  };
  var go = function (href) {
    if (href.charAt(0) === "#" && onDocs) { location.hash = href; return; }
    location.href = href;
  };
  var defaults = {
    b: BUY,
    d: onDocs ? "#hub" : "docs.html",
    g: "https://github.com/revjmoney/axis-bbs",
    t: onDocs ? "index.html#test-board" : "#test-board",
    l: onDocs ? "index.html#features" : "#features"
  };
  if (onDocs) defaults.r = "#rumor-list";
  document.addEventListener("keydown", function (e) {
    if (e.defaultPrevented || e.ctrlKey || e.metaKey || e.altKey || typing(e.target)) return;
    if (document.querySelector(".lightbox:not([hidden])")) return;
    var inBar = bar.indexOf(document.activeElement) >= 0;
    if (bar.length && (inBar || barVisible())) {
      var step = { ArrowLeft: -1, ArrowRight: 1, ArrowUp: inBar ? -1 : 0, ArrowDown: inBar ? 1 : 0 }[e.key];
      if (step) { e.preventDefault(); light(lit + step, true); return; }
      if (e.key === "Enter" && !inBar && (document.activeElement === document.body || !document.activeElement)) { e.preventDefault(); bar[lit].click(); return; }
    }
    if (e.key === "/") {
      var box = Array.prototype.filter.call(document.querySelectorAll(".doc-search"), function (i) { return i.offsetParent !== null; })[0];
      if (box) { e.preventDefault(); box.focus(); } else if (!onDocs) { e.preventDefault(); location.href = "docs.html"; }
      return;
    }
    if (e.key.length !== 1) return;
    var k = e.key.toLowerCase();
    var el = Array.prototype.filter.call(document.querySelectorAll('[data-key="' + (k === '"' ? "" : k) + '"]'), function (a) { return a.offsetParent !== null; })[0];
    if (el) { e.preventDefault(); el.click(); return; }
    if (defaults[k]) { e.preventDefault(); go(defaults[k]); }
  });

  /* ---------- rumors: a random one on the landing page, the whole wall in the docs ---------- */
  var FALLBACK = [
    "the ring board dreams in green",
    "measure twice, cut once, scrap it anyway, blame the endmill",
    "chips or it didn't happen",
    "your tool isn't dull, it's experienced",
    "the e-stop is the most used button on the machine"
  ];
  var rumors = FALLBACK.slice();
  var rumorText = document.querySelector(".rumor-text");
  var rumorBtn = document.querySelector(".rumor-next");
  var wall = document.querySelector(".rumor-wall");
  var lastRumor = -1;
  var showRumor = function () {
    if (!rumorText || !rumors.length) return;
    var n = Math.floor(Math.random() * rumors.length);
    if (rumors.length > 1 && n === lastRumor) n = (n + 1) % rumors.length;
    lastRumor = n;
    rumorText.textContent = "";
    var tag = document.createElement("span");
    tag.className = "c-grey";
    tag.textContent = "rumor: ";
    rumorText.appendChild(tag);
    rumorText.appendChild(document.createTextNode(rumors[n]));
  };
  if (rumorBtn) rumorBtn.addEventListener("click", showRumor);
  if ((rumorText || wall) && window.fetch) {
    fetch("rumors.txt").then(function (r) { return r.ok ? r.text() : ""; }).then(function (t) {
      var lines = t.split(/\r?\n/).map(function (l) { return l.trim().replace(/^rumor:\s*/i, ""); }).filter(function (l) { return l && l.length <= 90; });
      if (!lines.length) return;
      rumors = lines;
      showRumor();
      if (wall) {
        wall.textContent = "";
        lines.forEach(function (l) { var li = document.createElement("li"); li.textContent = l; wall.appendChild(li); });
        var count = document.querySelector(".rumor-count");
        if (count) count.textContent = lines.length + " rumors on file.";
      }
    }, function () { showRumor(); });
  } else {
    showRumor();
  }

  /* ---------- feature flashcards on the home page: tap to flip open ---------- */
  document.querySelectorAll(".fcard-more").forEach(function (b) {
    b.addEventListener("click", function () {
      var card = b.closest(".fcard");
      var open = card.classList.toggle("open");
      b.setAttribute("aria-expanded", open ? "true" : "false");
      b.textContent = open ? "Less" : "More";
    });
  });

  /* ---------- copy buttons on code blocks ---------- */
  document.querySelectorAll("pre > code").forEach(function (code) {
    var b = document.createElement("button");
    b.type = "button";
    b.className = "copy-btn";
    b.textContent = "Copy";
    b.setAttribute("aria-label", "Copy this code");
    b.addEventListener("click", function () {
      var done = function () {
        b.textContent = "Copied";
        b.classList.add("done");
        setTimeout(function () { b.textContent = "Copy"; b.classList.remove("done"); }, 1600);
      };
      var text = code.innerText.replace(/\n$/, "");
      if (navigator.clipboard && navigator.clipboard.writeText) navigator.clipboard.writeText(text).then(done, function () {});
      else {
        var t = document.createElement("textarea");
        t.value = text; document.body.appendChild(t); t.select();
        try { document.execCommand("copy"); done(); } catch (e) {}
        document.body.removeChild(t);
      }
    });
    code.parentNode.appendChild(b);
  });

  /* ---------- lightbox for screenshots: links with class "lb" ---------- */
  var box = null, boxImg, boxCap, lastFocus = null;
  var close = function () {
    if (!box || box.hidden) return;
    box.hidden = true;
    boxImg.removeAttribute("src");
    document.body.style.overflow = "";
    if (lastFocus) lastFocus.focus();
  };
  var open = function (href, caption, from) {
    if (!box) {
      box = document.createElement("div");
      box.className = "lightbox";
      box.setAttribute("role", "dialog");
      box.setAttribute("aria-modal", "true");
      box.setAttribute("aria-label", "Screenshot");
      box.innerHTML = '<button type="button" class="lightbox-close">Close (Esc)</button><figure><img alt=""><figcaption></figcaption></figure>';
      boxImg = box.querySelector("img");
      boxCap = box.querySelector("figcaption");
      box.addEventListener("click", function (e) { if (e.target !== boxImg) close(); });
      document.addEventListener("keydown", function (e) { if (e.key === "Escape") close(); });
      document.body.appendChild(box);
    }
    lastFocus = from;
    boxImg.src = href;
    boxImg.alt = caption;
    boxCap.textContent = caption;
    box.hidden = false;
    document.body.style.overflow = "hidden";
    box.querySelector(".lightbox-close").focus();
  };
  document.addEventListener("click", function (e) {
    var a = e.target.closest("a.lb");
    if (!a || e.metaKey || e.ctrlKey || e.shiftKey) return;
    e.preventDefault();
    var img = a.querySelector("img");
    open(a.getAttribute("href"), img ? img.getAttribute("alt") : "", a);
  });
})();
