#!/usr/bin/env python3
"""Builds Serval Engine's documentation site: the docs as pages, and a front
page.

Usage: tools/site/build.py [--out DIR] [--base-url URL]

  --out        where to write the site (default build/site; emptied first)
  --base-url   where the site is published (default the GitHub Pages URL);
               used only for canonical and social-preview links, since every
               link inside the site is relative

Every docs/*.md becomes a page (docs/vm.md -> docs/vm/index.html; docs/README.md
-> docs/index.html). Links between docs become links between pages; links to
other files in the repository become GitHub links. After writing the site it
checks every local link and anchor and fails listing the broken ones.

Search is a separate step: `python -m pagefind --site DIR` indexes the docs
pages. Needs the packages in tools/site/requirements.txt.
"""

import argparse
import html
import posixpath
import re
import shutil
import string
import sys
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import unquote

from markdown_it import MarkdownIt
from mdit_py_plugins.anchors import anchors_plugin
from mdit_py_plugins.gfm import gfm_plugin
from pygments import highlight
from pygments.formatters import HtmlFormatter
from pygments.lexers import BashLexer, CLexer, CMakeLexer, JsonLexer

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
REPO = "StevenGann/Serval-Engine"
REPO_URL = f"https://github.com/{REPO}"
BRANCH = "main"
DEFAULT_BASE_URL = "https://stevengann.github.io/Serval-Engine/"
MERMAID_VERSION = "12.1.0"  # pinned; tools/site/static/diagrams.js loads it from jsDelivr

# The docs sidebar, after docs/README.md's two audiences: what a game
# developer reads (the guide, the reference, then the design documents), and
# what someone working on the engine itself reads. Every docs/*.md must be
# listed (the build fails otherwise); docs/README.md is the index above them.
NAV = [
    ("Writing a game", [
        ("getting-started", "Getting started"),
        ("api-reference", "API reference"),
    ]),
    ("Design", [
        ("overview", "Overview"),
        ("core-api", "Core API"),
        ("ecs", "ECS"),
        ("sprites", "Sprites"),
        ("tilemaps", "Tilemaps"),
        ("audio", "Audio"),
        ("frame-loop", "Frame loop"),
        ("runtime-systems", "Other runtime systems"),
        ("vm", "Bytecode VM"),
        ("debug-link", "Debug link"),
        ("platforms", "Platforms"),
    ]),
    ("Working on the engine", [
        ("development", "Development"),
        ("examples-roadmap", "Examples roadmap"),
        ("releases", "Releases"),
        ("licensing", "Licensing"),
        ("open-questions", "Open questions"),
    ]),
]
INDEX_LABEL = "All documents"

errors = []


def fail(message):
    errors.append(message)


# --- Paths and URLs --------------------------------------------------------------


def rel(from_page, target):
    """A relative URL from the page at site path from_page (e.g.
    "docs/vm/index.html") to target, a site path: "docs/vm/" for a directory,
    "assets/site.css" for a file, "" for the site's root; "#fragment" allowed."""
    path, hash_, fragment = target.partition("#")
    base = posixpath.dirname(from_page) or "."
    if path == "" or path.endswith("/"):
        url = posixpath.relpath(path.rstrip("/") or ".", base)
        url = "./" if url == "." else url + "/"
    else:
        url = posixpath.relpath(path, base)
    return url + hash_ + fragment


def github_url(repo_path, fragment=""):
    kind = "tree" if (ROOT / repo_path).is_dir() else "blob"
    url = f"{REPO_URL}/{kind}/{BRANCH}/{repo_path}".rstrip("/")
    return url + ("#" + fragment if fragment else "")


def github_slug(title):
    """The id GitHub gives a heading, so the docs' own #anchors keep working:
    lowercase, punctuation and symbols dropped (not - or _), spaces to -."""
    return re.sub(r"[^\w\- ]", "", title.strip().lower()).replace(" ", "-")


def esc(text):
    return html.escape(text, quote=True)


# --- Highlighting ----------------------------------------------------------------


LEXERS = {"c": CLexer, "h": CLexer, "sh": BashLexer, "cmake": CMakeLexer, "json": JsonLexer}
FORMATTER = HtmlFormatter(nowrap=True)


def highlight_code(code, lang):
    lexer = LEXERS.get(lang)
    if lexer is None:
        return esc(code)
    return highlight(code, lexer(), FORMATTER)


def code_block(code, lang):
    label = f' data-lang="{esc(lang)}"' if lang and lang != "text" else ""
    return f'<pre class="code"{label}><code>{highlight_code(code, lang)}</code></pre>\n'


