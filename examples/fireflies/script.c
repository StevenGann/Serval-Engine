// The game's logic: every rule of fireflies, as a bytecode listing for Serval
// Engine's VM (docs/vm.md). One op per line; the comment after each op is the
// value stack after it, top on the right ("e" an entity, "x"/"y" positions,
// "fx" a FIXED value). Handlers end in HALT; subroutines in RET.
//
// The listing is assembled at boot (asm.c) into a blob in EWRAM. In a game
// made with Studio Advance, the editor's script compiler emits the blob as
// ROM data from event blocks instead; this file is what such a compiler's
// output reads like, written by hand.
//
// Objects (game.h):
//   Room (thread, Room Start)  sets the round up, fades in, counts 60 seconds
//                              down on the HUD, then: TIME UP, the score,
//                              PRESS START, a fade out and G_RESTART for C
//   Spawner (thread)           a firefly every 40-90 frames, at most 8 alive,
//                              never right on the serval
//   Player: Create, Step       the D-pad sets its velocity; walk or stand
//   Firefly: Create            wanders along random paths until its time is
//                              up, then fades out; Collision: caught (score,
//                              chime, sparkle, a jingle every 10); Destroy:
//                              one fewer alive
//   Sparkle: Create, Anim End  a burst that removes itself when it's done
//   Resting: Create            the serval sitting down when time is up
//
// Globals are in game.h (G_*), locals per handler are named below.

#include "asm.h"
#include "game.h"

// --- Strings -------------------------------------------------------------------

enum {
    STR_SCORE,
    STR_TIME,
    STR_BLANK2, // clears the time before it is printed again (9 after 10)
    STR_BLANK3, // the same for the score (0 after the last round's 23)
    STR_BLANK_LINE,
    STR_HINT,
    STR_TIME_UP,
    STR_CAUGHT,
    STR_PRESS_START,
    STR_COUNT
};

static void strings(void) {
    asm_string(STR_SCORE, "SCORE");
    asm_string(STR_TIME, "TIME");
    asm_string(STR_BLANK2, "  ");
    asm_string(STR_BLANK3, "   ");
    asm_string(STR_BLANK_LINE, "                    ");
    asm_string(STR_HINT, "CATCH THE FIREFLIES!");
    asm_string(STR_TIME_UP, "TIME UP!");
    asm_string(STR_CAUGHT, "CAUGHT");
    asm_string(STR_PRESS_START, "PRESS START");
}

// --- Numbers the listing uses ----------------------------------------------------

#define SPEED (FX(3) / 2) // the serval: 1.5 pixels per frame

// HUD and messages: text cells (30 x 20).
#define SCORE_COL 7 // "SCORE" at 1
#define TIME_COL 28 // "TIME" at 23
#define HINT_COL 5  // 20 characters, centered
#define HINT_ROW 6  // above the serval's ears
#define TIME_UP_ROW 8
#define CAUGHT_ROW 10
#define START_ROW 12

#define PLAYER_X ((SCREEN_W - SERVAL_BODY_W) / 2)
#define PLAYER_Y ((FIELD_TOP + SCREEN_H - SERVAL_BODY_H) / 2)

// Where fireflies appear (their top-left, in pixels).
#define SPAWN_LEFT 8
#define SPAWN_RIGHT (SCREEN_W - 16)
#define SPAWN_TOP (FIELD_TOP + 16)
#define SPAWN_BOTTOM (SCREEN_H - 32)
#define SPAWN_CLEAR 40 // pixels between a new firefly and the serval, at least

// The Spawner's delay, in frames: 40-90 at first; every 10 points both ends
// come down, until the longest is at most SPAWN_MAX_FLOOR.
#define SPAWN_MIN_START 40
#define SPAWN_MAX_START 90
#define SPAWN_MIN_STEP 5
#define SPAWN_MAX_STEP 12
#define SPAWN_MAX_FLOOR 42

#define FLIGHTS_MIN 3 // a firefly's life, in flights (about 2.3 s each with its hover)
#define FLIGHTS_MAX 4

#define DEPTH_SERVAL 10 // sys_render_by_depth: higher in front
#define DEPTH_FIREFLY 20
#define DEPTH_SPARKLE 30

#define FADE_STEP 2 // brightness change per frame in fades

_Static_assert(SPRITE_FLIP_H == 1, "the Collision handler ORs a comparison (0 or 1) into flags");

