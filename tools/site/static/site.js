// Every page: the theme toggle, search, the docs menu on small screens, copy
// buttons on code blocks.
"use strict";

(() => {
  const root = document.body.dataset.root || "./";
  const html = document.documentElement;

  // --- Theme: follows the system unless the reader picked one --------------------

  const darkQuery = matchMedia("(prefers-color-scheme: dark)");
  const currentTheme = () => html.getAttribute("data-theme") || (darkQuery.matches ? "dark" : "light");
  const themeButton = document.querySelector(".theme-toggle");
  function labelThemeButton() {
    const next = currentTheme() === "dark" ? "light" : "dark";
    themeButton.setAttribute("aria-label", `Switch to the ${next} theme`);
    themeButton.title = `Switch to the ${next} theme`;
  }
  themeButton.addEventListener("click", () => {
    const next = currentTheme() === "dark" ? "light" : "dark";
    html.setAttribute("data-theme", next);
    try { localStorage.setItem("serval-theme", next); } catch (e) { /* private mode: this page only */ }
    labelThemeButton();
    document.dispatchEvent(new CustomEvent("serval-theme", { detail: next }));
  });
  darkQuery.addEventListener("change", () => {
    labelThemeButton();
    document.dispatchEvent(new CustomEvent("serval-theme", { detail: currentTheme() }));
  });
  labelThemeButton();

  // --- Search: Pagefind's UI, loaded the first time it opens -----------------------

  const searchDialog = document.querySelector(".search-dialog");
  let searchLoaded = null;
  function loadSearch() {
    if (searchLoaded) return searchLoaded;
    const bundle = new URL(root + "pagefind/", location.href);
    searchLoaded = new Promise((resolve, reject) => {
      const css = document.createElement("link");
      css.rel = "stylesheet";
      css.href = bundle.href + "pagefind-ui.css";
      document.head.append(css);
      const script = document.createElement("script");
      script.src = bundle.href + "pagefind-ui.js";
      script.onload = resolve;
      script.onerror = reject;
      document.head.append(script);
    }).then(() => {
      // Results link from the site's root, wherever it is served.
      new window.PagefindUI({
        element: "#search",
        bundlePath: bundle.pathname,
        baseUrl: new URL(root, location.href).pathname,
        showImages: false,
        showSubResults: true,
        resetStyles: false,
        translations: { placeholder: "Search the docs" },
      });
    }).catch(() => {
      searchDialog.querySelector(".search-missing").hidden = false;
    });
    return searchLoaded;
  }
  function openSearch() {
    if (!searchDialog.open) searchDialog.showModal();
    loadSearch().then(() => {
      const input = searchDialog.querySelector("input");
      if (input) { input.focus(); input.select(); }
    });
  }
  document.querySelector(".search-open").addEventListener("click", openSearch);
  searchDialog.querySelector(".dialog-close").addEventListener("click", () => searchDialog.close());
  searchDialog.addEventListener("click", e => { if (e.target === searchDialog) searchDialog.close(); });
  addEventListener("keydown", e => {
    if (e.key !== "/" || e.ctrlKey || e.metaKey || e.altKey) return;
    const t = e.target;
    if (t.isContentEditable || /^(INPUT|TEXTAREA|SELECT)$/.test(t.tagName)) return;
    if (document.querySelector("dialog[open]:not(.search-dialog)")) return;
    e.preventDefault();
    openSearch();
  });

  // --- Docs menu on small screens ------------------------------------------------

  const navToggle = document.querySelector(".docs-nav-toggle");
  if (navToggle) {
    navToggle.addEventListener("click", () => {
      const open = navToggle.getAttribute("aria-expanded") !== "true";
      navToggle.setAttribute("aria-expanded", String(open));
      document.querySelector(".docs-sidebar").classList.toggle("open", open);
    });
  }

  // --- Copy buttons on code blocks ------------------------------------------------

  window.servalCopyButtons = scope => {
    for (const pre of scope.querySelectorAll("pre.code")) {
      if (pre.parentElement.classList.contains("code-wrap")) continue;
      const wrap = document.createElement("div");
      wrap.className = "code-wrap";
      pre.replaceWith(wrap);
      wrap.append(pre);
      const button = document.createElement("button");
      button.type = "button";
      button.className = "copy";
      button.textContent = "Copy";
      button.addEventListener("click", async () => {
        try {
          await navigator.clipboard.writeText(pre.innerText.replace(/\n$/, ""));
          button.textContent = "Copied";
        } catch (e) {
          button.textContent = "Select and copy";
        }
        setTimeout(() => { button.textContent = "Copy"; }, 1600);
      });
      wrap.append(button);
    }
  };
  servalCopyButtons(document);
})();
