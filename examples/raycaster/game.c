// The game's screens: the title, the choice of difficulty, the play screen
// (the view, drawn over two frames, with the HUD's sprites over it) and the
// results; and the fades and moves between them (game.h).
//
// All four are the pixel surface (surface.h) with sprites over it, turned on
// once at boot. Behind the title and the menu the view itself runs, dimmed,
// its camera circling the pillar hall while wisps drift around it; the
// results stand on the last picture of the run. The surface leaves no text
// layer, so what these screens say is said with sprites: the HUD's digits
// (art.c), the logo, and text composed into sprites from two fonts (title.c).

#include "game.h"

enum { SCREEN_TITLE, SCREEN_PICK, SCREEN_PLAY, SCREEN_RESULT };
static u8 screen;
static u8 difficulty = NORMAL;
// Frames since the screen came up (it stops counting long before it wraps),
// and since it began to go, if it goes by more than a fade: the logo flying
// off, the menu's band closing. 0: it stays.
static u16 clock;
static u8 going;

static const char* const difficulty_names[DIFFICULTY_COUNT] = {"EASY", "NORMAL", "HARD"};

// --- Fades ----------------------------------------------------------------------

#define FADE_STEP 2 // brightness per frame: 8 frames each way

// The screen's brightness, and where a fade is taking it: out to black (or
// to white, through the moon gate), then `next` sets up the new screen while
// nothing can be seen, and back in.
static int brightness = SCREEN_BRIGHTNESS_MIN;
static int fade_to; // 0: none, or fading in; else the brightness faded out to
static void (*next)(void);

static void leave_to(void (*enter)(void), int through) {
    if (fade_to)
        return;
    next = enter;
    fade_to = through;
}

// Steps the fade. True while fading out, and in the frame the screens
// change: the screen takes no input then.
static bool fade_step(void) {
    if (fade_to) {
        brightness += fade_to < 0 ? -FADE_STEP : FADE_STEP;
        if (brightness == fade_to) {
            fade_to = 0;
            next();
            screen_set_brightness(brightness);
            return true; // the new screen starts with the next frame
        }
    } else if (brightness) {
        brightness += brightness < 0 ? FADE_STEP : -FADE_STEP;
    }
    screen_set_brightness(brightness);
    return fade_to != 0;
}

// --- What every screen shares ------------------------------------------------------

// A picture takes two frames: its left part is drawn in the first and the
// rest in the second, which ends with the flip. So the view runs at 30
// pictures a second whatever the frame's 60, each part inside a frame's
// time, and the world steps once per picture (world_tick()). In play the
// first frame also steps the world and sets the view up, so it draws fewer
// columns: FIRST_COLS. Behind the title the split moves (tour_frame()).
#define FIRST_COLS 52
static u8 split = FIRST_COLS; // the columns the first frame draws
static u8 half;               // which part this frame draws: 0 or 1

static void picture_begin(void) {
    view_begin();
    view_draw(0, split);
    half = 1;
}

static void picture_end(void) {
    view_draw(split, VIEW_COLS);
    surface_flip();
    half = 0;
}

// A picture whole, behind a fade: one long frame nobody sees.
static void picture_whole(void) {
    view_begin();
    view_draw(0, VIEW_COLS);
    surface_flip();
    half = 0;
}

// The surface's colors now: art_palette()'s or view_colors()'s.
static const Color* colors;

static void set_colors(const Color* wanted) {
    if (colors == wanted)
        return;
    colors = wanted;
    surface_set_colors(0, wanted, 256);
}

// The HUD's group is loaded once and stays; the title's group or the menu's
// (title.c) comes and goes after it.
static u32 hud_mark;

static void show_group(const SpriteGroup* group) {
    sprite_groups_release(hud_mark);
    if (group)
        sprite_group_load(group);
}

// How much of the glass band and of what blends like it (SPRITE_BLEND) is
// its own color, in sixteenths; the rest is the view behind it.
#define GLASS 11

static void set_glass(u32 weight) {
    screen_set_blend(0, LAYER_PLAYFIELD | LAYER_BACKDROP, weight, 16 - weight);
}

// SELECT, on every screen: the frame's CPU use, top left.
static bool show_cpu;
static u16 cpu_now;    // the last two frames' CPU use, in thousandths: one
static u16 cpu_before; // picture's two halves