// --- Subroutines -----------------------------------------------------------------

// print_score ( -- ): the score on the HUD.
static void print_score(AsmLabel self) {
    LABEL(self);
    PUSH(SCORE_COL);        // col
    PUSH(0);                // col row
    PUSH(STR_BLANK3);       // col row str
    SYS(TEXT_PRINT);        //
    PUSH(SCORE_COL);        // col
    PUSH(0);                // col row
    LDG(G_SCORE);           // col row score
    SYS(TEXT_PRINT_NUMBER); //
    OP(RET);                //
}

// print_time ( -- ): the seconds left on the HUD.
static void print_time(AsmLabel self) {
    LABEL(self);
    PUSH(TIME_COL);         // col
    PUSH(0);                // col row
    PUSH(STR_BLANK2);       // col row str
    SYS(TEXT_PRINT);        //
    PUSH(TIME_COL);         // col
    PUSH(0);                // col row
    LDG(G_TIME);            // col row time
    SYS(TEXT_PRINT_NUMBER); //
    OP(RET);                //
}

// TEXT_PRINT of string `str` at a text cell: four ops as one line of the
// listing.
static void print(s32 col, s32 row, s32 str) {
    PUSH(col);       // col
    PUSH(row);       // col row
    PUSH(str);       // col row str
    SYS(TEXT_PRINT); //
}

// --- Room ------------------------------------------------------------------------

// Room Start, run as a thread. Local 0: the brightness while fading.
static void room(AsmLabel print_score_, AsmLabel print_time_) {
    AsmLabel fade_in = asm_label(), faded_in = asm_label(), tick = asm_label();
    AsmLabel hint_stays = asm_label(), no_tick = asm_label(), wait_start = asm_label();
    AsmLabel fade_out = asm_label();

    HANDLER(OBJ_ROOM, ROOM_START);
    // A new round: every global set, whatever the last round left.
    PUSH(0);               // 0
    STG(G_SCORE);          //
    PUSH(0);               // 0
    STG(G_LIVE);           //
    PUSH(0);               // 0
    STG(G_RESTART);        //
    PUSH(ROUND_SECONDS);   // 60
    STG(G_TIME);           //
    PUSH(1);               // 1
    STG(G_PLAYING);        //
    PUSH(SPAWN_MIN_START); // 40
    STG(G_SPAWN_MIN);      //
    PUSH(SPAWN_MAX_START); // 90
    STG(G_SPAWN_MAX);      //
    PUSH(SND_START);       // sound
    SYS(PSG_PLAY);         //
    PUSH(SONG_DUSK);       // song
    SYS(MUSIC_PLAY);       //
    // The HUD, and the last round's messages cleared.
    print(1, 0, STR_SCORE);
    print(TIME_COL - 5, 0, STR_TIME);
    CALL(print_score_); //
    CALL(print_time_);  //
    print(HINT_COL, TIME_UP_ROW, STR_BLANK_LINE);
    print(HINT_COL, CAUGHT_ROW, STR_BLANK_LINE);
    print(HINT_COL, START_ROW, STR_BLANK_LINE);
    print(HINT_COL, HINT_ROW, STR_HINT);
    // The serval, in the middle of the meadow.
    PUSH(FX(PLAYER_X)); // x
    PUSH(FX(PLAYER_Y)); // x y
    SPAWN(OBJ_PLAYER);  // e
    STG(G_PLAYER);      //
    // Fade in from black (where the last round, or the boot, left it).
    PUSH(SCREEN_BRIGHTNESS_MIN); // -16
    STL(0);                      //
    LABEL(fade_in);
    LDL(0);          // level
    SYS(BRIGHTNESS); //
    LDL(0);          // level
    JZ(faded_in);    //
    LDL(0);          // level
    PUSH(FADE_STEP); // level 2
    OP(ADD);         // level+2
    STL(0);          //
    PUSH(1);         // 1
    OP(WAIT);        //
    JMP(fade_in);    //
    LABEL(faded_in);

    // Once a second: one second less on the HUD.
    LABEL(tick);
    PUSH(60);                                  // 60
    OP(WAIT);                                  //
    LDG(G_TIME);                               // time
    PUSH(1);                                   // time 1
    OP(SUB);                                   // time-1
    STG(G_TIME);                               //
    CALL(print_time_);                         //
    LDG(G_TIME);                               // time
    PUSH(ROUND_SECONDS - 3);                   // time 57
    OP(NE);                                    // time!=57
    JNZ(hint_stays);                           //
    print(HINT_COL, HINT_ROW, STR_BLANK_LINE); // the hint goes after 3 seconds
    LABEL(hint_stays);
    LDG(G_TIME);    // time
    PUSH(10);       // time 10
    OP(GT);         // time>10
    JNZ(no_tick);   //
    LDG(G_TIME);    // time
    JZ(no_tick);    //
    PUSH(SND_TICK); // sound: the last ten seconds tick
    SYS(PSG_PLAY);  //
    LABEL(no_tick);
    LDG(G_TIME); // time
    JNZ(tick);   //

    // Time is up. The Spawner and the fireflies see G_PLAYING and stop.
    PUSH(0);           // 0
    STG(G_PLAYING);    //
    SYS(MUSIC_STOP);   //
    PUSH(SND_TIME_UP); // sound
    SYS(PSG_PLAY);     //
    // The serval sits down where it stands, facing the same way: a resting
    // serval takes its place. It isn't a player (no C_PLAYER), so fireflies
    // drifting into it are no longer caught.
    LDG(G_PLAYER);      // player
    GETP(X);            // x
    LDG(G_PLAYER);      // x player
    GETP(Y);            // x y
    SPAWN(OBJ_RESTING); // rest
    LDG(G_PLAYER);      // rest player
    GETP(FLAGS);        // rest flags
    SETP(FLAGS);        //
    LDG(G_PLAYER);      // player
    OP(KILL);           //
    print(11, TIME_UP_ROW, STR_TIME_UP);
    print(10, CAUGHT_ROW, STR_CAUGHT);
    PUSH(17);               // col
    PUSH(CAUGHT_ROW);       // col row
    LDG(G_SCORE);           // col row score
    SYS(TEXT_PRINT_NUMBER); //
    PUSH(90);               // 90
    OP(WAIT);               //
    print(9, START_ROW, STR_PRESS_START);
    LABEL(wait_start);
    PUSH(1);             // 1
    OP(WAIT);            //
    PUSH(BUTTON_START);  // button
    SYS(BUTTON_PRESSED); // pressed
    JZ(wait_start);      //
    // Fade out, then ask C for a new round.
    PUSH(0); // 0
    STL(0);  //
    LABEL(fade_out);
    LDL(0);                      // level
    PUSH(FADE_STEP);             // level 2
    OP(SUB);                     // level-2
    OP(DUP);                     // level-2 level-2
    STL(0);                      // level-2
    SYS(BRIGHTNESS);             //
    PUSH(1);                     // 1
    OP(WAIT);                    //
    LDL(0);                      // level
    PUSH(SCREEN_BRIGHTNESS_MIN); // level -16
    OP(GT);                      // level>-16
    JNZ(fade_out);               //
    PUSH(1);                     // 1
    STG(G_RESTART);              // main.c restarts after this frame
    OP(HALT);                    //
}

