// The pictures that follow the music and the pads.
//
// Everything that keeps time with the music reads music_position(): where
// the module is, in its ticks, at the VBlank that shows this frame. Both
// tunes have 6 ticks a row and 4 rows a beat, so the beat's phase and the
// bar's beat come from its row and tick, and stay in step with the music
// whatever music_set_speed() does to the tempo. On each beat the marquee's
// trim, the backdrop and the title flare and settle, and the beat lamp of
// the bar moves on; the marquee's bulbs chase one step a row; the record
// turns one turn a bar (33 rpm at THEME's 132 BPM), from the ticks heard,
// so it stops dead when the music pauses and spins faster as SPEED goes up.
//
// The pads' sounds are pads.lua's; this file watches the same buttons to
// light the pad pressed and throw its particles: a coin for A, a beam for B
// to the speaker the laser was panned to (the script's laser_left), a
// burst, a flash and a shake for L, ripples while R holds the hum.

#include "show.h"

#include "art.h"
#include "pads_script.h"

static const char title[] = "SERVAL JUKEBOX";

// --- The beat -------------------------------------------------------------------------------

typedef struct {
    bool on;    // the music is playing (paused included)
    int beat;   // the bar's beat, 0-3
    int phase;  // how far into the beat, 0-255 (held while paused)
    int row;    // the pattern's row
    int order;  // its order-list entry
    FIXED tick; // ticks heard (MusicPosition.ticks)
} Beat;

static Beat beat_now(void) {
    Beat b = {0};
    if (!music_playing())
        return b;
    MusicPosition pos = music_position();
    if (pos.speed == 0)
        return b;
    int into = (pos.row % 4) * pos.speed + pos.tick; // ticks into the beat
    b.on = true;
    b.beat = (pos.row / 4) % 4;
    b.phase = pos.ticks < 0 ? 255 : (into * 256 + (pos.ticks & 0xFF)) / (4 * pos.speed);
    b.row = pos.row;
    b.order = pos.order;
    b.tick = pos.ticks;
    return b;
}

// --- State ----------------------------------------------------------------------------------

typedef enum { FX_COIN, FX_SPARK, FX_BEAM, FX_WAVE } FxKind;

typedef struct {
    s16 x, y, vx, vy; // sixteenths of a pixel (a frame)
    u8 life, age, kind, pad;
} Particle;

#define PARTICLES 24

static struct {
    int fade;         // the fade-in: from SCREEN_BRIGHTNESS_MIN up to 0
    int flash, shake; // the boom's: brightness, and frames of shaking
    int brightness;   // as last set
    int pulse, bulb;  // as last written (-1: never)
    int pad[PADS];    // each pad's flash level
    int pad_shown[PADS];
    int meter[2];    // each meter's lamps lit, as last written (-1: never)
    int cursor_y;    // sixteenths of a pixel, easing to the cursor's row
    int knob_x;      // likewise, to the speed's place on its slider
    FIXED last_tick; // the record's turn: ticks heard, last frame
    u32 turn;        // and how far round it is, in 256ths of a tick (a bar a turn)
    int now_state, now_row, now_order; // the now-playing lines, as printed
    Particle fx[PARTICLES];
} s SERVAL_EWRAM_BSS;

static int ox, oy; // this frame's shake, added to every sprite

static void draw(u16 id, int frame, int x, int y, u16 flags) {
    sprite_draw(id, (u8)frame, x + ox, y + oy, flags);
}

void show_start(void) {
    screen_set_brightness(SCREEN_BRIGHTNESS_MIN);
    art_build();
    art_load();
    raster_backdrop(art_backdrop(0));

    text_set_shadow(true);
    text_set_color(COLOR_RGB(244, 240, 255), COLOR_RGB(10, 6, 22));
    text_set_style_color(TEXT_HIGHLIGHT, COLOR_RGB(255, 214, 96), COLOR_RGB(10, 6, 22));
    text_set_style_color(STYLE_DIM, COLOR_RGB(150, 136, 196), COLOR_RGB(10, 6, 22));
    text_set_style_color(STYLE_ALERT, COLOR_RGB(255, 112, 136), COLOR_RGB(10, 6, 22));

    s.fade = SCREEN_BRIGHTNESS_MIN;
    s.brightness = SCREEN_BRIGHTNESS_MIN;
    s.pulse = s.bulb = s.now_state = s.meter[0] = s.meter[1] = -1;
    for (int p = 0; p < PADS; p++)
        s.pad_shown[p] = 0;
    s.cursor_y = MENU_ROW(ROW_MUSIC) * 8 * 16;
    s.knob_x = SPEED_X(100) * 16;
}

// --- Particles ------------------------------------------------------------------------------

static void spawn(FxKind kind, int pad, int x, int y, int vx, int vy, int life) {
    for (int i = 0; i < PARTICLES; i++) {
        Particle* p = &s.fx[i];
        if (p->life)
            continue;
        *p = (Particle){(s16)(x * 16), (s16)(y * 16), (s16)vx, (s16)vy, (u8)life, 0,
                        (u8)kind,      (u8)pad};
        return;
    }
}