# --- Markdown --------------------------------------------------------------------


class Docs:
    """The docs/*.md files: repository path -> site directory."""

    def __init__(self):
        self.files = sorted(p.name for p in (ROOT / "docs").glob("*.md"))
        self.page_dir = {f"docs/{name}": ("docs/" if name == "README.md" else f"docs/{name[:-3]}/") for name in self.files}


def rewrite_link(url, env, is_image=False):
    """Docs link to another doc -> its page; to docs/images -> the copied image;
    to anything else in the repository -> GitHub. External links unchanged."""
    if re.match(r"^[a-zA-Z][a-zA-Z0-9+.-]*:", url) or url.startswith("//") or url.startswith("#"):
        return url
    path, _, fragment = url.partition("#")
    repo_path = posixpath.normpath(posixpath.join(posixpath.dirname(env["source"]), unquote(path)))
    if repo_path.startswith("../") or repo_path == "..":
        fail(f"{env['source']}: link {url} points outside the repository")
        return url
    page = env["page"]
    docs = env["docs"]
    if repo_path in docs.page_dir:
        return rel(page, docs.page_dir[repo_path] + ("#" + fragment if fragment else ""))
    if repo_path.startswith("docs/images/") and (ROOT / repo_path).is_file():
        return rel(page, repo_path)
    if not (ROOT / repo_path).exists():
        fail(f"{env['source']}: link {url} points to {repo_path}, which doesn't exist")
        return url
    if is_image:
        fail(f"{env['source']}: image {url} is outside docs/images, so the site can't show it")
    return github_url(repo_path, fragment)


def links_rule(state):
    for token in state.tokens:
        for child in token.children or []:
            if child.type == "link_open":
                child.attrSet("href", rewrite_link(child.attrGet("href"), state.env))
            elif child.type == "image":
                child.attrSet("src", rewrite_link(child.attrGet("src"), state.env, is_image=True))


def make_markdown():
    def fence(code, lang, attrs):
        if lang == "mermaid":
            return f'<pre class="mermaid">{esc(code)}</pre>\n'
        return code_block(code, lang)

    md = MarkdownIt("commonmark", {"html": True, "highlight": fence})
    md.use(gfm_plugin)
    # Strikethrough is ~~text~~ only: the docs use a single ~ for "about"
    # (~1,000 cycles), which the single-tilde form would strike through.
    md.options["strikethrough_single_tilde"] = False
    md.use(anchors_plugin, min_level=1, max_level=6, slug_func=github_slug)
    md.core.ruler.push("serval_links", links_rule)

    def heading_close(self, tokens, idx, options, env):
        opening = tokens[idx - 2]
        anchor = ""
        if opening.tag in ("h2", "h3", "h4") and opening.attrGet("id"):
            anchor = f'<a class="anchor" href="#{opening.attrGet("id")}" aria-label="Link to this section"></a>'
        return f"{anchor}</{tokens[idx].tag}>\n"

    md.add_render_rule("heading_close", heading_close)
    return md


def inline_text(token):
    return "".join(c.content for c in token.children or [] if c.type in ("text", "code_inline"))


def render_doc(md, docs, name, page):
    source = f"docs/{name}"
    text = (ROOT / source).read_text(encoding="utf-8")
    env = {"source": source, "page": page, "docs": docs}
    tokens = md.parse(text, env)
    title, toc = None, []
    for i, token in enumerate(tokens):
        if token.type != "heading_open":
            continue
        heading = inline_text(tokens[i + 1])
        if token.tag == "h1" and title is None:
            title = heading
        elif token.tag in ("h2", "h3"):
            toc.append((int(token.tag[1]), token.attrGet("id"), tokens[i + 1].children))
    body = md.renderer.render(tokens, md.options, env)
    body = re.sub(r"<p><strong>Status:</strong>", '<p class="status"><strong>Status:</strong>', body)
    body = body.replace("<table>", '<div class="table-wrap"><table>').replace("</table>", "</table></div>")
    toc_html = [(level, slug, md.renderer.renderInline(children, md.options, env)) for level, slug, children in toc]
    return title or name, body, toc_html, text


# --- Page layout -----------------------------------------------------------------

ICON_SEARCH = ('<svg viewBox="0 0 16 16" width="16" height="16" aria-hidden="true" focusable="false">'
               '<path d="M6.5 1a5.5 5.5 0 0 1 4.38 8.82l3.65 3.65-1.06 1.06-3.65-3.65A5.5 5.5 0 1 1 6.5 1Zm0 '
               '1.5a4 4 0 1 0 0 8 4 4 0 0 0 0-8Z" fill="currentColor"/></svg>')
