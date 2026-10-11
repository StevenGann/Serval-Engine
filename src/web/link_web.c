// The debug link's agent, the web page's end of it (docs/debug-link.md#
// finding-the-endpoint): the page's script (src/web/link.js, which
// cmake/ServalWeb.cmake adds to pages built with the agent) opens its
// WebSocket when the URL fragment holds serval-link=TOKEN and calls
// serval_link_attach(); the agent rings through serval_link_ring(), parks in
// an asynchronous wait (Asyncify unwinds the game to the browser, which goes
// on running the page and its WebSocket), and hands debug_log() and the
// engine's warnings to the page as LOG. In debug builds with the agent only:
// a release page has none of it, and a page without the fragment never
// attaches, so its agent stays dormant.

#include "../core/link_internal.h"
#include "web.h"

#include <emscripten.h>

#ifdef SERVAL_LINK_AGENT

// The page's end (src/web/link.js); a page opened without the fragment has
// none of these functions, and never attaches. (JavaScript: not formatted as C.)
// clang-format off
EM_JS(void, link_web_ring, (u32 mailbox), {
    if (Module.servalLinkRing)
        Module.servalLinkRing(mailbox);
});
EM_ASYNC_JS(void, link_web_wait, (void), {
    if (Module.servalLinkWait)
        await Module.servalLinkWait();
});
EM_JS(void, link_web_log, (u32 level, u32 source, const char* text), {
    if (Module.servalLinkLog)
        Module.servalLinkLog(level, source, text);
});
// The page's title and game code (serval_add_rom()'s TITLE and GAME_CODE, in
// its <script id="serval-rom">), as ASCII.
EM_JS(void, link_web_rom, (char* title, char* game_code), {
    let rom = {};
    try {
        rom = JSON.parse(document.getElementById("serval-rom").textContent);
    } catch (e) {
    }
    const put = (text, at, most) => {
        text = String(text || "").slice(0, most);
        for (let i = 0; i < text.length; i++)
            HEAPU8[at + i] = text.charCodeAt(i) & 0x7F;
        HEAPU8[at + text.length] = 0;
    };
    put(rom.title, title, 12);
    put(rom.gameCode, game_code, 4);
});
// clang-format on

static bool asked;   // the page called serval_link_attach()
static bool started; // serval_init() has asked whether it did

// Called by the page's script (link.js) when the fragment asks for the link,
// before the game starts; or later, which starts the agent at once. Returns
// the engine's version, which the page's HELLO carries.
EMSCRIPTEN_KEEPALIVE const char* serval_link_attach(void);
const char* serval_link_attach(void) {
    if (!asked) {
        asked = true;
        if (started)
            serval_link_init();
    }
    return SERVAL_LINK_ENGINE_VERSION;
}

bool serval_link_platform_present(void) {
    started = true;
    return asked;
}

void serval_link_platform_ring(ServalLinkMailbox* mailbox) {
    link_web_ring((u32)(uintptr_t)mailbox);
}

void serval_link_platform_wait(void) {
    link_web_wait();
}

// Nothing to mute: a parked page shows no frames, so it makes no sound.
void serval_link_platform_park(bool parked) {
    (void)parked;
}

void serval_link_platform_rom(char title[13], char game_code[5]) {
    link_web_rom(title, game_code);
}

void serval_web_link_log(u32 level, u32 source, const char* text) {
    if (asked)
        link_web_log(level, source, text);
}

#endif // SERVAL_LINK_AGENT