static void sparks(int pad, int x, int y, int count, int speed, int life) {
    for (int i = 0; i < count; i++) {
        u16 a = (u16)(i * 65536 / count + random_range(0, 4096));
        int v = speed + random_range(0, speed / 2);
        spawn(FX_SPARK, pad, x, y, fx_cos(a) * v / FX_ONE, fx_sin(a) * v / FX_ONE, life);
    }
}

// The pads: lit, and their particles thrown, as pads.lua plays them.
static void pads(void) {
    if (button_pressed(BUTTON_A)) {
        s.pad[PAD_COIN] = FLASH_LEVELS - 1;
        spawn(FX_COIN, PAD_COIN, PAD_CAP_X(PAD_COIN), PAD_TOP, random_range(-6, 6), -44, 40);
        sparks(PAD_COIN, PAD_CAP_X(PAD_COIN), PAD_CAP_Y, 4, 16, 14);
    }
    if (button_pressed(BUTTON_B)) {
        s.pad[PAD_LASER] = FLASH_LEVELS - 1;
        // pads.lua has just turned laser_left over for the next one.
        int dir = vm_global(G_LASER_LEFT) ? 1 : -1;
        spawn(FX_BEAM, PAD_LASER, PAD_CAP_X(PAD_LASER), PAD_TOP - 5, dir * 112, 0, 36);
        sparks(PAD_LASER, PAD_CAP_X(PAD_LASER), PAD_TOP - 5, 3, 12, 10);
    }
    if (button_pressed(BUTTON_L)) {
        s.pad[PAD_BOOM] = FLASH_LEVELS - 1;
        sparks(PAD_BOOM, PAD_CAP_X(PAD_BOOM), PAD_CAP_Y, 10, 40, 20);
        s.flash = 6;
        s.shake = 12;
    }
    if (button_down(BUTTON_R) && vm_global(G_HUM)) {
        // The hum's glow throbs while it plays, and ripples rise from it.
        int throb = 4 + fx_sin((u16)(frame_count() * 2048)) * 3 / FX_ONE;
        s.pad[PAD_HUM] = int_max(s.pad[PAD_HUM], throb);
        if (frame_count() % 8 == 0)
            spawn(FX_WAVE, PAD_HUM, PAD_CAP_X(PAD_HUM) + random_range(-10, 10), PAD_TOP + 2, 0, -12,
                  16);
    }
    for (int p = 0; p < PADS; p++) {
        if (s.pad[p] != s.pad_shown[p]) {
            art_pad_flash(p, s.pad[p]);
            s.pad_shown[p] = s.pad[p];
        }
        if (s.pad[p] && frame_count() % 2 == 0)
            s.pad[p]--;
    }
}

static void particles(void) {
    for (int i = 0; i < PARTICLES; i++) {
        Particle* p = &s.fx[i];
        if (!p->life)
            continue;
        p->x = (s16)(p->x + p->vx);
        p->y = (s16)(p->y + p->vy);
        p->age++;
        p->life--;
        int x = p->x / 16, y = p->y / 16;
        u16 pal = SPRITE_PALETTE(PAL_COIN + p->pad);
        switch (p->kind) {
        case FX_COIN:
            p->vy = (s16)(p->vy + 3); // falls back
            draw(SPR_COIN, p->age / 3 % COIN_FRAMES, x, y, 0);
            break;
        case FX_SPARK:
            p->vx = (s16)(p->vx * 7 / 8); // slows
            p->vy = (s16)(p->vy * 7 / 8);
            draw(SPR_SPARK, p->life < 6, x, y, pal);
            break;
        case FX_BEAM:
            draw(SPR_BEAM, 0, x, y, 0);
            break;
        case FX_WAVE:
            if (p->life > 4 || p->life % 2)
                draw(SPR_WAVE, 0, x + (p->age / 6 % 2), y, 0);
            break;
        }
    }
}

// --- The cabinet's lights -------------------------------------------------------------------

