// Shared declarations of the blackjack example's files: game.c (screens,
// states, the round's flow, HUD), rules.c (the shoe and hand totals),
// table.c (cards, chips, banners, number pops and sparks on screen), bank.c
// (the saved bankroll), art.c (graphics, built at boot) and sound.c.

#ifndef BLACKJACK_GAME_H
#define BLACKJACK_GAME_H

#include "serval/serval.h"

// --- Rules (rules.c) ---------------------------------------------------------
//
// A card is one byte: rank in the low four bits (1 = ace ... 10, 11 = jack,
// 12 = queen, 13 = king), suit above them.

enum { SUIT_SPADES, SUIT_HEARTS, SUIT_DIAMONDS, SUIT_CLUBS };
#define CARD(rank, suit) ((u8)((rank) | ((suit) << 4)))
#define CARD_RANK(c) ((c) & 0x0F)
#define CARD_SUIT(c) ((c) >> 4)
#define RANK_ACE 1
#define RANK_JACK 11
#define RANK_KING 13

#define SHOE_DECKS 6
#define SHOE_CARDS (52 * SHOE_DECKS)
#define HAND_MAX                                                                                   \
    11 // more cards than any hand can hold without busting (A,A,A,A,2,2,2,2,3,3,3 = 21)

typedef struct {
    u8 cards[HAND_MAX];
    u8 count;
} Hand;

void shoe_shuffle(void);       // all six decks back in, shuffled
bool shoe_needs_shuffle(void); // past the cut card (three quarters dealt)
u8 shoe_draw(void);
int shoe_left(void);

int hand_total(const Hand* h, bool* soft); // the best total; soft if an ace counts 11
bool hand_blackjack(const Hand* h);        // two cards making 21
int card_value(u8 card);                   // 1 for an ace, 10 for court cards
// The total as players say it: "7/17" for a soft 17, "21", "BUST 24".
const char* hand_total_text(const Hand* h);

// --- The bankroll (bank.c) ---------------------------------------------------

#define BANK_START 1000
#define BET_MIN 10
#define BET_MAX 500
#define BET_STEP 10

typedef struct {
    u32 bank;  // chips, between hands
    u32 best;  // the most the bankroll has held
    u16 bet;   // the last bet, offered again
    u16 hands; // rounds played
    u16 wins;  // hands won (blackjacks included)
    u16 blackjacks;
    u16 refills; // times the house staked a broke player again
    u16 unused;  // keeps the size a multiple of 4; saved as 0
} Bankroll;

extern Bankroll bankroll;
void bank_load(void);
void bank_save(void); // after each round: a save takes about a millisecond
void bank_reset(void);
bool bank_save_failed(void);

// --- Graphics (art.c) --------------------------------------------------------

enum {
    SPR_CARD,          // 32x64: the card's base (art in rows 8-55), frames CF_*
    SPR_NARROW_TOP,    // 16x32 over 16x16: the face and the back squashed to 14 pixels
    SPR_NARROW_BOTTOM, //   (flip); no hardware sprite is 16x64
    SPR_EDGE_TOP,      // 8x32 over 8x16: the card edge-on (flip)
    SPR_EDGE_BOTTOM,
    SPR_INDEX,     // 8x16: rank over a small suit, frame = (rank - 1) * 4 + suit
    SPR_PIP,       // 16x16: the big suit in the middle of number cards
    SPR_ACE,       // 32x32: the ace's bigger, chunkier suit
    SPR_COURT,     // 16x16: crown, tiara, plumed cap; frames COURT_*
    SPR_CHIP,      // 16x16: a chip seen from the side, frames CHIP_* (denominations)
    SPR_DIGIT,     // 8x16: outlined digits and signs, frames DIGIT_*; colors DIGITS_*
    SPR_LETTER,    // 16x16: banner letters, frames from letter_frame(); colors LETTERS_*
    SPR_BUTTON,    // 32x16: action buttons, frames BTN_*; greyed with PAL_BUTTON_OFF
    SPR_SPARK,     // 8x8: a twinkling star (plays once)
    SPR_CONFETTI,  // 8x8: a tumbling paper chip, frames per color
    SPR_FACE,      // metasprite: a face-up card, frame = (rank - 1) * 4 + suit; built at boot
    SPR_FACE_GLOW, //   the same with its base in PAL_GLOW (a winning hand)
    SPR_FLIP,      // metasprite: the two-piece narrow flip steps, frames FLIP_*
    SPRITE_COUNT
};