// A number in the HUD's digits, right-aligned in `digits` places from x.
static void draw_number(int value, int digits, int x, int y) {
    for (int i = digits - 1; i >= 0; i--) {
        sprite_draw(SPR_GLYPH, (u8)(GLYPH_0 + value % 10), x + i * 8, y, 0);
        value /= 10;
        if (!value)
            break;
    }
}

static void draw_cpu(void) {
    if (!show_cpu)
        return;
    // The busier half of the last picture, as a percentage of a frame.
    int percent = (cpu_now > cpu_before ? cpu_now : cpu_before) / 10;
    draw_number(percent, 3, 4, 4);
    sprite_draw(SPR_GLYPH, GLYPH_PERCENT, 29, 4, 0);
}

// PRESS START's colors, slowly from gold to white and back.
static void pulse(u32 sprite) {
    u32 t = frame_count() & 63;
    u32 amount = (t < 32 ? t : 63 - t) * 7;
    Color lit[3] = {
        color_mix(COLOR_RGB(232, 170, 48), COLOR_RGB(255, 255, 240), amount),
        COLOR_RGB(40, 16, 6),
        color_mix(COLOR_RGB(190, 120, 30), COLOR_RGB(255, 236, 150), amount),
    };
    sprite_set_colors((u16)sprite, PAL_PULSE * 16 + 1, lit, 3);
}

// --- The tour: the view behind the title and the menu ------------------------------------
//
// The warren with nobody playing: the camera goes round the middle of the
// pillar hall, turning as it goes, once in 22 seconds, while four wisps (the
// last a hex wisp) circle the hall the other way, outside the pillars, which
// hide them as they pass. The world ticks as in play, so that flames
// flicker, the gate turns and the wisps bob in their sleep, but with the
// serval put away inside the corner's wall, where no wisp sees it and
// nothing is in reach; the camera then takes its place for the picture.

#define HALL_X FIX(10.0) // the middle of the hall, between its four pillars
#define HALL_Y FIX(4.5)
#define DRIFTERS 4
static u8 drifters[DRIFTERS]; // which things[]

static Fix along(u16 angle, Fix radius, bool y) {
    return fix_mul(fix_from_fx(y ? fx_sin(angle) : fx_cos(angle)), radius);
}

static void tour_step(void) {
    static const Controls nobody;
    player.x = player.y = FIX_ONE / 2;
    world_tick(&nobody);
    u32 tick = run.ticks;
    u16 round = (u16)(tick * 99);
    player.x = HALL_X + along(round, FIX(0.6), false);
    player.y = HALL_Y + along(round, FIX(0.4), true);
    player.angle = (u16)(round + ANGLE_DEG(80));
    for (u32 i = 0; i < DRIFTERS; i++) {
        Thing* t = &things[drifters[i]];
        u16 at = (u16)(i * (0x10000 / DRIFTERS) - tick * 61);
        t->x = HALL_X + along(at, FIX(3.0), false);
        t->y = HALL_Y + along(at, FIX(2.35), true);
    }
}

static void tour_start(void) {
    world_start(NORMAL);
    // The drifters take free places among the warren's things; asleep, so
    // world_tick() only bobs them.
    u32 found = 0;
    for (u32 i = 0; i < MAX_THINGS && found < DRIFTERS; i++) {
        if (things[i].kind != THING_NONE)
            continue;
        things[i] = (Thing){.kind = found == DRIFTERS - 1 ? THING_HEX : THING_WISP,
                            .pic = PIC_WISP,
                            .state = WISP_ASLEEP,
                            .health = 1,
                            .glows = true};
        drifters[found++] = (u8)i;
    }
    tour_step();
}

// A frame of the tour: half a picture, unless the frame has other work
// (`busy`: a label composed); the picture then comes a frame later.
static void tour_frame(bool busy) {
    if (busy)
        return;
    if (half) {
        picture_end();
    } else {
        // The split follows what the last picture's two frames took
        // (cpu_before and cpu_now), two columns at a time: the picture's
        // dear part, a wisp or a pillar close by, wanders across it as the
        // camera turns, and a fixed split would leave one frame with most
        // of the work. (On the web they are the browser's times, and where
        // the split goes doesn't matter.)
        if (cpu_before > cpu_now + 20 && split > VIEW_COLS * 3 / 10)
            split -= 2;
        else if (cpu_now > cpu_before + 20 && split < VIEW_COLS * 7 / 10)
            split += 2;
        tour_step();
        picture_begin();
    }
}