ICON_THEME = ('<svg viewBox="0 0 16 16" width="16" height="16" aria-hidden="true" focusable="false">'
              '<path class="icon-moon" d="M6.2 1.3a6.8 6.8 0 1 0 8.5 8.5A5.6 5.6 0 0 1 6.2 1.3Z" fill="currentColor"/>'
              '<g class="icon-sun" fill="currentColor"><circle cx="8" cy="8" r="3.2"/>'
              '<path d="M7.25 0h1.5v2.5h-1.5zM7.25 13.5h1.5V16h-1.5zM0 7.25h2.5v1.5H0zM13.5 7.25H16v1.5h-2.5zM2.1 3.17l1.06-1.06 '
              '1.77 1.77-1.06 1.06zM11.07 12.13l1.06-1.06 1.77 1.77-1.06 1.06zM2.1 12.83l1.77-1.77 1.06 1.06-1.77 1.77zM11.07 '
              '3.87l1.77-1.77 1.06 1.06-1.77 1.77z"/></g></svg>')


class Site:
    def __init__(self, out, base_url):
        self.out = out
        self.base_url = base_url.rstrip("/") + "/"
        self.layout = string.Template((HERE / "templates" / "layout.html").read_text(encoding="utf-8"))
        self.pages = []

    def write(self, page, title, body, *, description, section="", body_class="", head="", scripts=""):
        root = rel(page, "")
        canonical = self.base_url + (page[: -len("index.html")] if page.endswith("index.html") else page)
        current = {s: ' aria-current="page"' if s == section else "" for s in ("docs", "examples")}
        html_text = self.layout.substitute(
            root=root,
            title=esc(title),
            description=esc(description),
            canonical=esc(canonical),
            social_image=esc(self.base_url + "docs/images/serval-engine-social.png"),
            body_class=body_class,
            head=head,
            body=body,
            scripts=scripts,
            docs_current=current["docs"],
            examples_current=current["examples"],
            repo_url=REPO_URL,
            icon_search=ICON_SEARCH,
            icon_theme=ICON_THEME,
            license_url=github_url("LICENSE"),
            releases_url=f"{REPO_URL}/releases",
        )
        path = self.out / page
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(html_text, encoding="utf-8")
        self.pages.append(page)


# --- Docs pages ------------------------------------------------------------------


def build_docs(site):
    docs = Docs()
    md = make_markdown()
    listed = [slug for _, items in NAV for slug, _ in items]
    for name in docs.files:
        if name != "README.md" and name[:-3] not in listed:
            fail(f"docs/{name} is not in the sidebar: add it to NAV in tools/site/build.py")
    for slug in listed:
        if f"{slug}.md" not in docs.files:
            fail(f"NAV in tools/site/build.py lists {slug}, but docs/{slug}.md doesn't exist")
    order = [("README.md", INDEX_LABEL)] + [(f"{slug}.md", label) for _, items in NAV for slug, label in items]
    order = [(name, label) for name, label in order if name in docs.files]
    pages = {name: docs.page_dir[f"docs/{name}"] + "index.html" for name, _ in order}

    for position, (name, label) in enumerate(order):
        page = pages[name]
        title, body, toc, text = render_doc(md, docs, name, page)

        def nav_link(doc, text_):
            current = ' aria-current="page"' if doc == name else ""
            return f'<li><a href="{rel(page, docs.page_dir["docs/" + doc])}"{current}>{esc(text_)}</a></li>'

        groups = [f'<ul class="nav-top">{nav_link("README.md", INDEX_LABEL)}</ul>']
        for group, items in NAV:
            links = "".join(nav_link(f"{slug}.md", text_) for slug, text_ in items)
            groups.append(f'<p class="nav-group">{esc(group)}</p><ul>{links}</ul>')
        sidebar = "".join(groups)

        toc_items = "".join(f'<li class="toc-{level}"><a href="#{slug}">{label_html}</a></li>'
                            for level, slug, label_html in toc)
        toc_block = f'<ul>{toc_items}</ul>' if toc else ""

        pager = []
        if position > 0:
            prev_name, prev_label = order[position - 1]
            pager.append(f'<a class="pager-prev" href="{rel(page, docs.page_dir["docs/" + prev_name])}" rel="prev">'
                         f'<span>Previous</span>{esc(prev_label)}</a>')
        if position + 1 < len(order):
            next_name, next_label = order[position + 1]
            pager.append(f'<a class="pager-next" href="{rel(page, docs.page_dir["docs/" + next_name])}" rel="next">'
                         f'<span>Next</span>{esc(next_label)}</a>')

        description = first_paragraph(text)
        diagrams = '<pre class="mermaid">' in body
        main = f"""
<div class="docs-layout">
  <aside class="docs-sidebar">
    <button class="docs-nav-toggle" type="button" aria-expanded="false" aria-controls="docs-nav">
      <span>Documents</span><span class="docs-nav-current">{esc(label)}</span>
    </button>
    <nav id="docs-nav" class="docs-nav" aria-label="Documentation">{sidebar}</nav>
  </aside>
  <main id="main" class="docs-main">
    {f'<details class="toc-inline"><summary>On this page</summary>{toc_block}</details>' if toc else ''}
    <article class="prose" data-pagefind-body>
{body}
    </article>
    <p class="edit-link"><a href="{REPO_URL}/edit/{BRANCH}/docs/{name}">Edit this page on GitHub</a></p>
    <nav class="pager" aria-label="Previous and next">{''.join(pager)}</nav>
  </main>
  <aside class="docs-toc">
    {f'<nav aria-label="On this page"><p class="toc-title">On this page</p>{toc_block}</nav>' if toc else ''}
  </aside>
</div>"""
        scripts = ""
        if diagrams:
            scripts = f'<script type="module" src="{rel(page, "assets/diagrams.js")}"></script>'
        site.write(page, f"{title} | Serval Engine", main, description=description, section="docs",
                   body_class="page-docs", scripts=scripts)