// --- Spawner -----------------------------------------------------------------------

// Room Start, run as a second thread. Locals: 0 x, 1 y (pixels).
static void spawner(void) {
    AsmLabel loop = asm_label(), pick = asm_label(), retry = asm_label(), done = asm_label();

    HANDLER(OBJ_SPAWNER, ROOM_START);
    LABEL(loop);
    LDG(G_SPAWN_MIN);    // min
    LDG(G_SPAWN_MAX);    // min max
    SYS(RANDOM_RANGE);   // frames
    OP(WAIT);            //
    LDG(G_PLAYING);      // playing
    JZ(done);            //
    LDG(G_LIVE);         // live
    PUSH(MAX_FIREFLIES); // live 8
    OP(GE);              // live>=8
    JNZ(loop);           // enough of them: try again later
    LABEL(pick);
    PUSH(SPAWN_LEFT);   // lo
    PUSH(SPAWN_RIGHT);  // lo hi
    SYS(RANDOM_RANGE);  // x
    STL(0);             //
    PUSH(SPAWN_TOP);    // lo
    PUSH(SPAWN_BOTTOM); // lo hi
    SYS(RANDOM_RANGE);  // y
    STL(1);             //
    // Not on the serval, which would catch it at once: the distance between
    // their middles, squared, must be at least SPAWN_CLEAR squared.
    LDL(0);                          // x
    LDG(G_PLAYER);                   // x player
    GETP(X);                         // x fx
    PUSH(8);                         // x fx 8
    OP(SHR);                         // x px
    OP(SUB);                         // x-px
    PUSH(SERVAL_BODY_W / 2 - 4);     // x-px 8
    OP(SUB);                         // dx
    OP(DUP);                         // dx dx
    OP(MUL);                         // dx²
    LDL(1);                          // dx² y
    LDG(G_PLAYER);                   // dx² y player
    GETP(Y);                         // dx² y fy
    PUSH(8);                         // dx² y fy 8
    OP(SHR);                         // dx² y py
    OP(SUB);                         // dx² y-py
    PUSH(SERVAL_BODY_H / 2 - 4);     // dx² y-py 6
    OP(SUB);                         // dx² dy
    OP(DUP);                         // dx² dy dy
    OP(MUL);                         // dx² dy²
    OP(ADD);                         // d²
    PUSH(SPAWN_CLEAR * SPAWN_CLEAR); // d² 1600
    OP(LT);                          // too close
    JNZ(retry);                      //
    LDL(0);                          // x
    PUSH(8);                         // x 8
    OP(SHL);                         // fx
    LDL(1);                          // fx y
    PUSH(8);                         // fx y 8
    OP(SHL);                         // fx fy
    SPAWN(OBJ_FIREFLY);              // e
    OP(DROP);                        //
    JMP(loop);                       //
    LABEL(retry);                    // somewhere else, next frame
    PUSH(1);                         // 1
    OP(WAIT);                        //
    LDG(G_PLAYING);                  // playing
    JNZ(pick);                       //
    LABEL(done);
    OP(HALT); //
}