// --- The title ---------------------------------------------------------------------------

#define LOGO_WAIT 14  // frames before the logo: the fade in first
#define TITLE_GOES 20 // frames the logo takes to fly off

// The story, a line at a time under the logo: each fades in, stays and fades
// out (SPRITE_BLEND, with the weights moving).
static const char* const story_lines[] = {
    "Wisps have taken the warren.",
    "Find the key.",
    "Reach the moon gate.",
};
#define STORY_LINES 3
#define STORY_TURN 200 // frames a line shows
#define STORY_FADE 16
static Label press_start, story;
static u8 story_line;
static u16 story_time;

static void menu_labels(void);

static void enter_title(void) {
    screen = SCREEN_TITLE;
    clock = 0;
    going = 0;
    story_time = 0;
    tour_start();
    set_colors(view_colors(VIEW_TITLE));
    menu_labels();
    show_group(&title_group);
    set_glass(0);
    picture_whole();
    sound_music(TUNE_TITLE);
}

static void enter_pick(void);

static void title_frame(bool leaving) {
    bool busy = false;
    int landed = clock - LOGO_WAIT - logo_frames();
    if (going) {
        // START was pressed: the logo flies off, PRESS START flickers, the
        // view darkens, and the menu comes when nothing of the title is left
        // on screen (its sprites' tiles become the menu's).
        going++;
        if (going == TITLE_GOES / 2)
            set_colors(view_colors(VIEW_BETWEEN));
        if (going >= TITLE_GOES) {
            enter_pick();
            tour_frame(false);
            return;
        }
    } else if (!leaving && button_pressed(BUTTON_START | BUTTON_A)) {
        psg_play(SND_CONFIRM);
        going = 1;
    }

    if (clock >= LOGO_WAIT && going < TITLE_GOES - 1)
        logo_draw(clock - LOGO_WAIT, going);
    if (landed >= 0 && going < TITLE_GOES - 1) {
        if (going) {
            if (going & 2)
                label_draw_centered(press_start, 116, 0);
        } else {
            pulse(SPR_START);
            label_draw_centered(press_start, 116, 0);

            // The story's line: its weight in the blend goes up, stays, and
            // comes down; then the next line is composed, unseen.
            u32 weight = story_time < STORY_FADE                 ? story_time
                         : story_time >= STORY_TURN - STORY_FADE ? STORY_TURN - story_time
                                                                 : STORY_FADE;
            set_glass(weight * 16 / STORY_FADE);
            if (weight)
                label_draw_centered(story, 140, SPRITE_BLEND);
            if (++story_time > STORY_TURN) {
                story_time = 0;
                story_line = (u8)((story_line + 1) % STORY_LINES);
                label_set(&story, story_lines[story_line]);
                busy = true;
            }
        }
    }
    tour_frame(busy);
}

// --- The menu ----------------------------------------------------------------------------

// The band and what stands on it, from the top of the screen.
#define PICK_TOP 4
#define PICK_HEIGHT 100
#define PICK_OPENS 8 // frames the band takes to open, and to close
#define ITEM_Y(i) (30 + (i) * 20)
#define BAR_BLOCKS 5

static Label heading, items[DIFFICULTY_COUNT], note, controls[6], footer;
static int bar_y; // of the bar under the chosen line, in 256ths of a pixel

static const char* const notes[DIFFICULTY_COUNT] = {
    "a few wisps, and gentle",
    "more wisps, and quicker",
    "every wisp, and fierce",
};

// Composes the menu's labels: at the title, behind its fade, so that the
// menu can come at once.
static void menu_labels(void) {
    static const char* const lines[6] = {
        "~+~ walk and turn", "~@~ the spark wand", "~< >~ step sideways",
        "~$~ open doors",    "~START~ pause",      "~SELECT~ CPU use",
    };
    labels_clear(SPR_WORD);
    labels_clear(SPR_LINE);
    heading = label_make(SPR_LINE, "~HOW BOLD ARE THE WISPS?~", 0);
    for (int i = 0; i < DIFFICULTY_COUNT; i++)
        items[i] = label_make(SPR_WORD, difficulty_names[i], 0);
    note = label_make(SPR_LINE, notes[difficulty], 4);
    for (int i = 0; i < 6; i++)
        controls[i] = label_make(SPR_LINE, lines[i], 0);
    footer = label_make(SPR_LINE, "~@~ begin   ~$~ back", 0);
}

