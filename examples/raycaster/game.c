// The game's screens: the title, the choice of difficulty, the play screen
// (the view, drawn over two frames, with the HUD's sprites over it) and the
// results; and the fades between them (game.h).
//
// The title, menu and results are text (text.h); the play screen is the
// pixel surface (surface.h). The two can't show together: the surface takes
// the text layer's video memory. So the switch is made while the screen is
// black, between a fade out and a fade in, and what the play screen must
// say is said with sprites (the HUD's numbers, PAUSED).

#include "game.h"

enum { SCREEN_TITLE, SCREEN_PICK, SCREEN_PLAY, SCREEN_RESULT };
static u8 screen;
static u8 difficulty = NORMAL;

static const char* const difficulty_names[DIFFICULTY_COUNT] = {"EASY", "NORMAL", "HARD"};

#define NIGHT COLOR_RGB(14, 12, 34) // the text screens' backdrop

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

// --- The play screen --------------------------------------------------------------

// A picture takes two frames: its left part is drawn in the first and the
// rest in the second, which ends with the flip. So the view runs at 30
// pictures a second whatever the frame's 60, each part inside a frame's
// time, and the world steps once per picture (world_tick()). The first frame
// also steps the world and sets the view up, so it draws fewer columns.
#define FIRST_COLS 52
static u8 half;      // which part this frame draws: 0 or 1
static bool playing; // the surface is on
static bool paused;
static bool show_cpu;
static u8 tint;        // TINT_*: the surface's colors now
static u16 bob;        // an angle: the wand sways with the serval's steps
static u16 cpu_now;    // the last two frames' CPU use, in thousandths: one
static u16 cpu_before; // picture's two halves
// Presses since the last tick: a press in either frame of a picture counts.
static bool pressed_use, pressed_start;
// The A that began the game fires no spark: the wand waits for A to be let go.
static bool wand_ready;

static void enter_result(void);

static void set_tint(u32 wanted) {
    if (tint == wanted)
        return;
    tint = (u8)wanted;
    surface_set_colors(0, art_palette(tint), 256);
}

static void enter_play(void) {
    text_clear();
    // The surface, on its side: screen columns are its rows (view.c).
    if (!surface_begin())
        return;
    playing = true;
    surface_set_transform(0, 0, 0, FX(1), FX(1) / 2, 0);
    tint = TINT_NONE;
    surface_set_colors(0, art_palette(TINT_NONE), 256);
    // surface_begin() unloaded the sprites: the HUD's load into the half of
    // the sprite tiles the surface leaves.
    sprite_group_load(&hud_group);

    random_seed(frame_count()); // when START was pressed: different every time
    world_start(difficulty);
    screen = SCREEN_PLAY;
    paused = false;
    pressed_use = pressed_start = false;
    wand_ready = false;
    half = 0;
    bob = 0;
    // The first picture whole, behind the black: one long frame nobody sees.
    view_begin();
    view_draw(0, VIEW_COLS);
    surface_flip();
    sound_music(true);
}

// A number in the HUD's digits, right-aligned in `digits` places from x.
static void draw_number(int value, int digits, int x, int y) {
    for (int i = digits - 1; i >= 0; i--) {
        sprite_draw(SPR_GLYPH, (u8)(GLYPH_0 + value % 10), x + i * 8, y, 0);
        value /= 10;
        if (!value)
            break;
    }
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

    if (show_cpu) {
        // The busier half of the last picture, as a percentage of a frame.
        int percent = (cpu_now > cpu_before ? cpu_now : cpu_before) / 10;
        draw_number(percent, 3, 4, 4);
        sprite_draw(SPR_GLYPH, GLYPH_PERCENT, 29, 4, 0);
    }
}

static void play_frame(bool leaving) {
    cpu_before = cpu_now;
    cpu_now = (u16)frame_cpu_permille();
    if (!leaving) {
        pressed_use |= button_pressed(BUTTON_B);
        pressed_start |= button_pressed(BUTTON_START);
        if (button_pressed(BUTTON_SELECT))
            show_cpu = !show_cpu;
    }

    if (half == 1) {
        view_draw(FIRST_COLS, VIEW_COLS);
        surface_flip();
        half = 0;
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
            set_tint(player.hurt || (run.over && !run.won) ? TINT_HURT : TINT_NONE);
            view_begin();
            view_draw(0, FIRST_COLS);
            half = 1;
        }
    } else if (run.over && !leaving) {
        // The last picture is on screen: out through white (the moon gate)
        // or black.
        sound_music(false);
        psg_play(run.won ? SND_WON : SND_LOST);
        leave_to(enter_result, run.won ? SCREEN_BRIGHTNESS_MAX : SCREEN_BRIGHTNESS_MIN);
    }
    draw_hud();
}

// --- The text screens ---------------------------------------------------------------