// --- Player ------------------------------------------------------------------------

static void player(void) {
    AsmLabel not_left = asm_label(), not_right = asm_label(), not_up = asm_label();
    AsmLabel not_down = asm_label(), standing = asm_label(), chosen = asm_label();
    AsmLabel same = asm_label();

    HANDLER(OBJ_PLAYER, CREATE);
    OP(SELF);              // e
    PUSH(SPR_SERVAL_IDLE); // e spr
    SETP(SPR);             //
    OP(SELF);              // e
    PUSH(SERVAL_BODY_W);   // e 24
    SETP(BODY_W);          //
    OP(SELF);              // e
    PUSH(SERVAL_BODY_H);   // e 20
    SETP(BODY_H);          //
    OP(SELF);              // e
    PUSH(DEPTH_SERVAL);    // e depth
    SETP(DEPTH);           //
    OP(HALT);              //

    // Step, every frame. Locals: 0 vx, 1 vy (both start at 0), 2 sprite.
    HANDLER(OBJ_PLAYER, STEP);
    PUSH(BUTTON_LEFT);   // button
    SYS(BUTTON_DOWN);    // down
    JZ(not_left);        //
    PUSH(-SPEED);        // -fx
    STL(0);              //
    OP(SELF);            // e
    OP(SELF);            // e e
    GETP(FLAGS);         // e flags
    PUSH(SPRITE_FLIP_H); // e flags flip
    OP(OR);              // e flags|flip: face left
    SETP(FLAGS);         //
    LABEL(not_left);
    PUSH(BUTTON_RIGHT);   // button
    SYS(BUTTON_DOWN);     // down
    JZ(not_right);        //
    PUSH(SPEED);          // fx
    STL(0);               //
    OP(SELF);             // e
    OP(SELF);             // e e
    GETP(FLAGS);          // e flags
    PUSH(~SPRITE_FLIP_H); // e flags ~flip
    OP(AND);              // e flags&~flip: face right
    SETP(FLAGS);          //
    LABEL(not_right);
    PUSH(BUTTON_UP);  // button
    SYS(BUTTON_DOWN); // down
    JZ(not_up);       //
    PUSH(-SPEED);     // -fx
    STL(1);           //
    LABEL(not_up);
    PUSH(BUTTON_DOWN); // button (0x80: a PUSH16)
    SYS(BUTTON_DOWN);  // down
    JZ(not_down);      //
    PUSH(SPEED);       // fx
    STL(1);            //
    LABEL(not_down);
    OP(SELF); // e
    LDL(0);   // e vx
    SETP(VX); //
    OP(SELF); // e
    LDL(1);   // e vy
    SETP(VY); //
    // Walking or standing. The sprite changes only when that changes, so
    // its animation plays (and starts from its first frame).
    LDL(0);                // vx
    LDL(1);                // vx vy
    OP(OR);                // moving
    JZ(standing);          //
    PUSH(SPR_SERVAL_WALK); // spr
    STL(2);                //
    JMP(chosen);           //
    LABEL(standing);
    PUSH(SPR_SERVAL_IDLE); // spr
    STL(2);                //
    LABEL(chosen);
    OP(SELF);    // e
    GETP(SPR);   // current
    LDL(2);      // current spr
    OP(EQ);      // same
    JNZ(same);   //
    OP(SELF);    // e
    LDL(2);      // e spr
    SETP(SPR);   //
    OP(SELF);    // e
    PUSH(0);     // e 0
    SETP(FRAME); //
    LABEL(same);
    OP(HALT); //
}