static void enter_pick(void) {
    screen = SCREEN_PICK;
    clock = 0;
    going = 0;
    bar_y = ITEM_Y(difficulty) << 8;
    set_colors(view_colors(VIEW_MENU));
    show_group(&menu_group);
    set_glass(GLASS);
    button_repeat_reset();
}

static void enter_play(void);

// Back to the title without a fade: the band has closed.
static void pick_to_title(void) {
    screen = SCREEN_TITLE;
    clock = LOGO_WAIT;
    going = 0;
    story_time = 0;
    set_colors(view_colors(VIEW_TITLE));
    show_group(&title_group);
    set_glass(0);
}

static void pick_frame(bool leaving) {
    bool busy = false;
    if (going) {
        // B was pressed: the band closes, then a frame with none of the
        // menu's sprites, then the title's take their tiles.
        going++;
        if (going == PICK_OPENS + 2) {
            pick_to_title();
            tour_frame(false);
            return;
        }
    } else if (!leaving && clock >= PICK_OPENS) {
        if (button_repeat(BUTTON_UP | BUTTON_DOWN)) {
            int step = button_down(BUTTON_DOWN) ? 1 : DIFFICULTY_COUNT - 1;
            difficulty = (u8)((difficulty + step) % DIFFICULTY_COUNT);
            psg_play(SND_MOVE);
            label_set(&note, notes[difficulty]);
            busy = true;
        }
        if (button_pressed(BUTTON_A | BUTTON_START)) {
            psg_play(SND_CONFIRM);
            leave_to(enter_play, SCREEN_BRIGHTNESS_MIN);
        } else if (button_pressed(BUTTON_B)) {
            psg_play(SND_MOVE);
            going = 1;
            set_colors(view_colors(VIEW_BETWEEN));
        }
    }

    // The band opens from its middle, and closes to it.
    int open = going ? PICK_OPENS + 1 - going : clock;
    if (open > PICK_OPENS)
        open = PICK_OPENS;
    int height = 32 + (PICK_HEIGHT - 32) * open / PICK_OPENS;
    if (open == PICK_OPENS) {
        // The bar goes to the chosen line a part of the way each frame:
        // quick at first, then gently into place.
        int target = ITEM_Y(difficulty) << 8;
        bar_y += (target - bar_y) * 3 / 8;
        if (bar_y - target < 64 && target - bar_y < 64)
            bar_y = target;
        int y = bar_y >> 8;

        label_draw_centered(heading, 12, 0);
        for (int i = 0; i < DIFFICULTY_COUNT; i++)
            label_draw_centered(items[i], ITEM_Y(i),
                                SPRITE_PALETTE(i == difficulty ? PAL_GOLD : PAL_SOFT));
        // The spark beside the chosen line twinkles; while the game is being
        // entered it flickers faster.
        int twinkle = fade_to ? clock >> 1 & 1 : clock >> 4 & 1;
        sprite_draw(SPR_CURSOR, (u8)twinkle, 64, y - 2, 0);
        sprite_draw(SPR_CURSOR, (u8)twinkle, 160, y - 2, 0);
        label_draw_centered(note, 91, SPRITE_PALETTE(PAL_SOFT));
        for (int i = 0; i < 6; i++)
            label_draw(controls[i], i & 1 ? 128 : 20, 112 + (i >> 1) * 11, 0);
        label_draw_centered(footer, 148, SPRITE_PALETTE(PAL_SOFT));
        bar_draw((SCREEN_W - BAR_BLOCKS * 32) / 2, y - 4, BAR_BLOCKS,
                 SPRITE_BLEND | SPRITE_PALETTE(PAL_BAR));
    }
    if (open > 0)
        band_draw(PICK_TOP + (PICK_HEIGHT - height) / 2, height, SPRITE_BLEND);
    tour_frame(busy);
}

// --- The play screen --------------------------------------------------------------

static bool paused;
static u16 bob; // an angle: the wand sways with the serval's steps
// Presses since the last tick: a press in either frame of a picture counts.
static bool pressed_use, pressed_start;
// The A that began the game fires no spark: the wand waits for A to be let go.
static bool wand_ready;