static void lights(const Beat* b) {
    bool running = b->on && !music_paused();
    int level = 0;
    if (running) {
        int fall = 256 - b->phase; // the beat's flare, falling off quickly
        level = (PULSE_LEVELS - 1) * fall * fall / 65536;
    }
    if (level != s.pulse) {
        art_pulse(level);
        raster_backdrop(art_backdrop(level));
        s.pulse = level;
    }
    int bulb = b->on ? b->row % 4 : (int)(frame_count() / 30 % 4); // idle: a slow chase
    if (bulb * PULSE_LEVELS + level != s.bulb) {
        art_bulbs(bulb, level);
        s.bulb = bulb * PULSE_LEVELS + level;
    }

    // The bar's beats: the one sounding lit, bright then dimming; held and
    // blinking while paused.
    for (int i = 0; i < LAMPS && b->on; i++) {
        if (i != b->beat)
            continue;
        int frame = b->phase < 160 ? LAMP_BRIGHT : LAMP_DIM;
        if (!running)
            frame = frame_count() / 20 % 2 ? LAMP_BRIGHT : LAMP_DIM;
        draw(SPR_LAMP, frame, LAMP_X + i * LAMP_STEP, NOW_ROW * 8 + 4,
             SPRITE_PALETTE(i == 0 ? PAL_LAMP_DOWN : PAL_LAMP));
    }

    // The title: a ripple runs along its letters on each beat.
    static const u8 hop[6] = {1, 2, 3, 3, 2, 1};
    for (int i = 0; title[i]; i++) {
        if (title[i] == ' ')
            continue;
        int f = 0;
        while (title_letters[f] != title[i])
            f++;
        int local = b->phase - i * 12, dy = 0;
        if (running && local >= 0 && local < 96)
            dy = hop[local / 16];
        draw(SPR_LETTER, f, TITLE_X + i * LETTER_ADVANCE, TITLE_Y - dy, 0);
    }
}

// --- The deck and the panel -----------------------------------------------------------------

static void deck(const Jukebox* j, const Beat* b) {
    // Turned by the ticks heard since last frame: none while paused or
    // stopped; a new tune starts again from 0 without a jump back.
    if (b->on) {
        if (b->tick > s.last_tick)
            s.turn = (s.turn + (u32)(b->tick - s.last_tick)) % (96 * 256);
        s.last_tick = b->tick;
    } else {
        s.last_tick = 0;
    }
    u16 angle = (u16)(s.turn * 8 / 3); // 96 ticks (a bar) a turn
    draw(SPR_ARM, j->tune == TUNE_OFF ? ARM_REST : ARM_PLAY, ARM_X, ARM_Y, 0);
    draw(SPR_GLARE, 0, RECORD_X, RECORD_Y, 0);
    sprite_draw_rotated(SPR_RECORD, 0, RECORD_X + ox, RECORD_Y + oy, angle,
                        SPRITE_PALETTE(PAL_RECORD + j->tune));
}

static void panel(const Jukebox* j, const Beat* b) {
    // The cursor eases to its row, and nods on the beat.
    int target = MENU_ROW(j->row) * 8 * 16;
    s.cursor_y += (target - s.cursor_y) / 3;
    int nod = b->on && !music_paused() && b->phase < 64 ? 1 : 0;
    draw(SPR_CURSOR, 0, 79, (s.cursor_y + 8) / 16 - nod, 0);

    // The meters: ten lamps each, lit by palette writes when they change.
    const int levels[2] = {j->volume, j->effects};
    for (int m = 0; m < 2; m++) {
        int lit = (levels[m] * LEDS + 127) / 255;
        if (lit != s.meter[m]) {
            art_meter(m, lit);
            s.meter[m] = lit;
        }
    }
    s.knob_x += (SPEED_X(j->speed) * 16 - s.knob_x) / 3;
    draw(SPR_KNOB, 0, (s.knob_x + 8) / 16, MENU_ROW(ROW_SPEED) * 8 + 4, 0);

    // The now-playing lines: the state, and where the module is.
    int state = !b->on ? 0 : music_paused() ? 1 : 2;
    if (state != s.now_state) {
        static const char* const names[3] = {"STOPPED", "PAUSED", "PLAYING"};
        text_set_style(state == 1 ? STYLE_ALERT : state == 2 ? TEXT_HIGHLIGHT : STYLE_DIM);
        text_print(11, NOW_ROW, "       ");
        text_print(11, NOW_ROW, names[state]);
        text_set_style(TEXT_NORMAL);
        s.now_state = state;
        s.now_row = -1;
    }
    int row = b->on ? b->row : -2, order = b->on ? b->order : -2;
    if (row != s.now_row || order != s.now_order) {
        text_set_style(STYLE_DIM);
        text_print_line(11, NOW_ROW + 1,
                        b->on ? text_format("ORDER %02d  ROW %02d", order, row)
                              : "ORDER --  ROW --");
        text_set_style(TEXT_NORMAL);
        s.now_row = row;
        s.now_order = order;
    }
}

// --- A frame --------------------------------------------------------------------------------

void show_frame(const Jukebox* j) {
    Beat b = beat_now();

    // The boom's shake, the background moving with the sprites.
    ox = oy = 0;
    if (s.shake) {
        s.shake--;
        int amount = s.shake > 6 ? 2 : 1;
        ox = random_range(-amount, amount);
        oy = random_range(-amount, amount);
    }
    map_set_scroll(CABINET_BG, -ox, -oy);

    // The fade-in, then the boom's flash.
    if (s.fade < 0)
        s.fade++;
    int bright = s.fade < 0 ? s.fade : s.flash;
    if (s.flash)
        s.flash--;
    if (bright != s.brightness) {
        screen_set_brightness(bright);
        s.brightness = bright;
    }

    pads();
    particles(); // in front of everything else
    panel(j, &b);
    lights(&b);
    deck(j, &b);
}
