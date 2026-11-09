// One song played.
//
// Time is the music's, never the frames': every frame starts from
// music_position(), the ticks heard since row 0 at the start of the picture
// being drawn (docs/audio.md#position), and the chart is in the same ticks,
// so nothing can drift, whatever the tempo, a pause or a frame that ran
// long. Three moments are taken from it, each a fixed time away, turned into
// ticks of the song (ms_to_ticks()) whenever a tick's length changes:
//
// - press time: music_ticks() - INPUT_MS - the player's calibration. A
//   press seen this frame was polled at the VBlank before (a frame before
//   music_ticks()) and happened on average half a frame before that poll.
//   Presses are judged at it, and notes expire against it.
// - draw time: music_ticks() + DRAW_MS. The pads' line, low on the screen,
//   shows about a frame after the VBlank music_ticks() names, so a note is
//   drawn where it is then.
// - the windows: PERFECT_MS and GOOD_MS either side of a note.
//
// The autoplay presses each note in the first frame whose press time has
// reached it, and is judged at that press time through play_press(), as a
// player with no calibration (0) who pressed in that frame would be: a note
// it presses too late is a GOOD or a MISS like anyone's. The readout
// (SELECT) shows how long after its note the latest of those presses came
// (a frame's worth of ticks or less while the music plays smoothly).
//
// The picture: the notes fall at a fixed number of pixels a tick for the
// difficulty, the pads flash on a press (brightly on a hit) by palette, a
// burst of sparks opens on a hit, the judgement word pops up over the stage
// with the combo under it, the serval dances to the beat (its frame follows
// the ticks: a paw on the beat, a bob between) and the field's colours
// pulse on every beat. START pauses: music_pause(), so the ticks stand still
// and the picture with them; CONTINUE resumes exactly where it was.

#include "play.h"

#include "art.h"
#include "songs.h"
#include "sound.h"

// --- Timing (docs/audio.md#position) ------------------------------------------------------

#define INPUT_MS 25   // a press seen this frame came about 1.5 frames before music_ticks()
#define DRAW_MS 17    // the pads' line shows about a frame after music_ticks()
#define PERFECT_MS 35 // either side of a note
#define GOOD_MS 80

// Frames the music may be stopped with notes left before the song ends
// anyway, every note left a MISS: never, with a module that plays to its
// end (its last note is long before it), but a song must always end.
#define STOPPED_LIMIT 30

// --- The picture ---------------------------------------------------------------------------

static const u16 lane_buttons[LANES] = {BUTTON_LEFT, BUTTON_RIGHT, BUTTON_B, BUTTON_A};

// How fast the notes fall, pixels a tick in 8.8: 36 pixels a beat on EASY,
// 48 on HARD.
static const u16 scroll[DIFF_COUNT] = {384, 512};

#define NOTE_SPRITES 32 // notes drawn at most (the screen holds about 10)
#define NOTE_HIDDEN_Y 8 // a note whose middle is above this line is under the panel

#define WORD_Y 34    // the judgement word's top, over the stage
#define WORD_TIME 40 // frames it stays
#define COMBO_Y 52   // the combo's digits' top, under it
#define COMBO_ROW 9  // ... and "COMBO" under them

#define FLASH_HIT (FLASH_LEVELS - 1) // a pad's flash on a hit; a stray press, less
#define FLASH_PRESS 3

#define BURST_TIME 9 // frames of a burst: each of its frames three
static const u8 burst_frame[BURST_TIME] = {0, 0, 0, 1, 1, 1, 2, 2, 2};

