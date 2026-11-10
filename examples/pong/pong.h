// pong.h: what the game (main.c) and its graphics (art.c) share.

#ifndef PONG_H
#define PONG_H

#include "serval/serval.h"

// Sprite IDs (the sprite table in art.c).
enum {
    SPR_BALL,
    SPR_PADDLE_PLAYER,
    SPR_PADDLE_CPU,
    SPR_PADDLE_FLASH, // a paddle for a few frames after it hits the ball
    SPR_GLOW,         // the halo behind a paddle, drawn see-through (SPRITE_BLEND)
    SPR_TRAIL,        // the ball's trail: 3 frames, largest to smallest
    SPR_SPARK,        // a spark of a hit or a goal: 3 frames, largest to smallest
    SPR_RING,         // the ring that closes in on the ball before a serve: 3 frames
    SPR_BIG,          // 32x32 letters and digits: a frame per glyph (glyph())
    SPR_SMALL,        // the same glyphs at 16x16
    SPRITE_COUNT
};

// Palettes. Every one has the same roles, so any art can be drawn in any of
// them (SPRITE_PALETTE): 1 base, 2 highlight, 3 shadow, 4 outline, 5 glow,
// 6 shine.
enum { PAL_PLAYER, PAL_CPU, PAL_FLASH, PAL_BALL, PAL_GOLD, PALETTE_COUNT };

extern const SpriteAsset* const sprite_table[SPRITE_COUNT];
extern const SpriteGroup pong_group;

// The court, on background 3: rails, the net, the center circle and the goal
// lines. Its empty pixels show the backdrop, which main.c shades and flashes.
// Not const: art_build() counts its tiles.
extern Tileset court_tileset;
extern const MapLayer court_layer;

// Draws every sprite and the court into RAM. Call once, before loading them.
void art_build(void);

// The frame of SPR_BIG and SPR_SMALL that shows character c (digits, and the
// letters of "PONG", "YOU WIN!", "CPU WINS" and "PAUSED"), or -1.
int glyph(char c);

#endif // PONG_H