def first_paragraph(markdown_text):
    """The doc's first plain paragraph, as text, for the page's description."""
    for block in re.split(r"\n\s*\n", markdown_text):
        block = block.strip()
        if not block or block[0] in "#|`-*>" or block.startswith("**Status"):
            continue
        plain = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", block)
        plain = re.sub(r"[`*]", "", plain)
        return " ".join(plain.split())[:300]
    return "Serval Engine documentation."


# --- Front page ------------------------------------------------------------------


def readme_parts():
    readme = (ROOT / "README.md").read_text(encoding="utf-8")
    paragraphs = [p.strip() for p in re.split(r"\n\s*\n", readme)]
    tagline = next(p for p in paragraphs if p.startswith("An open-source"))
    about = next(p for p in paragraphs if p.startswith("Serval Engine is the code"))
    status = re.search(r"\*\*Status:\*\*\s*(.*)", readme)
    return tagline, about, status.group(1).strip() if status else ""


def md_inline(md, text, page):
    env = {"source": "README.md", "page": page, "docs": Docs()}
    return md.renderInline(text, env)


def shape_of_a_game():
    """The frame loop from getting-started.md's "The shape of a game"."""
    text = (ROOT / "docs/getting-started.md").read_text(encoding="utf-8")
    match = re.search(r"## 3\. The shape of a game\s*```c\n(.*?)```", text, re.S)
    if not match:
        fail("docs/getting-started.md: the front page needs the code under '## 3. The shape of a game'")
        return ""
    return code_block(match.group(1), "c")