static void enter_result(void);

static void enter_play(void) {
    show_group(NULL);
    set_glass(0);
    set_colors(art_palette(TINT_NONE));
    random_seed(frame_count()); // when START was pressed: different every time
    world_start(difficulty);
    screen = SCREEN_PLAY;
    paused = false;
    pressed_use = pressed_start = false;
    wand_ready = false;
    bob = 0;
    split = FIRST_COLS;
    picture_whole();
    sound_music(TUNE_WARREN);
}

static void draw_hud(void) {
    static const u8 paused_glyphs[] = {GLYPH_P, GLYPH_A, GLYPH_U, GLYPH_S, GLYPH_E, GLYPH_D};
    if (paused)
        for (int i = 0; i < 6; i++)
            sprite_draw(SPR_GLYPH, paused_glyphs[i], 91 + i * 10, 48, 0);
    sprite_draw(SPR_CROSS, 0, SCREEN_W / 2 - 4, SCREEN_H / 2 - 4, 0);

    // The paw and wand, bottom right of the middle, swaying with each step;
    // lit for a moment after a spark.
    int sway = fx_sin(bob) * 3 / FX_ONE;
    int dip = fx_cos((u16)(bob * 2)) * 2 / FX_ONE;
    u8 wand = player.flash > 2 ? 1 : player.flash ? 2 : 0;
    sprite_draw(SPR_WAND, wand, 130 + sway, SCREEN_H - 62 + dip, 0);

    sprite_draw(SPR_ICON, ICON_HEART, 4, SCREEN_H - 20, 0);
    draw_number(player.health, 3, 22, SCREEN_H - 20);
    if (player.has_key)
        sprite_draw(SPR_ICON, ICON_KEY, 52, SCREEN_H - 20, 0);
    sprite_draw(SPR_ICON, ICON_CRYSTAL, SCREEN_W - 40, SCREEN_H - 20, 0);
    draw_number(player.sparks, 2, SCREEN_W - 22, SCREEN_H - 20);
}

static void play_frame(bool leaving) {
    if (!leaving) {
        pressed_use |= button_pressed(BUTTON_B);
        pressed_start |= button_pressed(BUTTON_START);
    }

    if (half == 1) {
        picture_end();
    } else if (!leaving && !run.over) {
        if (pressed_start) {
            pressed_start = false;
            paused = !paused;
            psg_play(SND_PAUSE);
            if (paused)
                psg_music_pause();
            else
                psg_music_resume();
        }
        if (!button_down(BUTTON_A))
            wand_ready = true;
        if (!paused) {
            Controls input = {
                .forward = (s8)(button_down(BUTTON_UP) - button_down(BUTTON_DOWN)),
                .strafe = (s8)(button_down(BUTTON_R) - button_down(BUTTON_L)),
                .turn = (s8)(button_down(BUTTON_RIGHT) - button_down(BUTTON_LEFT)),
                .fire = wand_ready && button_down(BUTTON_A),
                .use = pressed_use,
            };
            pressed_use = false;
            world_tick(&input);
            if (input.forward || input.strafe)
                bob = (u16)(bob + ANGLE_DEG(12));
            // Red for a moment when hurt; and it stays red when that was the end.
            set_colors(art_palette(player.hurt || (run.over && !run.won) ? TINT_HURT : TINT_NONE));
            picture_begin();
        }
    } else if (run.over && !leaving) {
        // The last picture is on screen: out through white (the moon gate)
        // or black.
        sound_music(TUNE_NONE);
        psg_play(run.won ? SND_WON : SND_LOST);
        leave_to(enter_result, run.won ? SCREEN_BRIGHTNESS_MAX : SCREEN_BRIGHTNESS_MIN);
    }
    draw_hud();
}

// --- The results ----------------------------------------------------------------------------

// The run's last picture stays on the surface, in other colors (pale for the
// moon gate, dark red for the wisps), and the band opens over it once the
// fade is done: blending waits for a fade (screen.h), and the glass would be
// solid until then.
#define RESULT_TOP 14
#define RESULT_HEIGHT 132
#define RESULT_OPENS 10

static Label result_title, result_story, result_names[3], result_values[3], result_start;