// --- Firefly -------------------------------------------------------------------------

static void firefly(AsmLabel print_score_) {
    AsmLabel wander = asm_label(), fade = asm_label(), caught = asm_label();

    // Create, then its whole life. Local 0: flights left.
    HANDLER(OBJ_FIREFLY, CREATE);
    OP(SELF);                 // e
    PUSH(8);                  // e 8
    SETP(BODY_W);             //
    OP(SELF);                 // e
    PUSH(8);                  // e 8
    SETP(BODY_H);             //
    OP(SELF);                 // e
    PUSH(DEPTH_FIREFLY);      // e depth
    SETP(DEPTH);              //
    OP(SELF);                 // e
    PUSH(0);                  // e lo
    PUSH(FIREFLY_FRAMES - 1); // e lo hi
    SYS(RANDOM_RANGE);        // e frame: blinks out of step with the others
    SETP(FRAME);              //
    LDG(G_LIVE);              // live
    PUSH(1);                  // live 1
    OP(ADD);                  // live+1
    STG(G_LIVE);              //
    PUSH(FLIGHTS_MIN);        // lo
    PUSH(FLIGHTS_MAX);        // lo hi
    SYS(RANDOM_RANGE);        // flights
    STL(0);                   //
    // From here on a catch (Collision) may cut into this handler's waits:
    // without it, the event would be dropped, since the firefly's script is
    // always waiting (one script per entity).
    PUSH(1);           // 1
    OP(INTERRUPTIBLE); //
    LABEL(wander);
    LDG(G_PLAYING);                      // playing
    JZ(fade);                            // the round is over
    LDL(0);                              // flights
    JZ(fade);                            // its time is up
    LDL(0);                              // flights
    PUSH(1);                             // flights 1
    OP(SUB);                             // flights-1
    STL(0);                              //
    OP(SELF);                            // e
    PUSH(0);                             // e lo
    PUSH(PATH_COUNT - 1);                // e lo hi
    SYS(RANDOM_RANGE);                   // e path
    PUSH(0);                             // e path lo
    PUSH(PATH_MIRROR_X | PATH_MIRROR_Y); // e path lo hi
    SYS(RANDOM_RANGE);                   // e path flags: mirrored at random
    SYS(PATH_START);                     //
    OP(WAIT_MOVE);                       // until the path ends (it slows to a stop)
    PUSH(10);                            // lo
    PUSH(40);                            // lo hi
    SYS(RANDOM_RANGE);                   // frames
    OP(WAIT);                            // hover a moment
    JMP(wander);                         //
    LABEL(fade);
    OP(SELF);               // e
    PUSH(SPR_FIREFLY_FADE); // e spr
    SETP(SPR);              //
    OP(SELF);               // e
    PUSH(0);                // e 0
    SETP(FRAME);            //
    OP(WAIT_ANIM);          // until the glow is gone
    OP(SELF);               // e
    OP(KILL);               //
    OP(HALT);               //

    // Collision: the serval (OTHER) caught it.
    HANDLER(OBJ_FIREFLY, COLLISION);
    LDG(G_SCORE);       // score
    PUSH(1);            // score 1
    OP(ADD);            // score+1
    STG(G_SCORE);       //
    CALL(print_score_); //
    PUSH(SND_CHIME);    // sound
    SYS(PSG_PLAY);      //
    // A sparkle where it was: 16x16, centered on the 8x8 firefly.
    OP(SELF);           // e
    GETP(X);            // x
    PUSH(FX(4));        // x 4
    OP(SUB);            // sx
    OP(SELF);           // sx e
    GETP(Y);            // sx y
    PUSH(FX(4));        // sx y 4
    OP(SUB);            // sx sy
    SPAWN(OBJ_SPARKLE); // spark
    OP(DROP);           //
    // The serval turns to face it: flipped (facing left) if the firefly's
    // middle is left of the serval's.
    OP(OTHER);                       // o
    OP(OTHER);                       // o o
    GETP(FLAGS);                     // o flags
    PUSH(~SPRITE_FLIP_H);            // o flags ~flip
    OP(AND);                         // o unflipped
    OP(SELF);                        // o unflipped e
    GETP(X);                         // o unflipped x
    PUSH(FX(4 - SERVAL_BODY_W / 2)); // o unflipped x -8
    OP(ADD);                         // o unflipped x-8
    OP(OTHER);                       // o unflipped x-8 o
    GETP(X);                         // o unflipped x-8 ox
    OP(LT);                          // o unflipped left
    OP(OR);                          // o flags
    SETP(FLAGS);                     //
    // Every tenth: a jingle, and fireflies come a little faster.
    LDG(G_SCORE);          // score
    PUSH(10);              // score 10
    OP(MOD);               // score%10
    JNZ(caught);           //
    PUSH(SND_JINGLE);      // sound
    SYS(PSG_PLAY);         //
    LDG(G_SPAWN_MAX);      // max
    PUSH(SPAWN_MAX_FLOOR); // max floor
    OP(LE);                // max<=floor
    JNZ(caught);           //
    LDG(G_SPAWN_MIN);      // min
    PUSH(SPAWN_MIN_STEP);  // min 5
    OP(SUB);               // min-5
    STG(G_SPAWN_MIN);      //
    LDG(G_SPAWN_MAX);      // max
    PUSH(SPAWN_MAX_STEP);  // max 12
    OP(SUB);               // max-12
    STG(G_SPAWN_MAX);      //
    LABEL(caught);
    OP(SELF); // e
    OP(KILL); // its Destroy runs in this phase
    OP(HALT); //

    // Destroy (caught or faded): one fewer alive.
    HANDLER(OBJ_FIREFLY, DESTROY);
    LDG(G_LIVE); // live
    PUSH(1);     // live 1
    OP(SUB);     // live-1
    STG(G_LIVE); //
    OP(HALT);    //
}

