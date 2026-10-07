// Draws the docs' ```mermaid blocks (<pre class="mermaid">) with Mermaid, a
// pinned version from jsDelivr, in the site's colors; redrawn when the theme
// changes. tools/site/build.py fills in the version.
import mermaid from "https://cdn.jsdelivr.net/npm/mermaid@@MERMAID_VERSION@/dist/mermaid.esm.min.mjs";

const blocks = [...document.querySelectorAll("pre.mermaid")].map(el => ({ el, source: el.textContent }));

function dark() {
  const theme = document.documentElement.getAttribute("data-theme");
  return theme ? theme === "dark" : matchMedia("(prefers-color-scheme: dark)").matches;
}

function themeVariables(isDark) {
  const font = getComputedStyle(document.body).fontFamily;
  return isDark ? {
    darkMode: true,
    background: "#1d120b",
    primaryColor: "#2c1b10",
    primaryTextColor: "#fff7d6",
    primaryBorderColor: "#f7b531",
    lineColor: "#e79c39",
    textColor: "#fff7d6",
    fontFamily: font,
    fontSize: "15px",
  } : {
    background: "#fffcf2",
    primaryColor: "#fff7d6",
    primaryTextColor: "#2b170c",
    primaryBorderColor: "#5a2918",
    lineColor: "#5a2918",
    textColor: "#2b170c",
    fontFamily: font,
    fontSize: "15px",
  };
}

async function draw() {
  const isDark = dark();
  // Natural size: a wide diagram scrolls sideways instead of shrinking its text.
  mermaid.initialize({
    startOnLoad: false,
    securityLevel: "strict",
    theme: "base",
    themeVariables: themeVariables(isDark),
    flowchart: { useMaxWidth: false },
  });
  for (const { el, source } of blocks) {
    el.removeAttribute("data-processed");
    el.textContent = source;
  }
  try {
    await mermaid.run({ nodes: blocks.map(b => b.el) });
  } catch (e) {
    console.warn("diagram:", e);
  }
}

draw();
document.addEventListener("serval-theme", draw);
