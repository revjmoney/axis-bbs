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
      if (target && !topic && target !== hub) return; /* some other anchor (skip link): leave the view alone */
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
        window.scrollTo(0, 0);
        if (first) window.addEventListener("load", function () { setTimeout(function () { window.scrollTo(0, 0); }, 0); });
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
