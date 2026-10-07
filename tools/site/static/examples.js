// The examples gallery (filters, and a dialog that plays an example: opened
// by a card or by a link to examples/#<name>) and each example's own page
// (the game sized to fit, the source tabs).
"use strict";

(() => {
  const W = 240, H = 160;
  // Under the game, the web page (src/web/shell.html) keeps room for its key
  // help line; on touch screens it shows on-screen buttons instead.
  const TOUCH = matchMedia("(pointer: coarse)").matches;
  const RESERVE = TOUCH ? 0 : 40;

  // The game at the largest whole multiple of 240x160 that fits (the page
  // inside scales itself the same way), or smaller if even 1x doesn't fit.
  // On a touch screen the page needs room around the game for its buttons,
  // so it gets the whole stage, as when it is opened on its own.
  function fitFrame(article, maxHeight) {
    const stage = article.querySelector(".stage");
    const frame = stage.querySelector(".game-frame");
    const width = stage.clientWidth;
    if (TOUCH) {
      frame.style.width = width + "px";
      frame.style.height = Math.max(Math.min(maxHeight + 160, innerHeight - 80), 320) + "px";
      return;
    }
    const fit = Math.min(width / W, (maxHeight - RESERVE) / H);
    const scale = fit >= 1 ? Math.floor(fit) : Math.max(fit, 0.5);
    frame.style.width = Math.round(W * scale) + "px";
    frame.style.height = Math.round(H * scale + RESERVE) + "px";
  }

  // The site shows the controls itself, so the page's own help line goes; Esc
  // inside the game closes the dialog.
  function wireFrame(frame, onEscape) {
    frame.addEventListener("load", () => {
      try {
        const doc = frame.contentDocument;
        const help = doc && doc.getElementById("help");
        if (help) help.style.visibility = "hidden";
        if (onEscape) frame.contentWindow.addEventListener("keydown", e => { if (e.key === "Escape") onEscape(); });
      } catch (e) { /* another origin: leave the page as it is */ }
    });
  }

  function initTabs(article) {
    const tabs = [...article.querySelectorAll('[role="tab"]')];
    function select(tab, focus) {
      for (const t of tabs) {
        const on = t === tab;
        t.setAttribute("aria-selected", String(on));
        t.tabIndex = on ? 0 : -1;
        article.querySelector("#" + CSS.escape(t.getAttribute("aria-controls"))).hidden = !on;
      }
      if (focus) tab.focus();
    }
    tabs.forEach((tab, i) => {
      tab.addEventListener("click", () => select(tab, false));
      tab.addEventListener("keydown", e => {
        const step = { ArrowRight: 1, ArrowLeft: -1 }[e.key];
        if (step) { e.preventDefault(); select(tabs[(i + step + tabs.length) % tabs.length], true); }
        if (e.key === "Home") { e.preventDefault(); select(tabs[0], true); }
        if (e.key === "End") { e.preventDefault(); select(tabs[tabs.length - 1], true); }
      });
    });
    if (window.servalCopyButtons) servalCopyButtons(article);
  }

  // --- An example's own page -------------------------------------------------------

  const ownPage = document.querySelector(".example-page .example");
  if (ownPage) {
    initTabs(ownPage);
    wireFrame(ownPage.querySelector(".game-frame"), null);
    const fit = () => fitFrame(ownPage, innerHeight - 140);
    addEventListener("resize", fit);
    fit();
    return;
  }

  // --- The gallery -----------------------------------------------------------------

  const gallery = document.querySelector(".gallery");
  if (!gallery) return;
  const cards = [...gallery.querySelectorAll(".card")];
  const tagButtons = [...gallery.querySelectorAll(".tag-filter .tag")];
  const input = gallery.querySelector("#example-filter");
  const count = gallery.querySelector(".gallery-count .count");
  const empty = gallery.querySelector(".gallery-empty");
  let tag = "";

  function applyFilters() {
    const words = input.value.toLowerCase().split(/\s+/).filter(Boolean);
    let shown = 0;
    for (const card of cards) {
      const tags = card.dataset.tags.split(" ");
      const visible = (!tag || tags.includes(tag)) && words.every(w => card.dataset.search.includes(w));
      card.hidden = !visible;
      if (visible) shown++;
    }
    for (const b of tagButtons) b.setAttribute("aria-pressed", String(b.dataset.tag === tag));
    for (const b of gallery.querySelectorAll(".card .tag")) b.classList.toggle("active", !!tag && b.dataset.tag === tag);
    count.textContent = shown === cards.length ? `Showing ${shown} examples`
      : `Showing ${shown} of ${cards.length} examples`;
    empty.hidden = shown > 0;
  }

  function setTag(value) {
    tag = tagButtons.some(b => b.dataset.tag === value) ? value : "";
    const url = new URL(location.href);
    if (tag) url.searchParams.set("tag", tag); else url.searchParams.delete("tag");
    history.replaceState(history.state, "", url);
    applyFilters();
  }

  gallery.addEventListener("click", e => {
    const button = e.target.closest("button.tag");
    if (button) setTag(button.dataset.tag === tag && button.closest(".card") ? "" : button.dataset.tag);
  });
  input.addEventListener("input", applyFilters);
  empty.querySelector(".clear-filters").addEventListener("click", () => { input.value = ""; setTag(""); input.focus(); });
  tag = new URLSearchParams(location.search).get("tag") || "";
  if (!tagButtons.some(b => b.dataset.tag === tag)) tag = "";
  applyFilters();

  // --- The dialog: the example's page, fetched, with the game running ---------------

  const dialog = document.querySelector(".example-dialog");
  const dialogBody = dialog.querySelector(".dialog-body");
  const dialogTitle = dialog.querySelector(".dialog-title");
  const names = new Set(cards.map(c => c.dataset.hash));
  let openedHere = false; // the dialog's history entry is ours to go back from
  let current = null;
  let returnFocus = null;
  let loading = 0;

  async function show(name) {
    if (current === name && dialog.open) return;
    current = name;
    const ticket = ++loading;
    const link = document.querySelector(`.card-link[data-example="${CSS.escape(name)}"]`);
    const card = link.closest(".card");
    returnFocus = returnFocus || link;
    dialogTitle.textContent = card.querySelector(".rom-title").textContent;
    dialogBody.replaceChildren();
    dialogBody.setAttribute("aria-busy", "true");
    if (!dialog.open) dialog.showModal();
    const url = new URL(link.getAttribute("href"), location.href);
    let article;
    try {
      const response = await fetch(url);
      if (!response.ok) throw new Error(response.status);
      const doc = new DOMParser().parseFromString(await response.text(), "text/html");
      article = doc.querySelector(".example");
    } catch (e) {
      location.href = url.href; // can't fetch (opened from disk?): go to the example's page
      return;
    }
    if (ticket !== loading || !dialog.open) return;
    // Links in the fetched page are relative to it.
    for (const el of article.querySelectorAll("[href], [src]")) {
      const attr = el.hasAttribute("src") ? "src" : "href";
      const value = el.getAttribute(attr);
      if (!value.startsWith("#")) el.setAttribute(attr, new URL(value, url).href);
    }
    for (const a of article.querySelectorAll("a.tag")) {
      a.addEventListener("click", e => {
        e.preventDefault();
        const value = new URL(a.href).searchParams.get("tag");
        close();
        setTag(value);
      });
    }
    const frame = article.querySelector(".game-frame");
    const src = frame.getAttribute("src");
    frame.removeAttribute("src");
    dialogBody.replaceChildren(document.adoptNode(article));
    dialogBody.removeAttribute("aria-busy");
    dialogBody.scrollTop = 0;
    initTabs(article);
    fitFrame(article, innerHeight - 300);
    wireFrame(frame, close);
    frame.addEventListener("load", () => frame.focus(), { once: true });
    frame.src = src;
  }

  function hide() {
    loading++;
    current = null;
    const frame = dialog.querySelector(".game-frame");
    if (frame) frame.src = "about:blank"; // stops the game and its sound
    dialogBody.replaceChildren();
    if (dialog.open) dialog.close();
    if (returnFocus) { returnFocus.focus(); returnFocus = null; }
  }

  // The hash names the open example: examples/#fireflies opens it.
  function route() {
    const name = decodeURIComponent(location.hash.slice(1));
    if (names.has(name)) show(name);
    else if (dialog.open) hide();
  }

  function close() {
    if (!dialog.open) return;
    if (openedHere) {
      openedHere = false;
      history.back(); // removes the hash; route() closes the dialog
    } else {
      history.replaceState(history.state, "", location.pathname + location.search);
      hide();
    }
  }

  for (const link of gallery.querySelectorAll(".card-link")) {
    link.addEventListener("click", e => {
      if (e.button !== 0 || e.metaKey || e.ctrlKey || e.shiftKey || e.altKey) return;
      e.preventDefault();
      returnFocus = link;
      if (location.hash.slice(1) === link.dataset.example) { route(); return; }
      openedHere = true;
      location.hash = link.dataset.example;
    });
  }
  dialog.querySelector(".dialog-close").addEventListener("click", close);
  dialog.addEventListener("cancel", e => { e.preventDefault(); close(); });
  dialog.addEventListener("click", e => { if (e.target === dialog) close(); });
  addEventListener("hashchange", route);
  addEventListener("resize", () => {
    const article = dialog.open && dialogBody.querySelector(".example");
    if (article) fitFrame(article, innerHeight - 300);
  });
  route();
})();
