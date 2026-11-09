// servle: a five-letter word-guessing game, six tries, with a bold, bouncy
// look and a serval that reacts. The whole game, its word lists included, is
// a script in the Lua subset (servle.lua); C only boots it.
//
// Rules: the answer is a five-letter word; you have six guesses, and each
// must be in the accepted-guess list (6,365 words; the answers are 1,508
// common ones among them), or it is refused and the row stays editable.
// Each letter of an entered guess is marked correct (green: right letter,
// right place), present (gold: in the word, another place) or absent
// (slate). Repeated letters by the usual two-pass rule: first every correct
// letter, then, left to right, a letter is present only while the answer
// still has a copy of it that no earlier letter has matched (answer ABBEY,
// guess BABES: present, present, correct, correct, absent). Guessing the
// answer wins; six wrong guesses lose, and the answer is shown. The
// statistics (games played, games won, the current streak of wins and the
// best one) are kept in save memory.
//
// Demonstrates:
//   - A whole game in the Lua subset, compiled at build time by
//     serval_add_script (examples/CMakeLists.txt). Two threads: Game (no
//     entity; main.c starts its room_start handler) plays the title, the
//     rounds and the reveal as plain sequential code with waits; Motion (the
//     create handler of an entity Game spawns) runs a frame of every effect
//     each frame, started by the rules through a few globals. RAM arrays
//     (the board's letters and marks, the strip's best mark per letter, the
//     statistics) and ROM arrays (the word tables, the animation curves);
//     a held-button repeat of the script's own (button_down,
//     button_pressed), and a button still held when the state changes
//     ignored until it is released
//   - Styled and formatted text from a script: the four text styles are the
//     four marks (text_set_style_color), so the alphabet strip shows each
//     letter in the color of its best mark; scripts have no strings, so a
//     letter prints with text_print_format("%c"), a statistic with
//     text_print_number in three columns (capped at 999 on screen; the save
//     keeps counting)
//   - Saves from a script: C opts in with vm_use_saves(); the script reads
//     save slot 0 into its stats array at boot and writes it after every
//     round (save_read, save_write, with a format version). A blank save,
//     one of another version or another size, starts at zero
//   - Seeding: random_seed() with the number of frames the title waited
//     when START is pressed, so every power-on picks different words;
//     random_range() picks the answer, every answer as likely
//   - A packed dictionary in ROM arrays, 2 bytes a word: the accepted
//     guesses sorted and bucketed by their first two letters (a table of
//     677 bucket starts), each word's last three letters in 15 bits, bit 15
//     set for an answer. A lookup is a binary search in one bucket; the
//     answer pick finds the k-th flagged word. tools/servle_words.py packs
//     the lists (words/answers.txt and words/guesses.txt, the source of
//     truth) into a generated block at the end of servle.lua; the whole
//     blob, about 22 KB, stays under 26,000 bytes, so the debug link can
//     break in it and hot reload it (a test checks the limit)
//   - Sprites driven by entity properties and palettes: one 32x32 picture
//     per letter, shown in four palettes (typed, absent, present, correct:
//     SPRITE_PALETTE in the flags), so 30 tiles take 30 hardware sprites; a
//     flip made of squashed frames, with a small pop from the scale property
//     (SPRITE_SCALED); tilts from the angle; depth (sys_render_by_depth)
//     lifting a moving tile above its neighbours; confetti moved by
//     sys_movement. Every sprite is in screen space (SPRITE_SCREEN), so the
//     camera, which the script drifts slowly, moves only the background: two
//     wrapping map layers of spots, the far one at half speed
//     (scroll_factor). Brightness fades from the script
//   - Music (PsgSong, bound to the script with vm_bind) and sound effects
//     with priorities: the effects on square 1, which the tune leaves free
//   - The C side's small role, what Studio Advance generates as C for a
//     game made in its editor: main() (this file), the art built at boot
//     from ASCII pictures (art.c), the sound and song tables (sound.c), the
//     numbers both sides name (game.h), vm_load, vm_bind, vm_start and the
//     frame loop. No game rule is in C
//   - The word lists come from SCOWL 2020.12.07 (Kevin Atkinson's word
//     lists, wordlist.aspell.net), curated by hand; its notice, which covers
//     these lists only, is third_party/licenses/scowl.txt. A game that
//     reuses the lists must carry it
//
// What to expect when booting the ROM:
//   - The Serval Engine splash, a moment of black while the art is built,
//     then the title fades in over dark plum with darker and lighter spots
//     drifting slowly at two speeds: "SERVLE" in six big tiles (green, gold
//     and slate, the marks' colors) that drop in one by one, tilted, bounce,
//     straighten and then wave gently; a serval's face, which blinks now and
//     then; "GUESS THE WORD IN SIX TRIES", a blinking gold "PRESS START",
//     the saved STREAK and BEST (0 and 0 on a first boot), and the controls,
//     "UP/DOWN:SPIN A:ENTER B:ERASE". A light tune plays and loops ("Spot
//     the Word", G major, about 34 seconds): a square-wave melody over a
//     soft bass on the wave channel and a quiet noise hi-hat.
//   - START (or A): a rising three-note chime; the screen cuts to black and
//     the board fades in: 5 x 6 empty slots in the middle, STREAK and BEST
//     at the left with the serval below them, WON and PLAYED at the right,
//     and the alphabet A to Z along the bottom. The cursor's cell has a gold
//     ring, with gold arrows above and below that bob. The tune goes on.
//   - UP/DOWN spin the letter in the cursor's cell to the next or previous
//     letter, wrapping (from an empty cell UP gives A and DOWN gives Z); held,
//     they repeat. Each step a short high tick, and the tile nudges 3 pixels
//     the way it spun and springs back. LEFT/RIGHT move the cursor along the
//     row (no wrap), with a blip. B clears the cursor's letter (a low,
//     falling blip), or, on an empty cell, steps left and clears that one. A
//     on a full row enters the guess; on a letter it jumps the cursor to the
//     row's first empty cell; on an empty cell it buzzes and the cell shakes.
//   - A guess that isn't in the list: the row shakes, a low buzz, and "NOT A
//     WORD" over the alphabet for a moment; the row stays as it was.
//   - An accepted guess: its tiles flip one after another, left to right,
//     about a quarter of a second each. Each flattens, takes its mark's color
//     when flattest with a note (low for absent, higher for present, higher
//     still for correct), opens out with a small pop, and its letter in the
//     alphabet takes the color of the best mark it has had. Buttons do
//     nothing until the last tile lands; then, unless the round is over, the
//     cursor is at the start of the next row.
//   - A win: the row's tiles hop in a wave, leaning back and forward; a gold
//     cheer drops in letter by letter with a bounce and then waves, over the
//     row below the word (over the fifth row after a win on the sixth): by
//     the number of guesses, "PURRFECT!", "FIERCE!", "SWIFT!", "NICE!",
//     "CLOSE!" or "PHEW!". Confetti in five colors falls over the board, a
//     rising four-note fanfare plays, the serval's eyes turn happy and it
//     hops three times, and the new STREAK flashes; WON and PLAYED count up
//     (and BEST, when the streak passes it). After a second and a half,
//     "A:PLAY AGAIN" along the bottom: A starts a new round with a new word
//     and an empty board.
//   - A loss: the answer drops in in red letters over the middle of the
//     board, a falling three-note phrase plays, the serval's ears droop and
//     its eyes turn sad; the answer is also printed along the bottom, then
//     "A:TRY ANOTHER". The streak goes back to 0; A starts a new round.
//   - The statistics are saved after every round: power off and on, and the
//     title shows the same STREAK and BEST (and the play screen the same WON
//     and PLAYED). For demos: on the title, hold L and R and press SELECT to
//     reset them to 0 ("STATS RESET", a falling tone).
//   (In mGBA's default keyboard mapping: D-pad = arrow keys, A = X, B = Z,
//   L = A, R = S, START = Enter, SELECT = Backspace.)
//
// Uses only Serval Engine's API; no third-party headers. The files:
// servle.lua (all the game's rules, its effects and its word tables, in the
// Lua subset), art.c (graphics, built at boot), sound.c, game.h (the numbers
// both sides share, and the generated servle_script.h), words/ (the word
// lists, packed into servle.lua by tools/servle_words.py). The script's
// tests: tools/servle_test.py runs it on the VM.

#include "game.h"

int main(void) {
    serval_init();
    serval_splash();
    screen_set_brightness(SCREEN_BRIGHTNESS_MIN); // black until the script fades in
    art_build();
    sprite_table_set(sprite_table, SPRITE_COUNT);
    sprite_group_load(&sprite_group);
    psg_table_set(sound_table, SOUND_COUNT);
    tileset_load(&spots_tileset);
    map_load(&spots_far_layer);
    map_load(&spots_near_layer);
    screen_set_backdrop(COLOR_RGB(43, 23, 70)); // plum, behind the spots
    text_set_color(COLOR_RGB(255, 241, 207), COLOR_RGB(23, 11, 40));
    text_set_shadow(true);
    vm_use_saves(); // the script keeps its statistics in save slot 0
    vm_bind(&(VmBindings){.psg_songs = songs, .psg_song_count = SONG_COUNT});
    vm_load(servle_script, servle_script_size);
    vm_start(OBJ_GAME, VM_EV_ROOM_START);

    for (;;) {
        frame_begin();
        vm_step(); // waits, queued events, Step reactions
        sys_movement();
        vm_events();
        sys_render_by_depth();
        frame_end();
    }
}