// Back from the surface to tiles and text, if the play screen was on.
static void end_play(void) {
    if (playing) {
        surface_end();
        playing = false;
    }
    screen_set_backdrop(NIGHT);
    text_set_shadow(true);
    text_set_style(TEXT_NORMAL);
    text_clear();
    button_repeat_reset();
}

static void enter_title(void) {
    end_play();
    screen = SCREEN_TITLE;
    text_set_style(TEXT_HIGHLIGHT);
    text_print_centered(4, "W I S P   W A R R E N");
    text_set_style(TEXT_NORMAL);
    text_print_centered(8, "Wisps have taken the warren.");
    text_print_centered(10, "Find the key.");
    text_print_centered(11, "Reach the moon gate.");
    text_set_style(3);
    text_print_centered(18, "A SERVAL ENGINE RAYCASTER");
    text_set_style(TEXT_NORMAL);
}

static void print_choice(void) {
    static const char* const notes[DIFFICULTY_COUNT] = {
        "a few wisps, and gentle",
        "more wisps, and quicker",
        "every wisp, and fierce",
    };
    for (int i = 0; i < DIFFICULTY_COUNT; i++) {
        text_set_style(i == difficulty ? TEXT_HIGHLIGHT : TEXT_NORMAL);
        text_print_line(8, 5 + i * 2,
                        text_format("%c %s", i == difficulty ? '>' : ' ', difficulty_names[i]));
    }
    text_set_style(3);
    text_print_centered(11, notes[difficulty]);
    text_set_style(TEXT_NORMAL);
}

static void enter_pick(void) {
    end_play();
    screen = SCREEN_PICK;
    text_print_centered(2, "HOW BOLD ARE THE WISPS?");
    print_choice();
    text_print(4, 13, "PAD    walk and turn");
    text_print(4, 14, "L R    step sideways");
    text_print(4, 15, "A      the spark wand");
    text_print(4, 16, "B      open doors");
    text_print(4, 17, "START  pause");
    text_set_style(3);
    text_print_centered(19, "A begin        B back");
    text_set_style(TEXT_NORMAL);
}

static void enter_result(void) {
    end_play();
    screen = SCREEN_RESULT;
    text_set_style(run.won ? TEXT_HIGHLIGHT : 2);
    text_print_centered(4, run.won ? "THE MOON GATE OPENS" : "THE WISPS HAVE YOU");
    text_set_style(TEXT_NORMAL);
    text_print_centered(6, run.won ? "The serval slips out" : "The warren keeps");
    text_print_centered(7, run.won ? "into the night." : "its serval.");
    u32 seconds = run.ticks / 30;
    text_print(8, 10, text_format("TIME        %u:%02u", seconds / 60, seconds % 60));
    text_print(8, 12, text_format("WISPS       %u OF %u", run.downed, run.wisps));
    text_print(8, 14, text_format("DIFFICULTY  %s", difficulty_names[run.difficulty]));
    text_set_style(3);
    text_print_centered(18, "PRESS START");
    text_set_style(TEXT_NORMAL);
}

// --- The frame ------------------------------------------------------------------------

void game_init(void) {
    art_build();
    view_init();
    sound_init();
    sprite_table_set(sprite_table, SPRITE_COUNT);
    text_set_style_color(3, COLOR_RGB(150, 154, 190), 0);
    enter_title();
}

void game_frame(void) {
    bool leaving = fade_step();
    switch (screen) {
    case SCREEN_TITLE:
        // PRESS START blinks: half a second on, half off.
        if (frame_count() % 60 < 30)
            text_print_centered(14, "PRESS START");
        else
            text_print_centered(14, "");
        if (!leaving && button_pressed(BUTTON_START | BUTTON_A)) {
            psg_play(SND_CONFIRM);
            leave_to(enter_pick, SCREEN_BRIGHTNESS_MIN);
        }
        break;

    case SCREEN_PICK:
        if (leaving)
            break;
        if (button_repeat(BUTTON_UP | BUTTON_DOWN)) {
            int step = button_down(BUTTON_DOWN) ? 1 : DIFFICULTY_COUNT - 1;
            difficulty = (u8)((difficulty + step) % DIFFICULTY_COUNT);
            psg_play(SND_MOVE);
            print_choice();
        }
        if (button_pressed(BUTTON_A | BUTTON_START)) {
            psg_play(SND_CONFIRM);
            leave_to(enter_play, SCREEN_BRIGHTNESS_MIN);
        } else if (button_pressed(BUTTON_B)) {
            leave_to(enter_title, SCREEN_BRIGHTNESS_MIN);
        }
        break;

    case SCREEN_PLAY:
        play_frame(leaving);
        break;

    case SCREEN_RESULT:
        if (!leaving && button_pressed(BUTTON_START | BUTTON_A))
            leave_to(enter_title, SCREEN_BRIGHTNESS_MIN);
        break;

    default:
        break;
    }
}
