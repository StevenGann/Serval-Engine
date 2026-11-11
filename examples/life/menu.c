// The menu: a band of dark glass over the world, dimmed and paused behind
// it. Up and down choose a row, left and right change its value (at once:
// a new theme recolors the world behind the band), A acts on RANDOM and
// CLEAR; SELECT or B closes it.

#include "life.h"

enum { ROW_STAMP, ROW_RULE, ROW_SPEED, ROW_COLORS, ROW_SOUND, ROW_RANDOM, ROW_CLEAR, ROWS };

#define ROW_FRAME 21 // label frames: 7 rows of 7, then the footer's 4
#define ROW_BLOCKS 7
#define FOOTER_FRAME (ROW_FRAME + ROWS * ROW_BLOCKS)
#define FOOTER_BLOCKS 4

#define BAND_TOP 16
#define BAND_ROWS 4 // of 32 pixels
#define ROW_Y 28
#define ROW_STEP 14
#define ROW_X 32
#define MENU_DIM 168
#define DIM_FRAMES 8

static Label rows[ROWS], footer;
static int row, dim_t;
static bool closing, redraw;

static const char* row_text(int r) {
    const LifeSettings* s = game_settings();
    switch (r) {
    case ROW_STAMP:
        return text_format("STAMP\t< %s >", life_pattern_names[s->stamp]);
    case ROW_RULE:
        return text_format("RULE\t< %s ~%s~ >", life_rules[s->rule].name,
                           life_rules[s->rule].notation);
    case ROW_SPEED:
        return text_format("SPEED\t< %u A SECOND >", (u32)speeds[s->speed]);
    case ROW_COLORS:
        return text_format("COLORS\t< %s >", theme_names[s->theme]);
    case ROW_SOUND:
        return s->sound ? "SOUND\t< ON >" : "SOUND\t< OFF >";
    case ROW_RANDOM:
        return "RANDOM\t~A~  SOW A NEW SOUP";
    default:
        return "CLEAR\t~A~  EMPTY THE WORLD";
    }
}

void menu_build(void) {
    for (int r = 0; r < ROWS; r++)
        label_init(&rows[r], SPR_TEXT, ROW_FRAME + r * ROW_BLOCKS, ROW_BLOCKS, row_text(r));
    label_init(&footer, SPR_TEXT, FOOTER_FRAME, FOOTER_BLOCKS, "~SELECT~ OR ~B~  BACK");
}

// (The rows change only here: they show what they did when it last closed.)
void menu_open(void) {
    row = 0;
    dim_t = 0;
    closing = false;
    redraw = false;
    button_repeat_reset();
    sound_play(SFX_OPEN);
}

// Steps a value by `d` within 0 .. count - 1, around if `wrap`.
static u8 stepped(u8 value, int d, int count, bool wrap) {
    int v = value + d;
    if (wrap)
        return (u8)((v + count) % count);
    return (u8)int_clamp(v, 0, count - 1);
}

static void change(int d) {
    LifeSettings* s = game_settings();
    u8 before;
    switch (row) {
    case ROW_STAMP:
        s->stamp = stepped(s->stamp, d, LIFE_PAT_COUNT, true);
        s->turns = 0;
        break;
    case ROW_RULE:
        s->rule = stepped(s->rule, d, LIFE_RULE_COUNT, true);
        game_apply_rule();
        break;
    case ROW_SPEED:
        before = s->speed;
        s->speed = stepped(s->speed, d, LIFE_SPEEDS, false);
        if (s->speed == before)
            return;
        break;
    case ROW_COLORS:
        s->theme = stepped(s->theme, d, LIFE_THEMES, true);
        break;
    case ROW_SOUND:
        s->sound = !s->sound;
        sound_set(s->sound);
        break;
    default:
        return;
    }
    label_set(&rows[row], row_text(row));
    sound_play(SFX_CHANGE);
}

bool menu_frame(void) {
    const LifeSettings* s = game_settings();
    // A new world shows a frame after it is made: making it and drawing it
    // in one frame would take more than a frame.
    if (redraw) {
        game_draw_world();
        redraw = false;
    }
    if (!closing) {
        if (button_repeat(BUTTON_UP | BUTTON_DOWN)) {
            row = (row + (button_down(BUTTON_DOWN) ? 1 : ROWS - 1)) % ROWS;
            sound_play(SFX_MOVE);
        }
        if (button_repeat(BUTTON_LEFT))
            change(-1);
        if (button_repeat(BUTTON_RIGHT))
            change(1);
        if (button_pressed(BUTTON_A) && (row == ROW_RANDOM || row == ROW_CLEAR)) {
            if (row == ROW_RANDOM)
                game_soup();
            else
                game_clear();
            redraw = true;
            sound_play(row == ROW_RANDOM ? SFX_RANDOM : SFX_CLEAR);
            closing = true;
        } else if (button_pressed(BUTTON_SELECT | BUTTON_B)) {
            closing = true;
            sound_play(SFX_CLOSE);
        }
        if (closing)
            game_resume();
    }
    dim_t = int_clamp(dim_t + (closing ? -2 : 1), 0, DIM_FRAMES);
    art_show(s->theme, (u32)(MENU_DIM * dim_t / DIM_FRAMES));
    if (closing && dim_t == 0) {
        game_save();
        return false;
    }

    // The rows over the glass, the chosen one bright.
    if (dim_t > DIM_FRAMES / 2 || !closing) {
        for (int r = 0; r < ROWS; r++)
            label_draw(&rows[r], ROW_X, ROW_Y + r * ROW_STEP,
                       SPRITE_PALETTE(r == row ? PAL_PICKED : PAL_SOFT));
        label_draw(&footer, ROW_X, ROW_Y + ROWS * ROW_STEP + 6, SPRITE_PALETTE(PAL_SOFT));
        // The chosen row's marker: a cell.
        sprite_draw(SPR_CURSOR, 0, ROW_X - 12, ROW_Y + row * ROW_STEP + 2, 0);
        for (int b = 0; b < BAND_ROWS; b++) {
            u8 frame = b == 0 || b == BAND_ROWS - 1 ? GLASS_EDGE : GLASS_FILL;
            u16 flip = b == BAND_ROWS - 1 ? SPRITE_FLIP_V : 0;
            for (int x = 0; x < SCREEN_W; x += 32)
                sprite_draw(SPR_GLASS, frame, x, BAND_TOP + b * 32, SPRITE_BLEND | flip);
        }
    }
    return true;
}