// SPR_CARD frames: the faces and the back, and the face and back squashed
// to 24 pixels for the flip (the engine can rotate sprites but not scale
// them; narrower steps are SPR_NARROW_* and SPR_EDGE_*). A winning hand's
// faces are drawn with PAL_GLOW (a gold outline), shadows with PAL_SHADOW.
enum {
    CF_COURT, // a jack's, queen's or king's face: a gold frame around the middle
    CF_FACE,
    CF_FACE_24, // 24 pixels wide
    CF_BACK_24,
    CF_BACK,
    CF_COUNT
};
enum { NF_FACE_14, NF_BACK_14 };                        // SPR_NARROW_* frames
enum { FLIP_NARROW_FACE, FLIP_NARROW_BACK, FLIP_EDGE }; // SPR_FLIP frames
enum { COURT_KING, COURT_QUEEN, COURT_JACK };           // + 3 for black suits
enum { CHIP_10, CHIP_50, CHIP_100, CHIP_500, CHIP_KINDS };
enum { DIGIT_PLUS = 10, DIGIT_MINUS, DIGIT_SLASH, DIGIT_CHIP, DIGIT_COUNT };
enum { BTN_HIT, BTN_STAND, BTN_DOUBLE, BTN_SPLIT, BTN_DEAL, BTN_COUNT };
#define SPARK_FRAMES 4
#define CONFETTI_COLORS 4

// The sprite group's palettes. Each sprite has its own; drawing it with
// SPRITE_PALETTE(n) in its flags recolors it with another: the same tiles
// make the gold-outlined winning cards, the shadows, the greyed buttons and
// the digits and banner letters in three colors each.
enum {
    PAL_CARD,
    PAL_GLOW,   // the card's palette with a gold outline
    PAL_SHADOW, // every color the shadow's
    PAL_CHIP,
    PAL_DIGIT,
    PAL_DIGIT_GOLD,
    PAL_DIGIT_RED,
    PAL_LETTER_GOLD,
    PAL_LETTER_RED,
    PAL_LETTER_SILVER,
    PAL_BUTTON,
    PAL_BUTTON_OFF,
    PAL_FX,
    PALETTE_COUNT
};

// Colors of SPR_DIGIT and SPR_LETTER, as sprite_draw flags.
#define DIGITS_WHITE 0                             // PAL_DIGIT, the sprite's own
#define DIGITS_GOLD SPRITE_PALETTE(PAL_DIGIT_GOLD) // counting up, winnings, 21
#define DIGITS_RED SPRITE_PALETTE(PAL_DIGIT_RED)   // losses, bust
#define LETTERS_GOLD 0                             // PAL_LETTER_GOLD, the sprite's own
#define LETTERS_RED SPRITE_PALETTE(PAL_LETTER_RED)
#define LETTERS_SILVER SPRITE_PALETTE(PAL_LETTER_SILVER)

extern const SpriteAsset* const sprite_table[SPRITE_COUNT];
extern const SpriteGroup sprite_group;
int letter_frame(char c); // SPR_LETTER_* frame of a letter, -1 for a space

// Background: two layers of swirling marbled paint, the same tiles with the
// table's colors (green felt) or the title's (plum).
extern const Tileset table_tileset;
extern const Tileset title_tileset;
extern const MapLayer swirl_layer;  // background 3
extern const MapLayer ribbon_layer; // background 2, mostly transparent
void art_build(void);               // converts and generates the art (boot)