static void enter_result(void) {
    screen = SCREEN_RESULT;
    clock = 0;
    going = 0;
    set_colors(view_colors(run.won ? VIEW_WON : VIEW_LOST));

    labels_clear(SPR_WORD);
    labels_clear(SPR_LINE);
    result_title = label_make(SPR_WORD, run.won ? "THE MOON GATE OPENS" : "THE WISPS HAVE YOU", 0);
    result_story = label_make(
        SPR_LINE, run.won ? "The serval slips out into the night." : "The warren keeps its serval.",
        0);
    u32 seconds = run.ticks / 30;
    result_names[0] = label_make(SPR_LINE, "TIME", 0);
    result_values[0] = label_make(SPR_LINE, text_format("%u:%02u", seconds / 60, seconds % 60), 0);
    result_names[1] = label_make(SPR_LINE, "WISPS", 0);
    result_values[1] = label_make(SPR_LINE, text_format("%u of %u", run.downed, run.wisps), 0);
    result_names[2] = label_make(SPR_LINE, "DIFFICULTY", 0);
    result_values[2] = label_make(SPR_LINE, difficulty_names[run.difficulty], 0);
    result_start = label_make(SPR_WORD, "PRESS START", 0);
    show_group(&menu_group);
    set_glass(GLASS);
}

static void result_frame(bool leaving) {
    bool fading_in = brightness && !fade_to;
    if (fading_in)
        clock = 0;
    if (!leaving && !going && button_pressed(BUTTON_START | BUTTON_A))
        going = 1;
    if (going && !fade_to) {
        // The band closes first, for the same reason, then the fade.
        going++;
        if (fading_in || going > RESULT_OPENS)
            leave_to(enter_title, SCREEN_BRIGHTNESS_MIN);
    }
    if (brightness)
        return;

    int open = going ? RESULT_OPENS + 1 - going : RESULT_OPENS;
    if (open > clock)
        open = clock;
    if (open <= 0)
        return;
    int height = 32 + (RESULT_HEIGHT - 32) * open / RESULT_OPENS;
    if (open == RESULT_OPENS) {
        label_draw_centered(result_title, 26, SPRITE_PALETTE(run.won ? PAL_GOLD : PAL_RED));
        label_draw_centered(result_story, 46, SPRITE_PALETTE(PAL_SOFT));
        for (int i = 0; i < 3; i++) {
            label_draw(result_names[i], 64, 66 + i * 14, SPRITE_PALETTE(PAL_SOFT));
            label_draw(result_values[i], 176 - result_values[i].width, 66 + i * 14, 0);
        }
        pulse(SPR_WORD);
        label_draw_centered(result_start, 120, SPRITE_PALETTE(PAL_PULSE));
    }
    band_draw(RESULT_TOP + (RESULT_HEIGHT - height) / 2, height, SPRITE_BLEND);
}

// --- The frame ------------------------------------------------------------------------

void game_init(void) {
    art_build();
    title_build();
    view_init();
    sound_init();
    sprite_table_set(sprite_table, SPRITE_COUNT);

    // The surface, on its side: screen columns are its rows (view.c). It
    // stays on for good.
    surface_begin();
    surface_set_transform(0, 0, 0, FX(1), FX(1) / 2, 0);
    // surface_begin() unloaded the sprites: the HUD's load into the half of
    // the sprite tiles the surface leaves.
    sprite_group_load(&hud_group);
    hud_mark = sprite_groups_mark();

    press_start = label_make(SPR_START, "PRESS START", 0);
    // All six of SPR_STORY's frames: room for the longest line.
    story = label_make(SPR_STORY, story_lines[story_line], 6);
    enter_title();
}

void game_frame(void) {
    bool leaving = fade_step();
    cpu_before = cpu_now;
    cpu_now = (u16)frame_cpu_permille();
    if (clock < 60000)
        clock++;
    if (button_pressed(BUTTON_SELECT))
        show_cpu = !show_cpu;
    draw_cpu(); // first: in front of everything else

    switch (screen) {
    case SCREEN_TITLE:
        title_frame(leaving);
        break;
    case SCREEN_PICK:
        pick_frame(leaving);
        break;
    case SCREEN_PLAY:
        play_frame(leaving);
        break;
    case SCREEN_RESULT:
        result_frame(leaving);
        break;
    default:
        break;
    }
}