// --- Sparkle and the resting serval ---------------------------------------------------

static void sparkle(void) {
    HANDLER(OBJ_SPARKLE, CREATE);
    OP(SELF);                            // e
    PUSH(DEPTH_SPARKLE);                 // e depth
    SETP(DEPTH);                         //
    OP(SELF);                            // e
    PUSH(0);                             // e lo
    PUSH(SPRITE_FLIP_H | SPRITE_FLIP_V); // e lo hi
    SYS(RANDOM_RANGE);                   // e flips: no two bursts alike
    SETP(FLAGS);                         //
    OP(HALT);                            //

    // Animation End: its one-shot animation finished.
    HANDLER(OBJ_SPARKLE, ANIM_END);
    OP(SELF); // e
    OP(KILL); //
    OP(HALT); //
}

static void resting(void) {
    HANDLER(OBJ_RESTING, CREATE);
    OP(SELF);           // e
    PUSH(DEPTH_SERVAL); // e depth
    SETP(DEPTH);        //
    OP(HALT);           //
}

// --- The blob ----------------------------------------------------------------------

u32 script_build(const u8** blob, const char** error) {
    asm_begin(OBJ_COUNT, STR_COUNT, G_COUNT);
    asm_object(OBJ_PLAYER, C_POS | C_VEL | C_SPR | C_ANIM | C_BODY | C_PLAYER, SPR_SERVAL_IDLE);
    asm_object(OBJ_FIREFLY, C_POS | C_VEL | C_SPR | C_ANIM | C_BODY | C_FIREFLY, SPR_FIREFLY);
    asm_object(OBJ_SPARKLE, C_POS | C_SPR | C_ANIM, SPR_SPARKLE);
    asm_object(OBJ_RESTING, C_POS | C_SPR | C_ANIM, SPR_SERVAL_SIT);
    strings();

    AsmLabel print_score_ = asm_label(), print_time_ = asm_label();
    room(print_score_, print_time_);
    spawner();
    player();
    firefly(print_score_);
    sparkle();
    resting();
    print_score(print_score_);
    print_time(print_time_);

    u32 size = asm_end(blob);
    *error = asm_error();
    return size;
}