// The beat's pulse by the tick in the beat: the peak on the beat, gone
// before the middle of it.
static const u8 pulse_curve[BEAT_TICKS] = {7, 7, 6, 6, 5, 5, 4, 4, 3, 3, 2, 2,
                                           1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static const u8 word_palette[WORD_COUNT] = {PAL_GOLD,  PAL_MINT,  PAL_GREY, PAL_WHITE,
                                            PAL_WHITE, PAL_WHITE, PAL_WHITE};

// The pause menu.
enum { MENU_CONTINUE, MENU_RETRY, MENU_QUIT, MENU_ITEMS };
static const char* const menu_items[MENU_ITEMS] = {"CONTINUE", "RETRY", "QUIT"};
#define MENU_ROW 6 // "PAUSED", the items two rows under it
#define MENU_COL 11

#define READOUT_ROW 18 // SELECT's readout: the bottom two rows
#define READOUT_FRAMES 16

// --- State --------------------------------------------------------------------------------

typedef struct {
    const Song* song;
    const Chart* chart;
    u8 difficulty;
    bool autoplay;
    s16 calibration_ms;
    // The times, in 24.8 ticks of tick_samples samples.
    u16 tick_samples;
    FIXED input_delay, draw_ahead, calibration, perfect, good;
    FIXED now;       // music_ticks() of the picture drawn (held while paused)
    FIXED end;       // the module's end
    u16 draw_from;   // every note before it is judged
    u16 auto_next;   // the autoplay's next note
    FIXED auto_late; // the latest an autoplay press came after its note's tick
    u8 flash[LANES], burst[LANES];
    u8 word, word_time;
    u8 stopped; // frames the music has not been playing
    bool paused;
    u8 menu;
    // What the screen shows, so only changes are written.
    u8 shown_pulse, shown_flash[LANES];
    u32 shown_score;
    u16 shown_combo;
    bool combo_text;
    u8 digits[5], digit_count; // the combo's digits, most significant first
} State;

typedef struct {
    bool on, printed;
    u32 frames, cpu, drawn, line;        // the highest of the frames since it printed
    u32 cpu_peak, drawn_peak, line_peak; // ... and since it came on
    u32 cut, lost;                       // sprites cut short or lost since it came on
} Readout;

static State st SERVAL_EWRAM_BSS;
static Play play SERVAL_EWRAM_BSS;
static Readout ro SERVAL_EWRAM_BSS;

// --- Timing --------------------------------------------------------------------------------

static void set_timing(u16 tick_samples) {
    st.tick_samples = tick_samples;
    st.input_delay = ms_to_ticks(INPUT_MS, tick_samples);
    st.draw_ahead = ms_to_ticks(DRAW_MS, tick_samples);
    st.calibration = ms_to_ticks(st.calibration_ms, tick_samples);
    st.perfect = ms_to_ticks(PERFECT_MS, tick_samples);
    st.good = ms_to_ticks(GOOD_MS, tick_samples);
}

// The moment a press seen this frame happened, in the chart's ticks.
static FIXED press_time(void) {
    return st.now - st.input_delay - st.calibration;
}

// --- Judging --------------------------------------------------------------------------------

static void show_word(int word) {
    st.word = (u8)word;
    st.word_time = WORD_TIME;
}

static void hit(int lane, Judgement j) {
    st.flash[lane] = FLASH_HIT;
    st.burst[lane] = BURST_TIME;
    show_word(j == JUDGE_PERFECT ? WORD_PERFECT : WORD_GOOD);
}

static void presses(FIXED press) {
    for (int l = 0; l < LANES; l++) {
        if (!button_pressed(lane_buttons[l]))
            continue;
        sound_play(SND_LANE + l);
        Judgement j = play_press(&play, (u8)l, press);
        if (j != JUDGE_NONE)
            hit(l, j);
        else if (st.flash[l] < FLASH_PRESS)
            st.flash[l] = FLASH_PRESS;
    }
}

static void autoplay(FIXED press) {
    const Chart* c = st.chart;
    while (st.auto_next < c->count) {
        const RhythmNote* n = &c->notes[st.auto_next];
        FIXED t = (FIXED)(n->tick << 8);
        if (t > press)
            break;
        if (press - t > st.auto_late)
            st.auto_late = press - t;
        st.auto_next++;
        if (n->lane >= LANES)
            continue;
        sound_play(SND_LANE + n->lane);
        Judgement j = play_press(&play, n->lane, press);
        if (j != JUDGE_NONE)
            hit(n->lane, j);
    }
}

// --- Drawing --------------------------------------------------------------------------------

static void set_pulse(int level) {
    if (level == st.shown_pulse)
        return;
    st.shown_pulse = (u8)level;
    tileset_set_colors(BANK_FIELD * 16, field_pulse[level], PULSE_COLORS);
    for (int l = 0; l < LANES; l++)
        tileset_set_colors((BANK_PAD + l) * 16 + 1, &field_pulse[level][1], FIELD_SHARED);
}

static void set_flashes(void) {
    for (int l = 0; l < LANES; l++) {
        if (st.flash[l] == st.shown_flash[l])
            continue;
        st.shown_flash[l] = st.flash[l];
        tileset_set_colors((BANK_PAD + l) * 16 + PAD_COLOR0, pad_flash[l][st.flash[l]], PAD_COLORS);
    }
}

static void draw_word(int w, int y) {
    for (int piece = 0; piece < WORD_PIECES; piece++)
        sprite_draw(SPR_WORD, (u8)(w * WORD_PIECES + piece), STAGE_X - 64 + piece * 32, y,
                    SPRITE_PALETTE(word_palette[w]));
}

// The judgement word, popping up for its first frames; before it, through
// the first bar, the count-in's beat.
static void draw_judgement(int beat) {
    if (st.word_time) {
        int pop = st.word_time - (WORD_TIME - 3);
        draw_word(st.word, WORD_Y - (pop > 0 ? pop : 0));
    } else if (beat >= 0 && beat < 4) {
        draw_word(WORD_BEAT1 + beat, WORD_Y);
    }
}

static void draw_combo(void) {
    u16 combo = play_score(&play)->combo;
    if (combo != st.shown_combo) {
        st.shown_combo = combo;
        // Its digits: only when it changes, a few times a second at most.
        u8 n = 0, rev[5];
        u32 v = combo;
        do {
            rev[n++] = (u8)(v % 10);
            v /= 10;
        } while (v && n < 5);
        for (u8 i = 0; i < n; i++)
            st.digits[i] = rev[n - 1 - i];
        st.digit_count = n;
    }
    bool show = combo >= 2;
    if (show != st.combo_text) {
        st.combo_text = show;
        if (show)
            text_print_centered(COMBO_ROW, "COMBO");
        else
            text_clear_area(0, COMBO_ROW, TEXT_COLS, 1);
    }
    if (!show)
        return;
    // A digit's 16x16 frame holds it with its outline in columns 2-13.
    int x = STAGE_X - 2 - st.digit_count * DIGIT_ADVANCE / 2;
    for (int i = 0; i < st.digit_count; i++)
        sprite_draw(SPR_DIGIT, st.digits[i], x + i * DIGIT_ADVANCE, COMBO_Y, 0);
}

// The progress marker's x: along the groove as the song goes (a division
// a frame).
static int progress_x(void) {
    if (st.now >= st.end)
        return PROGRESS_X + PROGRESS_W;
    if (st.now > 0)
        return PROGRESS_X + (int)(st.now * PROGRESS_W / st.end);
    return PROGRESS_X;
}

static void draw_notes(int marker_x) {
    const Chart* c = st.chart;
    while (st.draw_from < c->count && play_note_judged(&play, st.draw_from))
        st.draw_from++;
    FIXED drawn = st.now + st.draw_ahead;
    s32 speed = scroll[st.difficulty];
    int count = 0;
    for (u16 i = st.draw_from; i < c->count && count < NOTE_SPRITES; i++) {
        if (play_note_judged(&play, i))
            continue; // hit or missed: gone
        const RhythmNote* n = &c->notes[i];
        s32 ahead = (s32)(n->tick << 8) - drawn; // until it crosses the pads' line
        int y = PAD_Y - (int)((ahead * speed) >> 16);
        if (y < NOTE_HIDDEN_Y)
            break; // the chart is sorted: every later note is higher still
        // Under the panel a note is hidden by it, but not where its box
        // meets the marker's, which is drawn in front of the panel: the
        // hardware takes a pixel's colour from the first sprite drawn there
        // and its layer from the frontmost sprite's box, so the note would
        // show around the marker. It waits until it is clear of it.
        if (y - 8 < PROGRESS_Y + 4 && lane_x[n->lane] + 8 > marker_x - 4 &&
            lane_x[n->lane] - 8 < marker_x + 4)
            continue;
        sprite_draw(SPR_NOTE, 0, lane_x[n->lane], y, SPRITE_PALETTE(PAL_LANE + n->lane));
        count++;
    }
}

// The serval's dance by the beat: a paw up on the beat (the left on even
// beats, the right on odd), standing, bobbing down on the half beat,
// standing. Still before the music and after it.
static void draw_serval(int beat, int into) {
    u8 frame = DANCE_IDLE;
    u16 flags = 0;
    if (beat >= 0 && st.now < st.end) {
        if (into < BEAT_TICKS / 4) {
            frame = DANCE_PAW;
            flags = beat & 1 ? SPRITE_FLIP_H : 0;
        } else if (into >= BEAT_TICKS / 2 && into < BEAT_TICKS * 3 / 4) {
            frame = DANCE_BOB;
        }
    }
    sprite_draw(SPR_SERVAL, frame, STAGE_X, STAGE_FOOT, flags);
}

static void draw(void) {
    // Which beat, and how many ticks into it (a division a frame).
    int beat = -1, into = 0;
    if (st.now >= 0) {
        beat = (int)(st.now / (BEAT_TICKS << 8));
        into = (int)((st.now - beat * (BEAT_TICKS << 8)) >> 8);
    }
    set_pulse(beat >= 0 && st.now < st.end ? pulse_curve[into] : 0);
    set_flashes();
    u32 score = play_score(&play)->score;
    if (score != st.shown_score)
        text_print(23, 0, text_format("%06u", st.shown_score = score));
    // Sprites, front to back.
    if (!st.paused) {
        draw_judgement(beat);
        draw_combo();
    }
    for (int l = 0; l < LANES; l++)
        if (st.burst[l])
            sprite_draw(SPR_BURST, burst_frame[BURST_TIME - st.burst[l]], lane_x[l], PAD_Y,
                        SPRITE_PALETTE(PAL_LANE + l));
    int marker_x = progress_x();
    draw_notes(marker_x);
    draw_serval(beat, into);
    sprite_draw(SPR_MARKER, 0, marker_x, PROGRESS_Y, SPRITE_ABOVE_FOREGROUND);
}

// A frame of the effects running down (not while paused).
static void animate(void) {
    for (int l = 0; l < LANES; l++) {
        if (st.flash[l])
            st.flash[l]--;
        if (st.burst[l])
            st.burst[l]--;
    }
    if (st.word_time)
        st.word_time--;
}

// --- The readout (SELECT) --------------------------------------------------------------------

static void readout_toggle(void) {
    bool on = !ro.on;
    ro = (Readout){.on = on, .printed = true};
    sprite_stats_scanlines(on); // its cost shows in the CPU load
    text_clear_area(0, READOUT_ROW, TEXT_COLS, 2);
}

// Every READOUT_FRAMES frames, the highest of them: the CPU load (percent
// of a frame, the readout's own scanline count included) and its highest
// since the readout came on; hardware sprites used and their highest; then
// the judgements so far, the busiest scanline's sprite time (cycles of
// 1,210), sprites that lost a line (CUT) and draws lost to the hardware's
// limits (LOST), both of which stay 0; with the autoplay, how long after
// its note's tick its latest press came, in milliseconds.
static void readout_draw(void) {
    if (!ro.on)
        return;
    u32 p = frame_cpu_permille();
    SpriteStats s = sprite_stats();
    ro.cut += s.cut_short;
    ro.lost += s.dropped + s.untransformed;
    if (!ro.printed) { // printing is slow, and would measure itself
        ro.cpu = p > ro.cpu ? p : ro.cpu;
        ro.drawn = s.drawn > ro.drawn ? s.drawn : ro.drawn;
        ro.line = s.busiest_line > ro.line ? s.busiest_line : ro.line;
    }
    ro.printed = false;
    ro.cpu_peak = ro.cpu > ro.cpu_peak ? ro.cpu : ro.cpu_peak;
    ro.drawn_peak = ro.drawn > ro.drawn_peak ? ro.drawn : ro.drawn_peak;
    ro.line_peak = ro.line > ro.line_peak ? ro.line : ro.line_peak;
    if (++ro.frames < READOUT_FRAMES)
        return;
    const Score* sc = play_score(&play);
    text_print_line(0, READOUT_ROW,
                    text_format("CPU %2u.%u PK %2u.%u SPR %2u PK %2u", ro.cpu / 10, ro.cpu % 10,
                                ro.cpu_peak / 10, ro.cpu_peak % 10, ro.drawn, ro.drawn_peak));
    const char* counts = text_format("P%u G%u M%u L%u C%u X%u", sc->perfect, sc->good, sc->miss,
                                     ro.line_peak, ro.cut, ro.lost);
    if (st.autoplay) {
        u32 samples = ((u32)st.auto_late * st.tick_samples) >> 8;
        u32 tenths = samples * 10000 / MUSIC_RATE; // 0.1 ms
        counts = text_format("%s A%u.%u", counts, tenths / 10, tenths % 10);
    }
    text_print_line(0, READOUT_ROW + 1, counts);
    ro.frames = ro.cpu = ro.drawn = ro.line = 0;
    ro.printed = true;
}

// --- Pausing --------------------------------------------------------------------------------

static void menu_print(void) {
    text_clear_area(0, MENU_ROW, TEXT_COLS, MENU_ITEMS + 2);
    text_set_style(TEXT_HIGHLIGHT);
    text_print_centered(MENU_ROW, "PAUSED");
    for (int i = 0; i < MENU_ITEMS; i++) {
        bool chosen = i == st.menu;
        text_set_style(chosen ? TEXT_HIGHLIGHT : TEXT_NORMAL);
        text_print(MENU_COL - 1, MENU_ROW + 2 + i, chosen ? ">" : " ");
        text_print(MENU_COL, MENU_ROW + 2 + i, menu_items[i]);
    }
    text_set_style(TEXT_NORMAL);
}

static void pause_song(void) {
    music_pause();
    st.paused = true;
    st.menu = MENU_CONTINUE;
    st.combo_text = false; // the menu covers its row: printed again after
    sound_play(SND_PAUSE);
    menu_print();
}

static void start_song(void);

static PlayResult pause_frame(void) {
    int choice = -1;
    if (button_pressed(BUTTON_UP) || button_pressed(BUTTON_DOWN)) {
        st.menu = (u8)((st.menu + (button_pressed(BUTTON_UP) ? MENU_ITEMS - 1 : 1)) % MENU_ITEMS);
        sound_play(SND_MOVE);
        menu_print();
    }
    if (button_pressed(BUTTON_START) || button_pressed(BUTTON_B))
        choice = MENU_CONTINUE;
    else if (button_pressed(BUTTON_A))
        choice = st.menu;
    if (choice == MENU_CONTINUE) {
        text_clear_area(0, MENU_ROW, TEXT_COLS, MENU_ITEMS + 2);
        st.paused = false;
        music_resume();
        sound_play(SND_SELECT);
    } else if (choice == MENU_RETRY) {
        sound_play(SND_SELECT);
        start_song();
    } else if (choice == MENU_QUIT) {
        text_clear_area(0, MENU_ROW, TEXT_COLS, MENU_ITEMS + 2);
        sound_play(SND_SELECT);
        music_stop();
        st.paused = false;
        sprite_stats_scanlines(false);
        return PLAY_QUIT;
    } else {
        // Held where it stopped, but kept current: the music's position
        // settles a frame or two after the pause, and the first frame after
        // CONTINUE must draw from where it settled, not jump to it.
        st.now = music_ticks();
    }
    draw();
    readout_draw();
    return PLAY_RUNNING;
}

// --- Playing -------------------------------------------------------------------------------

// The song from row 0, everything reset but the readout.
static void start_song(void) {
    st.draw_from = st.auto_next = 0;
    st.auto_late = 0;
    for (int l = 0; l < LANES; l++) {
        st.flash[l] = st.burst[l] = 0;
        st.shown_flash[l] = 0xFF; // written at the first draw
    }
    st.word_time = 0;
    st.stopped = 0;
    st.paused = false;
    st.shown_pulse = 0xFF;
    st.shown_score = 0xFFFFFFFF;
    st.shown_combo = 0xFFFF;
    st.combo_text = false;
    text_clear_area(0, 1, TEXT_COLS, READOUT_ROW - 1);
    text_print_line(1, 0,
                    text_format("%s %s%s", st.song->name, difficulty_names[st.difficulty],
                                st.autoplay ? " AUTO" : ""));
    music_play(st.song->module, false);
    MusicPosition pos = music_position();
    set_timing(pos.tick_samples);
    st.now = pos.ticks;
    st.end = (FIXED)(st.song->length_ticks << 8);
    play_start(&play, st.chart->notes, st.chart->count, st.perfect, st.good);
}

void play_begin(u8 song, u8 difficulty, bool autoplay, s16 calibration_ms) {
    st.song = &songs[song < SONG_COUNT ? song : 0];
    st.difficulty = difficulty < DIFF_COUNT ? difficulty : 0;
    st.chart = &st.song->charts[st.difficulty];
    st.autoplay = autoplay;
    st.calibration_ms = autoplay ? 0 : calibration_ms; // the autoplay presses on the beat
    text_clear();
    ro.on = false;
    sprite_stats_scanlines(false);
    art_show_play();
    start_song();
}

PlayResult play_frame(void) {
    if (button_pressed(BUTTON_SELECT))
        readout_toggle();
    if (st.paused)
        return pause_frame();
    if (button_pressed(BUTTON_START)) {
        pause_song();
        draw();
        readout_draw();
        return PLAY_RUNNING;
    }
    MusicPosition pos = music_position();
    if (pos.tick_samples && pos.tick_samples != st.tick_samples) {
        set_timing(pos.tick_samples);
        play_set_windows(&play, st.perfect, st.good);
    }
    st.now = pos.ticks;
    FIXED press = press_time();
    if (st.autoplay)
        autoplay(press);
    else
        presses(press);
    st.stopped = music_playing() ? 0 : (u8)int_min(st.stopped + 1, 255);
    // Every note gone by is a MISS (and, should the music stop with notes
    // left, every note left).
    if (play_expire(&play, st.stopped > STOPPED_LIMIT ? 0x7FFF0000 : press))
        show_word(WORD_MISS);
    animate();
    draw();
    readout_draw();
    if (play_done(&play) && (st.now >= st.end || st.stopped > STOPPED_LIMIT)) {
        sprite_stats_scanlines(false);
        return PLAY_FINISHED;
    }
    return PLAY_RUNNING;
}

const Score* play_result(void) {
    return play_score(&play);
}

void play_draw(void) {
    draw();
}