// --- The table on screen (table.c) -------------------------------------------

#define CARD_W 32
#define CARD_H 48
#define SHOE_X 204 // where cards come from (a card's top-left)
#define SHOE_Y 6

// Card pieces on screen. Each is a card dealt to a hand (or the title's
// fan); they move, flip, wobble and glow by themselves once told where to go.
#define TABLE_CARDS 24

void table_reset(void);                                         // no cards, chips or effects
int table_deal(u8 card, int x, int y, bool face_up, int delay); // slides in from the shoe
int table_deal_from(u8 card, int from_x, int from_y, int x, int y, bool face_up, int delay);
bool table_shown(int id); // landed face up, done flipping: its rank counts on screen
void table_move(int id, int x, int y, int frames); // slides to (x, y)
void table_flip(int id);                           // turns over (face up, or back)
void table_lift(int id, int pixels);               // raised above its place (peeking)
void table_glow(int id, bool on);                  // gold outline (a winning hand)
void table_nudge(int id, int strength);            // a wobble, e.g. when a total is reached
void table_set_angle(int id, int angle);           // resting tilt in degrees (title fan)
void table_discard_all(void);                      // every card flies off to the left
bool table_busy(void);                             // anything still moving or flipping
void table_update(void);
void table_draw(void);

// Chips: the bet's stack beside the player's hand.
// The stack shows `amount` in chips; chips it didn't have drop in after
// `delay` frames (CHIPS_LANDING: once chips_fly's chips get there).
void chips_set(int amount, int x, int y, int delay);
#define CHIPS_LANDING 24
void chips_fly(int amount, int from_x, int from_y, int to_x, int to_y); // chips sliding over
void chips_draw(void);

// Banner: big letters that pop in one by one over a point, then stay.
// color: LETTERS_*.
void banner_show(const char* text, int center_x, int y, u16 color);
void banner_clear(void);

// Rising "+150" / "-50" pops.
void pop_number(int value, int center_x, int y);

// Bursts of sparks and confetti (entities, sys_movement and sys_animate).
void sparks_burst(int x, int y, int count, bool confetti);

// Big outlined numbers drawn with SPR_DIGIT: the bankroll and the totals.
// color: DIGITS_*. `bounce` makes each digit jump (0-8 pixels, decaying in
// the caller).
void draw_number(u32 value, int x, int y, u16 color, int bounce);
int number_width(u32 value);

// The action buttons along the bottom.
void buttons_draw(const u8* buttons, int count, int selected, u16 enabled_mask, int pressed);

// --- Game (game.c) -----------------------------------------------------------

void game_init(void);
void game_frame(void);

// --- Sound (sound.c) ---------------------------------------------------------

enum {
    SND_DEAL,    // a card sliding out of the shoe
    SND_FLIP,    // a card turning over
    SND_LAND,    // a card landing
    SND_CHIP_UP, // the bet raised
    SND_CHIP_DOWN,
    SND_CHIPS,   // chips sliding over
    SND_TICK,    // the bankroll counting
    SND_MOVE,    // the action cursor
    SND_PRESS,   // an action chosen
    SND_NOPE,    // an action not allowed now
    SND_SHUFFLE, // the shoe reshuffled
    SND_WIN,
    SND_BLACKJACK,
    SND_LOSE,
    SND_BUST,
    SND_PUSH,
    SND_START,
    SND_RESET,
    SND_REFILL, // the house stakes a broke player
    SOUND_COUNT
};
extern const PsgSound* const sound_table[SOUND_COUNT];
extern const PsgSong lounge_song; // the tune of the title and the table, looping
extern const u32 bass_wave[4];    // the walking bass's waveform, for the wave channel

// --- Game components (C_GAME bits) -------------------------------------------

#define C_SPARK C_GAME(0)

#endif // BLACKJACK_GAME_H