def build_home(site):
    page = "index.html"
    md = MarkdownIt("commonmark", {"html": True})
    md.core.ruler.push("serval_links", links_rule)
    tagline, about, status = readme_parts()
    logo = "docs/images/serval-engine-logo"
    highlights = [
        ("A flat C API",
         "Plain functions in the style of raylib for input, sprites, tilemaps and the camera, sound, text and save "
         "data. No hidden objects, no <code>malloc</code>, no C library in the ROM.",
         "docs/api-reference/", "API reference"),
        ("Entities and physics",
         "128 entities in a fixed-pool ECS, with systems for movement, bouncing physics, map collision, "
         "animation, paths and drawing in depth order.",
         "docs/ecs/", "The ECS"),
        ("A bytecode VM for game logic",
         "GameMaker-style objects and events run by a compact VM, with waits, spawning and engine calls. "
         "<em>Fireflies</em> is a whole game written for it.",
         "docs/vm/", "The VM"),
        ("Every game runs on the web",
         "Any game also builds into one self-contained web page, with sound, saves, keyboard, gamepad and "
         "touch, ready for any static host.",
         "docs/platforms/#web", "The web target"),
    ]
    cards = "".join(f"""
      <li><h3>{esc(h)}</h3><p>{text}</p><a href="{rel(page, target)}">{esc(link)}</a></li>"""
                    for h, text, target, link in highlights)
    body = f"""
<main id="main" class="home">
  <section class="hero">
    <div class="hero-text">
      <h1 class="hero-logo"><img src="{rel(page, logo + "@4x.png")}" srcset="{rel(page, logo + "@8x.png")} 2x"
        width="552" height="188" alt="Serval Engine"></h1>
      <p class="tagline">{md_inline(md, tagline, page)}</p>
      <p class="about">{md_inline(md, about, page)}</p>
      <p class="home-status"><strong>Status:</strong> {md_inline(md, status, page)}</p>
      <p class="actions">
        <a class="button primary" href="{rel(page, "docs/getting-started/")}">Get started</a>
        <a class="button" href="{rel(page, "docs/")}">Read the docs</a>
        <a class="button" href="{REPO_URL}">GitHub</a>
      </p>
    </div>
  </section>

  <section class="highlights" aria-labelledby="highlights-title">
    <h2 id="highlights-title">What's in the engine</h2>
    <ul>{cards}
    </ul>
  </section>

  <section class="loop" aria-labelledby="loop-title">
    <div class="loop-text">
      <h2 id="loop-title">A game is a loop</h2>
      <p>Everything runs at 60 frames per second, one loop iteration per frame. Sprites are immediate-mode: draw
        every sprite you want to see on every frame. Entities, sprites and sounds come from fixed pools and
        constant tables, built ahead of time.</p>
      <p><a href="{rel(page, "docs/getting-started/")}">Write your first game</a></p>
    </div>
    {shape_of_a_game()}
  </section>
</main>"""
    site.write(page, "Serval Engine: an open-source Game Boy Advance game runtime in C", body,
               description=re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", tagline), body_class="page-home")


# --- Static files ----------------------------------------------------------------


def copy_static(out):
    shutil.copytree(HERE / "static", out / "assets")
    js = (out / "assets/diagrams.js").read_text(encoding="utf-8")
    (out / "assets/diagrams.js").write_text(js.replace("@MERMAID_VERSION@", MERMAID_VERSION), encoding="utf-8")
    shutil.copytree(ROOT / "docs/images", out / "docs/images")


# --- Link check ------------------------------------------------------------------


class Links(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.ids = set()
        self.refs = []

    def handle_starttag(self, tag, attrs):
        for key, value in attrs:
            if value is None:
                continue
            if key == "id" or (tag == "a" and key == "name"):
                self.ids.add(value)
            elif key in ("href", "src") and not (tag == "link" and ("rel", "canonical") in attrs):
                self.refs.append(value)
            elif key == "srcset":
                self.refs += [part.split()[0] for part in value.split(",") if part.strip()]


def check_links(out):
    parsed = {}

    def parse(path):
        if path not in parsed:
            parser = Links()
            parser.feed(path.read_text(encoding="utf-8"))
            parsed[path] = parser
        return parsed[path]

    broken = []
    pages = sorted(out.rglob("*.html"))
    for path in pages:
        page = path.relative_to(out).as_posix()
        for ref in parse(path).refs:
            if re.match(r"^[a-zA-Z][a-zA-Z0-9+.-]*:", ref) or ref.startswith("//"):
                continue
            target, _, fragment = ref.partition("#")
            target = target.partition("?")[0]
            if target == "":
                file = path
            else:
                resolved = posixpath.normpath(posixpath.join(posixpath.dirname(page), unquote(target)))
                if resolved.startswith(".."):
                    broken.append(f"{page}: {ref} (outside the site)")
                    continue
                file = out / resolved
                if target.endswith("/") or file.is_dir():
                    file = file / "index.html"
            if not file.is_file():
                broken.append(f"{page}: {ref} (no such file)")
            elif fragment and file.suffix == ".html" and fragment not in parse(file).ids:
                broken.append(f"{page}: {ref} (no #{fragment} on that page)")
    return broken


# --- Main ------------------------------------------------------------------------


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--out", default=str(ROOT / "build/site"))
    parser.add_argument("--base-url", default=DEFAULT_BASE_URL)
    args = parser.parse_args()
    out = Path(args.out).resolve()

    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    site = Site(out, args.base_url)
    copy_static(out)
    build_docs(site)
    build_home(site)
    if errors:
        sys.exit("build.py: the site has problems:\n  " + "\n  ".join(errors))

    broken = check_links(out)
    if broken:
        sys.exit(f"build.py: {len(broken)} broken link(s):\n  " + "\n  ".join(broken))
    print(f"build.py: {len(site.pages)} pages in {out}; every local link and anchor resolves")


if __name__ == "__main__":
    main()
