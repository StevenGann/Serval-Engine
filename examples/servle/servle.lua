-- servle.lua: every rule of servle, a five-letter word-guessing game, in the
-- Lua subset (docs/lua.md). serval_add_script (examples/CMakeLists.txt)
-- compiles it with tools/svlua.py and assembles the result with tools/svm.py
-- into servle_script, the blob main.c loads. C does what fireflies' C does:
-- it boots, builds the art, binds the sounds and the song, opts in to saves
-- (vm_use_saves), starts the Game thread and runs the frame loop. Everything
-- else is here: the title, the rules, the controls, the marking, the word
-- lists and their lookup, the statistics and their save. main.c's opening
-- comment says what the game does on screen; tools/servle_test.py (CTest
-- servle_script) plays it on the VM and checks the rules.
--
-- The script ends with the word lists, packed into three tables by
-- tools/servle_words.py from examples/servle/words/answers.txt and
-- guesses.txt (the lists' source of truth) between two marker lines: edit the
-- text files and run `tools/servle_words.py pack`, never the block. Each word
-- is packed in 15 bits: the guesses are sorted and bucketed by their first two
-- letters (WORD_STARTS), and WORD_TAILS holds the last three letters of each,
-- with bit 15 set if the word may be an answer (ANSWER_STARTS counts those).
-- They are top-level arrays (no `local`), so the code above them sees them.
--
-- Names in ALL_CAPS that this file doesn't define come from the game's C
-- headers: game.h and the engine's core.h, ecs.h, save.h, screen.h and
-- sprites.h (serval_add_script's HEADERS).

-- Objects -------------------------------------------------------------------

-- Game has no components: main.c runs its room_start handler as a thread
-- (vm_start). The board's 30 tiles are entities it spawns when play starts;
-- the title's six are tiles too. Game plays the rules and turns the tiles
-- over; Motion, one more thread (the create of an entity Game spawns),
-- moves everything else that moves, every frame (see Motion, below).
Game = object {}
Tile = object { components = C_POS | C_SPR, sprite = SPR_TILE }
Glyph = object { components = C_POS | C_SPR, sprite = SPR_GLYPH } -- a letter of the cheer
Arrow = object { components = C_POS | C_SPR, sprite = SPR_ARROW } -- the cursor's two
Cursor = object { components = C_POS | C_SPR, sprite = SPR_TILE } -- its ring (TILE_CURSOR)
Confetti = object { components = C_POS | C_VEL | C_SPR, sprite = SPR_CONFETTI }
Serval = object { components = C_POS | C_SPR, sprite = SPR_SERVAL }
Motion = object { components = C_POS }

-- Globals -------------------------------------------------------------------

-- A word is packed as five letters of 5 bits each, the first highest, a
-- letter 0 to 25: (c1 << 20) | (c2 << 15) | (c3 << 10) | (c4 << 5) | c5.
answer = 0  -- the word to find; 0 means "pick one when the round starts"
row = 0     -- the guess being typed, 0 to 5
col = 0     -- the cursor's cell in it, 0 to 4
state = 0   -- STATE_TITLE, below

-- The board, a cell per tile, row by row: cell row * 5 + col + 1. A letter is
-- 1 to 26 (A to Z), 0 an empty cell; a mark is a MARK_* of game.h.
letters = array(30)
marks = array(30)
tiles = array(30)     -- the Tile entities
known = array(26)     -- the best mark each letter has had this round (the strip)
stats = array(4)      -- PLAYED, WON, STREAK, BEST: what the save slot keeps
unmatched = array(26) -- marking: copies of each letter the guess hasn't matched
title_tiles = array(6) -- the title's SERVLE
cheer = array(9)       -- the Glyphs over the board at a round's end
confetti = array(24)   -- a won round's scraps (none: not thrown, or gone)
cat = none             -- the serval
arrow_up = none        -- the cursor's arrows, above and below its cell
arrow_down = none
cursor = none          -- the gold ring around its cell

local held = 0         -- buttons down when the state began, ignored until released
local spin_dir = 0     -- Up (1), Down (-1) or neither (0) last frame
local spin_time = 0    -- frames that direction has been held
local message_time = 0 -- frames until a message on the strip's row goes

-- What Motion animates. The rules start an effect by setting these; Motion
-- counts its frames and puts what moved back where it rests at its end.
local brightness = SCREEN_BRIGHTNESS_MIN -- the screen's, faded up to 0
local title_time = 0   -- frames since the title's tiles began to drop
local nudge_tile = 0   -- the cell a spin nudges (1-30; 0: none)
local nudge_dir = 0    -- -1 up, 1 down
local nudge_time = 0
local shake_first = 0  -- the cells shaking, first to last (0: none)
local shake_last = 0
local shake_time = 0
local hop_first = 0    -- the won row's first cell (0: no hop)
local hop_time = 0
local cheer_count = 0  -- Glyphs in cheer
local cheer_palette = 0
local cheer_y = 0      -- where its glyphs rest
local cheer_time = 0
local mood = SERVAL_IDLE -- the serval's frame: idle (and blinking), happy or sad
local mood_time = 0
local flash_time = 0   -- frames the streak's number still flashes
local confetti_to_throw = 0 -- scraps still to throw, a few a frame
local confetti_live = 0     -- scraps thrown and not yet gone

-- Numbers -------------------------------------------------------------------

local STATE_TITLE <const> = 0
local STATE_PLAY <const> = 1   -- typing a guess
local STATE_REVEAL <const> = 2 -- an entered guess showing its marks: no input
local STATE_WON <const> = 3
local STATE_LOST <const> = 4

-- The statistics, in stats and in save slot 0.
local PLAYED <const> = 1
local WON <const> = 2
local STREAK <const> = 3
local BEST <const> = 4
local SAVE_VERSION <const> = 1 -- raised if stats' layout or length changes
local STAT_MAX <const> = 999   -- what a statistic's three columns can show

-- Timing, in frames.
local REPEAT_DELAY <const> = 16 -- a held Up or Down acts at once, again after this,
local REPEAT_EVERY <const> = 4  -- then every this many frames
local REVEAL_STEP <const> = 14  -- between the tiles of a guess showing their marks: a flip
local FLIP_TURN <const> = 6     -- the frame of a flip that shows the mark
local MESSAGE_TIME <const> = 80 -- "NOT A WORD" on the strip's row
local PROMPT_DELAY <const> = 90 -- from the last tile landing to A starting the next round
local BLINK <const> = 60        -- "PRESS START": shown 40 frames, hidden 20
local BLINK_OFF <const> = 40

-- The board: a tile's sprite is 32x32 with the 22x22 tile in its middle, so
-- it sits 4 pixels up and left of its 24-pixel cell.
local TILE_INSET <const> = 4

-- Every sprite is drawn on the screen, not in the world (the camera drifts
-- only to move the background), and in front of background 1, where the
-- near spots are (art.c): set_sprite_flags gives every sprite these.
local DRAW <const> = SPRITE_SCREEN | SPRITE_ABOVE_FOREGROUND

-- Which sprite is in front where they overlap (sys_render_by_depth: higher
-- in front): a tile that moves rises above its neighbours while it moves.
local DEPTH_TILE <const> = 0
local DEPTH_LIFTED <const> = 1
local DEPTH_CURSOR <const> = 2
local DEPTH_ARROW <const> = 3
local DEPTH_CHEER <const> = 4
local DEPTH_CONFETTI <const> = 5

-- The title's tiles: the 22x22 tile's top-left of the first, and the step
-- to the next (sprites at TILE_INSET + 1 up and left of it).
local TITLE_X <const> = 43
local TITLE_Y <const> = 16
local TITLE_STEP <const> = 26
-- The serval's 48x48 face: on the title, and left of the board in play.
local CAT_TITLE_X <const> = 96
local CAT_TITLE_Y <const> = 50
local CAT_PLAY_X <const> = 6
local CAT_PLAY_Y <const> = 92

-- The background: the camera moves a pixel right every DRIFT_X frames and
-- down every DRIFT_Y; the screen fades in from black by FADE_STEP a frame.
local DRIFT_X <const> = 8
local DRIFT_Y <const> = 12
local FADE_STEP <const> = 1

-- Motion, in frames and pixels (the tables of each move are with Motion,
-- below). A degree as an angle (65536 a turn).
local DEGREE <const> = ANGLE_DEG(1)
local TITLE_STAGGER <const> = 5   -- between the title's tiles starting to drop
local TITLE_WAVE <const> = 96     -- the title's wave, once up and down
local ARROW_BOB <const> = 54      -- the cursor's arrows, out and back
local ARROW_INSET <const> = 4     -- an arrow's 16-pixel sprite, centred on its cell
local HOP_STAGGER <const> = 5     -- between the won row's tiles starting to hop
local CHEER_Y <const> = 58        -- a lost round's answer, over the board's middle
local CHEER_INSET <const> = 4     -- a glyph's 16-pixel sprite, centred on a row's cells
local GLYPH_PITCH <const> = 14
local CHEER_MAX <const> = 9       -- glyphs in the longest cheer, PURRFECT!
local CHEER_STAGGER <const> = 4   -- between its glyphs starting to drop
local CHEER_WAVE <const> = 72
local WAVE_EVERY <const> = 4      -- a wave's height is worked out every this many frames
local CONFETTI_COUNT <const> = 24 -- the length of confetti
local CONFETTI_PER_FRAME <const> = 3
local CONFETTI_EVERY <const> = 4
local CONFETTI_FALL <const> = 0.2 -- added to a scrap's speed down every CONFETTI_EVERY frames
local CAT_BLINK_EVERY <const> = 170 -- the idle serval blinks this often,
local CAT_BLINK_FOR <const> = 8     -- for this long
local CAT_HOPS <const> = 3          -- a won round: the serval hops this many times
local FLASH_TIME <const> = 48       -- a won round: the streak's number flashes,
local FLASH_EVERY <const> = 4       -- on or off for this long

-- The word tables.
local BUCKETS <const> = 26 * 26 -- by the first two letters
local ANSWER_FLAG <const> = 0x8000
local TAIL_MASK <const> = 0x7FFF

-- Text, in cells (30 x 20). The play screen's statistics sit either side of
-- the board (columns 0-6 and 23-29), the title's under the serval.
local TAGLINE_ROW <const> = 13
local PRESS_ROW <const> = 15
local RESET_ROW <const> = 16
local TITLE_STATS_ROW <const> = 17
local CONTROLS_ROW <const> = 19
local STREAK_ROW <const> = 2 -- the streak's and won's numbers; best's and played's 3 below
local LEFT_NUMBER_COL <const> = 2
local RIGHT_NUMBER_COL <const> = 25
local ANSWER_COL <const> = 5 -- a lost round's strip: the answer, then the prompt
local TRY_COL <const> = 12

-- The round -----------------------------------------------------------------

function Game:room_start()
  -- The statistics: anything but an intact save of this version (none yet,
  -- another version, another size) leaves them at zero.
  if save_read(0, stats, SAVE_VERSION) ~= SAVE_OK then
    for i = 1, 4 do stats[i] = 0 end
  end
  -- The four text styles are the four marks, for the alphabet strip (the
  -- title borrows two: gold for PRESS START, the muted one for the controls).
  local shadow = COLOR_RGB(23, 11, 40)
  text_set_style_color(MARK_NONE, COLOR_RGB(239, 233, 255), shadow)
  text_set_style_color(MARK_ABSENT, COLOR_RGB(110, 100, 146), shadow)
  text_set_style_color(MARK_PRESENT, COLOR_RGB(244, 177, 46), shadow)
  text_set_style_color(MARK_CORRECT, COLOR_RGB(86, 224, 140), shadow)
  psg_music_play(SONG_THEME)
  cat = spawn(Serval, CAT_TITLE_X, CAT_TITLE_Y)
  set_sprite_flags(cat, 0)
  spawn(Motion, 0, 0)

  title()
  spawn_tiles()
  draw_play_screen()
  wait(1) -- the tiles and the screen take most of a frame's operations
  while true do
    play_round()
    finish_round()
  end
end

-- The title, until START or A. The frames it waited seed the random numbers,
-- so every power-on picks different words.
function title()
  state = STATE_TITLE
  text_clear()
  -- Motion drops them in one by one: each hidden until its turn.
  for i = 1, 6 do
    local tile = spawn(Tile, TITLE_X + (i - 1) * TITLE_STEP - TILE_INSET - 1, -CELL)
    tile.frame = TITLE_LETTERS[i]
    set_sprite_flags(tile, SPRITE_HIDDEN)
    title_tiles[i] = tile
  end
  title_time = 0
  text_print_centered(TAGLINE_ROW, "GUESS THE WORD IN SIX TRIES")
  draw_title_stats()
  text_set_style(MARK_ABSENT)
  text_print_centered(CONTROLS_ROW, "UP/DOWN:SPIN A:ENTER B:ERASE")
  text_set_style(MARK_NONE)
  block_held()
  local frames = 0
  while true do
    update_held()
    if pressed(BUTTON_START) or pressed(BUTTON_A) then break end
    -- L and R held, SELECT pressed: the statistics start again.
    if pressed(BUTTON_SELECT) and button_down(BUTTON_L) and button_down(BUTTON_R) then
      reset_stats()
    end
    if frames % BLINK == 0 then
      text_set_style(MARK_PRESENT)
      text_print_centered(PRESS_ROW, "PRESS START")
      text_set_style(MARK_NONE)
    elseif frames % BLINK == BLINK_OFF then
      text_clear_area(0, PRESS_ROW, 30, 1)
    end
    wait(1)
    frames = frames + 1
  end
  random_seed(frames)
  psg_play(SND_START)
  for i = 1, 6 do
    kill(title_tiles[i])
    title_tiles[i] = none
  end
  cat.x = CAT_PLAY_X
  cat.y = CAT_PLAY_Y
  brightness = SCREEN_BRIGHTNESS_MIN -- black at once: the board fades in (Motion)
end

-- SERVLE in the marks' colors (Motion shows each tile in its mark's palette
-- as it starts to drop).
TITLE_LETTERS = { 19, 5, 18, 22, 12, 5 }
TITLE_MARKS = { MARK_CORRECT, MARK_PRESENT, MARK_ABSENT, MARK_CORRECT, MARK_PRESENT,
                MARK_CORRECT }

function reset_stats()
  for i = 1, 4 do stats[i] = 0 end
  save_write(0, stats, SAVE_VERSION)
  text_print_centered(RESET_ROW, "STATS RESET")
  psg_play(SND_RESET)
  draw_title_stats()
end

-- A new round: the answer (unless one is already set), an empty board.
function start_round()
  if answer == 0 then
    answer = pick_answer()
    wait(1) -- the search takes most of a frame's operations
  end
  clear_round_end()
  for i = 1, 30 do
    letters[i] = 0
    marks[i] = 0
    show_tile(i)
  end
  for i = 1, 26 do known[i] = 0 end
  row = 0
  col = 0
  message_time = 0
  draw_strip()
  state = STATE_PLAY
end

-- Plays a round until it is won or lost.
function play_round()
  start_round()
  block_held()
  while true do
    update_held()
    if message_time > 0 then
      message_time = message_time - 1
      if message_time == 0 then draw_strip() end
    end
    play_input()
    if state ~= STATE_PLAY then return end
    wait(1)
  end
end

-- The round's end: the statistics, kept in the save, and A for the next.
function finish_round()
  stats[PLAYED] = stats[PLAYED] + 1
  if state == STATE_WON then
    stats[WON] = stats[WON] + 1
    stats[STREAK] = stats[STREAK] + 1
    stats[BEST] = math.max(stats[BEST], stats[STREAK])
    psg_play(SND_WIN)
    celebrate()
  else
    stats[STREAK] = 0
    psg_play(SND_LOSE)
    show_answer()
    mourn()
  end
  draw_stats()
  save_write(0, stats, SAVE_VERSION)
  -- A press is taken only once the result has had time to show.
  block_held()
  local t = 0
  repeat
    wait(1)
    t = t + 1
    update_held()
    if t == PROMPT_DELAY then
      if state == STATE_WON then
        text_print_centered(STRIP_ROW, "A:PLAY AGAIN")
      else
        text_print(TRY_COL, STRIP_ROW, "A:TRY ANOTHER")
      end
    end
  until t >= PROMPT_DELAY and pressed(BUTTON_A)
  answer = 0
end

-- Input ---------------------------------------------------------------------

-- A button down when a state begins (the A that left the title, an Up held
-- through a reveal) does nothing until it is released and pressed again:
-- block_held notes those (and forgets the last spin), update_held (every
-- frame) forgets each released one, and pressed and down read the others
-- only.
function block_held()
  held = 0
  spin_dir = 0 -- and a spin starts afresh: an Up let go in a reveal is over
  spin_time = 0
  local b = 1
  while b <= BUTTON_ANY do
    if button_down(b) then held = held | b end
    b = b << 1
  end
end

function update_held()
  if held ~= 0 then
    local b = 1
    while b <= BUTTON_ANY do
      if held & b ~= 0 and not button_down(b) then held = held & ~b end
      b = b << 1
    end
  end
end

function pressed(button)
  return held & button == 0 and button_pressed(button)
end

function down(button)
  return held & button == 0 and button_down(button)
end

-- One frame of play: Up and Down spin the cursor's letter (held, they
-- repeat), Left and Right move the cursor, B erases, A enters or moves on.
function play_input()
  local dir = 0
  if down(BUTTON_UP) then
    dir = 1
  elseif down(BUTTON_DOWN) then
    dir = -1
  end
  if dir ~= spin_dir then
    spin_dir = dir
    spin_time = 0
  elseif dir ~= 0 then
    spin_time = spin_time + 1
  end
  if dir ~= 0 and (spin_time == 0 or spin_time >= REPEAT_DELAY
                   and (spin_time - REPEAT_DELAY) % REPEAT_EVERY == 0) then
    spin(dir)
  end
  if pressed(BUTTON_LEFT) and col > 0 then
    col = col - 1
    psg_play(SND_MOVE)
  end
  if pressed(BUTTON_RIGHT) and col < 4 then
    col = col + 1
    psg_play(SND_MOVE)
  end
  if pressed(BUTTON_B) then erase() end
  if pressed(BUTTON_A) then press_a() end
end

-- The next (step 1) or previous (-1) letter, wrapping; from an empty cell Up
-- gives A and Down gives Z.
function spin(step)
  local i = row * 5 + col + 1
  local letter = letters[i]
  if letter == 0 then
    if step > 0 then letter = 1 else letter = 26 end
  else
    letter = (letter - 1 + step) % 26 + 1
  end
  letters[i] = letter
  show_tile(i)
  psg_play(SND_TICK)
  nudge(i, -step) -- Up nudges the tile up, Down down
end

-- B clears the cursor's cell; if it is already empty, the cell left of it.
function erase()
  if letters[row * 5 + col + 1] == 0 and col > 0 then col = col - 1 end
  local i = row * 5 + col + 1
  letters[i] = 0
  show_tile(i)
  psg_play(SND_BACK)
end

-- A: a full row is entered; otherwise, on a letter, the cursor goes to the
-- row's first empty cell; on an empty cell, a buzz.
function press_a()
  local first_empty = 0 -- its column + 1; 0 if the row is full
  for c = 5, 1, -1 do
    if letters[row * 5 + c] == 0 then first_empty = c end
  end
  if first_empty == 0 then
    enter_guess()
  elseif letters[row * 5 + col + 1] ~= 0 then
    col = first_empty - 1
    psg_play(SND_MOVE)
  else
    psg_play(SND_BUZZ)
    shake(row * 5 + col + 1, row * 5 + col + 1)
  end
end

-- Marking -------------------------------------------------------------------

-- A full row: refused if it isn't a word; otherwise its marks are shown tile
-- by tile, and the round is won, lost, or goes on to the next row.
function enter_guess()
  local guess = 0
  for c = 1, 5 do guess = guess << 5 | letters[row * 5 + c] - 1 end
  if not is_word(guess) then
    psg_play(SND_BUZZ)
    text_print_centered(STRIP_ROW, "NOT A WORD")
    message_time = MESSAGE_TIME
    shake(row * 5 + 1, row * 5 + 5)
    return
  end
  state = STATE_REVEAL
  message_time = 0
  mark_row()
  for c = 1, 5 do flip_tile(row * 5 + c) end -- REVEAL_STEP frames each
  if guess == answer then
    state = STATE_WON
  elseif row == 5 then
    state = STATE_LOST
  else
    row = row + 1
    col = 0
    state = STATE_PLAY
    block_held()
  end
end

-- The answer's letter at a position, 1 to 5 (a letter 1 to 26).
function answer_letter(c)
  return (answer >> (25 - 5 * c) & 31) + 1
end

-- The marks of the row, by the usual two-pass rule: first every letter in
-- its right place is correct; then, left to right, a letter is present only
-- while the answer has a copy of it that no earlier letter has matched.
function mark_row()
  local base = row * 5
  -- Every letter's count, not only the answer's: a lost round's last guess
  -- leaves its answer's unused letters counted.
  for letter = 1, 26 do unmatched[letter] = 0 end
  for c = 1, 5 do
    local a = answer_letter(c)
    if letters[base + c] == a then
      marks[base + c] = MARK_CORRECT
    else
      unmatched[a] = unmatched[a] + 1
    end
  end
  for c = 1, 5 do
    local i = base + c
    if marks[i] ~= MARK_CORRECT then
      local letter = letters[i]
      if unmatched[letter] > 0 then
        marks[i] = MARK_PRESENT
        unmatched[letter] = unmatched[letter] - 1
      else
        marks[i] = MARK_ABSENT
      end
    end
  end
end

-- Statistics ----------------------------------------------------------------

-- Every statistic on screen goes through here: at most 999, right-aligned in
-- three columns, so a big count never runs out of its place (the save keeps
-- counting). Numbers are gold (the present mark's text style).
function print_stat(c, r, value)
  text_set_style(MARK_PRESENT)
  text_print_number(c, r, math.min(value, STAT_MAX), 3)
  text_set_style(MARK_NONE)
end

function draw_title_stats()
  text_print(5, TITLE_STATS_ROW, "STREAK")
  print_stat(12, TITLE_STATS_ROW, stats[STREAK])
  text_print(17, TITLE_STATS_ROW, "BEST")
  print_stat(22, TITLE_STATS_ROW, stats[BEST])
end

function draw_stats()
  print_stat(LEFT_NUMBER_COL, STREAK_ROW, stats[STREAK])
  print_stat(LEFT_NUMBER_COL, STREAK_ROW + 3, stats[BEST])
  print_stat(RIGHT_NUMBER_COL, STREAK_ROW, stats[WON])
  print_stat(RIGHT_NUMBER_COL, STREAK_ROW + 3, stats[PLAYED])
end

-- The board and text ------------------------------------------------------------

-- The statistics' labels are dim (the absent mark's style), their numbers
-- gold, under them.
function draw_play_screen()
  text_clear()
  text_set_style(MARK_ABSENT)
  text_print(0, STREAK_ROW - 1, "STREAK")
  text_print(1, STREAK_ROW + 2, "BEST")
  text_print(25, STREAK_ROW - 1, "WON")
  text_print(23, STREAK_ROW + 2, "PLAYED")
  text_set_style(MARK_NONE)
  draw_stats()
end

-- Where a cell's tile rests: its sprite's top-left.
function tile_x(i)
  return BOARD_X + (i - 1) % 5 * CELL - TILE_INSET
end

function tile_y(i)
  return BOARD_Y + (i - 1) // 5 * CELL - TILE_INSET
end

function spawn_tiles()
  for i = 1, 30 do
    local tile = spawn(Tile, tile_x(i), tile_y(i))
    set_sprite_flags(tile, 0)
    tiles[i] = tile
  end
end

-- Every sprite's flags are written here: DRAW (on the screen, in front of
-- the near spots) and `f`, a palette, SPRITE_HIDDEN or SPRITE_SCALED.
function set_sprite_flags(e, f)
  e.flags = DRAW | f
end

-- The flags that pick sprite palette n: SPRITE_PALETTE(n) of sprites.h.
function palette(n)
  return (n + 1) << 8
end

-- A tile shows its letter (frame 0: empty) in its mark's palette (PAL_*
-- equal the marks).
function show_tile(i)
  local tile = tiles[i]
  tile.frame = letters[i]
  set_sprite_flags(tile, palette(marks[i]))
end

-- A tile turns over to show its mark, in REVEAL_STEP frames (FLIP_FRAMES):
-- it flattens (the squashed frames, still cream), takes its mark's color at
-- its flattest, with the mark's note, and opens out again a little too far
-- (FLIP_SCALES) before it settles.
function flip_tile(i)
  local tile = tiles[i]
  tile.depth = DEPTH_LIFTED
  local mark = MARK_NONE
  for step = 1, REVEAL_STEP do
    if step == FLIP_TURN then
      mark = marks[i]
      psg_play(SND_MARK_ABSENT + mark - MARK_ABSENT) -- the sounds are in the marks' order
      local letter = letters[i]
      if mark > known[letter] then known[letter] = mark end
      draw_strip()
    end
    local frame = FLIP_FRAMES[step]
    if frame == 0 then frame = letters[i] end
    tile.frame = frame
    local scale = FLIP_SCALES[step]
    if scale == 16 then
      tile.scale = 0 -- unscaled: a scale without SPRITE_SCALED warns
      set_sprite_flags(tile, palette(mark))
    else
      tile.scale = scale / 16
      set_sprite_flags(tile, palette(mark) | SPRITE_SCALED)
    end
    wait(1)
  end
  tile.depth = DEPTH_TILE
end

-- The flip, a frame per entry: the tile's frame (0: its letter) and its
-- size in 16ths. The color changes at FLIP_TURN, the flattest frame.
FLIP_FRAMES = { TILE_SQUASH, TILE_SQUASH, TILE_SQUASH + 1, TILE_SQUASH + 1, TILE_SQUASH + 2,
                TILE_SQUASH + 2, TILE_SQUASH + 1, TILE_SQUASH, 0, 0, 0, 0, 0, 0 }
FLIP_SCALES = { 16, 16, 16, 16, 16, 16, 16, 16, 18, 18, 17, 16, 16, 16 }

-- A to Z on the strip's row, each letter in the style of its best mark.
function draw_strip()
  for letter = 1, 26 do
    text_set_style(known[letter])
    text_print_format(STRIP_COL + letter - 1, STRIP_ROW, "%c", 64 + letter)
  end
  text_set_style(MARK_NONE)
end

-- A lost round: the answer on the strip's row (the prompt joins it later).
function show_answer()
  text_clear_area(0, STRIP_ROW, 30, 1)
  for c = 1, 5 do
    text_print_format(ANSWER_COL + c - 1, STRIP_ROW, "%c", 64 + answer_letter(c))
  end
end

-- Motion --------------------------------------------------------------------
--
-- One thread moves everything that moves but the flips (flip_tile, which the
-- reveal waits on): a frame of each effect, every frame, after the rules
-- (Game's thread runs first). An effect the rules haven't started costs a
-- test. The rules start them: nudge, shake, celebrate, mourn.

function Motion:create()
  arrow_up = spawn(Arrow, 0, 0)
  arrow_down = spawn(Arrow, 0, 0)
  arrow_down.frame = ARROW_DOWN
  arrow_up.depth = DEPTH_ARROW
  arrow_down.depth = DEPTH_ARROW
  cursor = spawn(Cursor, 0, 0)
  cursor.frame = TILE_CURSOR
  cursor.depth = DEPTH_CURSOR
  local t = 0
  while true do
    fade_in()
    -- The background's layers move with the camera, the far one at half its
    -- speed (art.c); every sprite is drawn on the screen, so only the spots
    -- move.
    camera_set(t // DRIFT_X, t // DRIFT_Y)
    if title_tiles[1] ~= none then drop_title() end
    bob_arrows(t)
    nudge_step()
    shake_step()
    hop_step()
    if cheer_count > 0 then drop_cheer_step() end
    if confetti_to_throw > 0 then throw_confetti() end
    if confetti_live > 0 then fall_confetti(t) end
    move_serval(t)
    if flash_time > 0 then flash_streak() end
    t = t + 1
    wait(1)
  end
end

-- The screen comes up from black: at boot (main.c starts it black), and when
-- the title gives way to the board.
function fade_in()
  if brightness < 0 then
    brightness = math.min(brightness + FADE_STEP, 0)
    screen_set_brightness(brightness)
  end
end

-- The drop: from 60 pixels above, falling past where it rests and settling,
-- a frame per entry (a CSS cubic-bezier(.3, 1.6, .5, 1), sampled), and the
-- title's tiles turning from 20 degrees to straight as they fall.
DROP_Y = { -60, -51, -43, -36, -29, -23, -18, -13, -9, -5, -2, 0, 2, 4, 5, 6, 6, 7, 7, 6, 6, 6,
           5, 5, 4, 4, 3, 3, 2, 2, 1, 1, 1, 0, 0, 0 }
DROP_TURN = { -20, -17, -14, -12, -10, -8, -6, -4, -3, -2, -1, 0, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2,
              2, 2, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0 }

-- e, `f` frames (1 on) into its drop to rest_y: then it waves for ever, 2
-- pixels up and back down every `period` frames. A wave of 2 pixels needs no
-- more than a new height every WAVE_EVERY frames, which saves the busiest
-- frames, a won round's, most of its cost.
function drop_and_wave(e, f, rest_y, period)
  if f <= #DROP_Y then
    e.y = rest_y + DROP_Y[f]
  elseif f % WAVE_EVERY == 0 then
    e.y = rest_y - 1 + fx_cos((f - #DROP_Y) % period * 65536 // period)
  end
end

-- The title: SERVLE's tiles drop in one by one, turning as they fall, then
-- wave, one after the other.
function drop_title()
  title_time = title_time + 1
  for i = 1, 6 do
    local f = title_time - (i - 1) * TITLE_STAGGER
    if f >= 1 then
      local tile = title_tiles[i]
      if f == 1 then set_sprite_flags(tile, palette(TITLE_MARKS[i])) end
      if f <= #DROP_TURN then tile.angle = DROP_TURN[f] * DEGREE end
      drop_and_wave(tile, f, TITLE_Y - TILE_INSET - 1, TITLE_WAVE)
    end
  end
end

-- The cursor: a gold ring around its cell, and gold arrows above and below
-- it that bob together and apart, while a guess is being typed. Rows are
-- only 2 pixels apart, so the arrows sit on the gap between them, over the
-- ring and the tile's edge, reaching the neighbouring rows' tiles only with
-- their dark outline, and bob in towards the letter, never over the
-- neighbours; on the top row the up arrow stops at the screen's edge.
function bob_arrows(t)
  if state ~= STATE_PLAY then
    set_sprite_flags(arrow_up, SPRITE_HIDDEN)
    set_sprite_flags(arrow_down, SPRITE_HIDDEN)
    set_sprite_flags(cursor, SPRITE_HIDDEN)
    return
  end
  local i = row * 5 + col + 1
  cursor.x = tile_x(i)
  cursor.y = tile_y(i)
  local bob = 1 - fx_cos(t % ARROW_BOB * 65536 // ARROW_BOB) -- 0 to 2 pixels and back
  local x = BOARD_X + col * CELL + ARROW_INSET
  local top = BOARD_Y + row * CELL -- the cell's top
  arrow_up.x = x
  arrow_up.y = math.max(top - 3 + bob, 0.0)
  arrow_down.x = x
  arrow_down.y = top + CELL - 5 - bob
  set_sprite_flags(arrow_up, palette(PAL_GOLD))
  set_sprite_flags(arrow_down, palette(PAL_GOLD))
  set_sprite_flags(cursor, palette(PAL_GOLD))
end

-- A tile back where it rests, straight, among the others.
function rest_tile(i)
  local tile = tiles[i]
  tile.x = tile_x(i)
  tile.y = tile_y(i)
  tile.angle = 0
  tile.depth = DEPTH_TILE
end

-- A spin nudges the tile 3 pixels the way it spun, and it springs back.
NUDGE = { 2, 3, 3, 2, 1, 0, -1, 0 }

function nudge(i, dir)
  if nudge_tile ~= 0 and nudge_tile ~= i then rest_tile(nudge_tile) end
  nudge_tile = i
  nudge_dir = dir
  nudge_time = 0
  tiles[i].depth = DEPTH_LIFTED
end

function nudge_step()
  if nudge_tile == 0 then return end
  nudge_time = nudge_time + 1
  if nudge_time > #NUDGE then
    rest_tile(nudge_tile)
    nudge_tile = 0
  else
    tiles[nudge_tile].y = tile_y(nudge_tile) + NUDGE[nudge_time] * nudge_dir
  end
end

-- A refused guess shakes its row; A on an empty cell shakes that cell.
SHAKE_X = { 0, -1, -2, -2, -2, -1, 1, 3, 2, 0, -1, -2, -1, 0, 2, 2, 1, 1 }

function shake(first, last)
  if shake_first ~= 0 then
    for i = shake_first, shake_last do rest_tile(i) end
  end
  shake_first = first
  shake_last = last
  shake_time = 0
end

function shake_step()
  if shake_first == 0 then return end
  shake_time = shake_time + 1
  for i = shake_first, shake_last do
    if shake_time > #SHAKE_X then
      tiles[i].x = tile_x(i)
    else
      tiles[i].x = tile_x(i) + SHAKE_X[shake_time]
    end
  end
  if shake_time > #SHAKE_X then shake_first = 0 end
end

-- A won round: the row's tiles hop one after the other, leaning back as they
-- rise and forward as they land, a frame per entry (pixels and degrees). The
-- serval hops the same way, upright.
HOP_Y = { 0, -2, -3, -5, -6, -7, -8, -9, -10, -10, -9, -7, -6, -4, -3, -1, 0, 0, 1, 1, 1, 1,
          0, 0, 0, 0, 0 }
HOP_TURN = { 0, -1, -3, -4, -5, -6, -7, -7, -8, -8, -7, -5, -3, -2, 0, 1, 2, 3, 4, 4, 3, 2,
             2, 1, 1, 0, 0 }

function hop_step()
  if hop_first == 0 then return end
  hop_time = hop_time + 1
  local y = tile_y(hop_first) -- the row's
  for c = 0, 4 do
    local f = hop_time - c * HOP_STAGGER
    if f >= 1 and f <= #HOP_Y then
      local tile = tiles[hop_first + c]
      tile.y = y + HOP_Y[f]
      tile.angle = HOP_TURN[f] * DEGREE
    elseif f == #HOP_Y + 1 then
      rest_tile(hop_first + c)
    end
  end
  if hop_time > 4 * HOP_STAGGER + #HOP_Y then hop_first = 0 end
end

-- The words a won round cheers, by the number of guesses it took: a row of
-- CHEER_MAX glyph frames each (letters 1-26, GLYPH_BANG '!'), 0 after the end.
CHEERS = {
  16, 21, 18, 18, 6, 5, 3, 20, GLYPH_BANG, -- PURRFECT!
  6, 9, 5, 18, 3, 5, GLYPH_BANG, 0, 0,     -- FIERCE!
  19, 23, 9, 6, 20, GLYPH_BANG, 0, 0, 0,   -- SWIFT!
  14, 9, 3, 5, GLYPH_BANG, 0, 0, 0, 0,     -- NICE!
  3, 12, 15, 19, 5, GLYPH_BANG, 0, 0, 0,   -- CLOSE!
  16, 8, 5, 23, GLYPH_BANG, 0, 0, 0, 0,    -- PHEW!
}

-- A won round: the row hops, its cheer drops in, confetti falls, the serval
-- bounces with happy eyes and the streak's number flashes.
function celebrate()
  hop_first = row * 5 + 1
  hop_time = 0
  for i = hop_first, hop_first + 4 do tiles[i].depth = DEPTH_LIFTED end
  local n = 0
  while n < CHEER_MAX and CHEERS[row * CHEER_MAX + n + 1] ~= 0 do n = n + 1 end
  for k = 1, n do spawn_glyph(k, n, CHEERS[row * CHEER_MAX + k]) end
  -- Over the empty row below the won one (the fifth row, for the sixth), so
  -- the word just found stays in sight.
  local over = row + 1
  if row == 5 then over = 4 end
  start_cheer(n, PAL_GOLD, BOARD_Y + over * CELL + CHEER_INSET)
  confetti_to_throw = CONFETTI_COUNT
  set_mood(SERVAL_HAPPY)
  flash_time = FLASH_TIME
end

-- A lost round: the answer drops in, in red, and the serval's ears droop.
function mourn()
  for k = 1, 5 do spawn_glyph(k, 5, answer_letter(k)) end
  start_cheer(5, PAL_RED, CHEER_Y)
  set_mood(SERVAL_SAD)
end

-- Glyph k of n, centred over the board, hidden until its drop begins.
function spawn_glyph(k, n, frame)
  local g = spawn(Glyph, (SCREEN_W - n * GLYPH_PITCH) // 2 + (k - 1) * GLYPH_PITCH - 1, -CELL)
  g.frame = frame
  g.depth = DEPTH_CHEER
  set_sprite_flags(g, SPRITE_HIDDEN)
  cheer[k] = g
end

function start_cheer(n, pal, y)
  cheer_count = n
  cheer_palette = pal
  cheer_y = y
  cheer_time = 0
end

-- The cheer's glyphs drop in one by one with a bounce, then wave.
function drop_cheer_step()
  cheer_time = cheer_time + 1
  local f = cheer_time -- frames into glyph k's drop
  for k = 1, cheer_count do
    if f < 1 then return end -- this one and the rest haven't started
    local g = cheer[k]
    if f == 1 then set_sprite_flags(g, palette(cheer_palette)) end
    drop_and_wave(g, f, cheer_y, CHEER_WAVE)
    f = f - CHEER_STAGGER
  end
end

-- Confetti: scraps of five colors flung over the board, a few a frame,
-- falling ever faster (sys_movement moves them by their speed), gone below
-- the screen. Each speeds up every CONFETTI_EVERY frames, a share of them
-- each frame, so 24 scraps cost a frame no more than 6 do.
function throw_confetti()
  for n = 1, math.min(CONFETTI_PER_FRAME, confetti_to_throw) do
    local c = spawn(Confetti, random_range(BOARD_X - 10, BOARD_X + 5 * CELL + 2),
                    random_range(-30, 30))
    c.frame = random_range(0, CONFETTI_FRAMES - 1)
    c.vx = random_range(-200, 200) / 256
    c.vy = random_range(0, 160) / 256
    c.depth = DEPTH_CONFETTI
    set_sprite_flags(c, 0)
    confetti[confetti_to_throw] = c
    confetti_to_throw = confetti_to_throw - 1
    confetti_live = confetti_live + 1
  end
end

function fall_confetti(t)
  local k = t % CONFETTI_EVERY + 1
  while k <= CONFETTI_COUNT do
    local c = confetti[k]
    if c ~= none then
      if c.y > SCREEN_H then
        kill(c)
        confetti[k] = none
        confetti_live = confetti_live - 1
      else
        c.vy = c.vy + CONFETTI_FALL
      end
    end
    k = k + CONFETTI_EVERY
  end
end

-- The serval: idle it blinks now and then; happy it hops; sad its ears droop.
function set_mood(m)
  mood = m
  mood_time = 0
  cat.frame = m
end

function move_serval(t)
  if mood == SERVAL_IDLE then
    if t % CAT_BLINK_EVERY < CAT_BLINK_FOR then
      cat.frame = SERVAL_BLINK
    else
      cat.frame = SERVAL_IDLE
    end
  elseif mood == SERVAL_HAPPY and mood_time < CAT_HOPS * #HOP_Y then
    cat.y = CAT_PLAY_Y + HOP_Y[mood_time % #HOP_Y + 1]
    mood_time = mood_time + 1
  end
end

-- A won round's new streak flashes on and off, and stays on.
function flash_streak()
  flash_time = flash_time - 1
  if flash_time % FLASH_EVERY == 0 then
    if flash_time // FLASH_EVERY % 2 == 0 then
      print_stat(LEFT_NUMBER_COL, STREAK_ROW, stats[STREAK])
    else
      text_clear_area(LEFT_NUMBER_COL, STREAK_ROW, 3, 1)
    end
  end
end

-- The next round: the cheer, the confetti and the serval's mood go.
function clear_round_end()
  for k = 1, cheer_count do kill(cheer[k]) end
  cheer_count = 0
  for k = 1, CONFETTI_COUNT do
    if confetti[k] ~= none then kill(confetti[k]) end
    confetti[k] = none
  end
  confetti_live = 0
  confetti_to_throw = 0
  set_mood(SERVAL_IDLE)
  cat.y = CAT_PLAY_Y
end

-- Lookup --------------------------------------------------------------------

-- The tables are at the end of the script. ANSWER_STARTS and WORD_STARTS have
-- an entry per bucket and one more, the count of all answers or guesses
-- (#WORD_TAILS is the guess count too).
local ANSWER_TOTAL <const> = BUCKETS + 1 -- ANSWER_STARTS[ANSWER_TOTAL]: the answer count

-- Is a packed word in the guesses? Its first two letters are its bucket; a
-- binary search of the bucket's tails finds the last three.
function is_word(word)
  local b = (word >> 20) * 26 + (word >> 15 & 31)
  local tail = word & TAIL_MASK
  local lo = WORD_STARTS[b + 1]
  local hi = WORD_STARTS[b + 2] - 1
  while lo <= hi do
    local mid = (lo + hi) // 2
    local v = WORD_TAILS[mid + 1] & TAIL_MASK
    if v == tail then return true end
    if v < tail then
      lo = mid + 1
    else
      hi = mid - 1
    end
  end
  return false
end

-- An answer, every one as likely as the others.
function pick_answer()
  return nth_answer(random_range(1, ANSWER_STARTS[ANSWER_TOTAL]))
end

-- The k-th answer (1 to the answer count) in the list's order, packed: the
-- bucket is the last one with fewer than k answers before it, then a count
-- of the flagged words in it.
function nth_answer(k)
  local lo = 0
  local hi = BUCKETS - 1
  while lo < hi do
    local mid = (lo + hi + 1) // 2
    if ANSWER_STARTS[mid + 1] < k then
      lo = mid
    else
      hi = mid - 1
    end
  end
  local seen = ANSWER_STARTS[lo + 1]
  for i = WORD_STARTS[lo + 1] + 1, WORD_STARTS[lo + 2] do
    local tail = WORD_TAILS[i]
    if tail & ANSWER_FLAG ~= 0 then
      seen = seen + 1
      if seen == k then return lo // 26 << 20 | lo % 26 << 15 | tail & TAIL_MASK end
    end
  end
  return 0 -- not reached: every k has its word
end

-- BEGIN GENERATED WORDS (tools/servle_words.py pack): do not edit
WORD_STARTS = {
  0, 1, 27, 45, 66, 71, 78, 99, 100, 109,
  109, 111, 153, 177, 211, 212, 232, 233, 267, 286,
  298, 313, 322, 331, 337, 339, 346, 429, 429, 429,
  429, 500, 500, 500, 502, 544, 544, 544, 599, 599,
  599, 676, 676, 676, 740, 740, 740, 808, 808, 809,
  809, 813, 813, 903, 903, 903, 903, 929, 929, 929,
  1015, 1029, 1029, 1029, 1088, 1088, 1088, 1209, 1209, 1209,
  1278, 1278, 1278, 1330, 1330, 1330, 1330, 1340, 1341, 1379,
  1379, 1379, 1379, 1440, 1440, 1440, 1444, 1499, 1499, 1499,
  1499, 1499, 1499, 1565, 1565, 1565, 1612, 1612, 1612, 1653,
  1653, 1657, 1657, 1661, 1661, 1675, 1677, 1677, 1686, 1687,
  1688, 1693, 1693, 1697, 1698, 1699, 1714, 1730, 1745, 1746,
  1754, 1756, 1767, 1771, 1776, 1777, 1786, 1787, 1807, 1809,
  1809, 1869, 1869, 1869, 1869, 1909, 1909, 1909, 1909, 1964,
  1966, 1966, 2034, 2034, 2034, 2084, 2084, 2084, 2125, 2125,
  2125, 2155, 2155, 2155, 2155, 2155, 2155, 2210, 2210, 2210,
  2210, 2240, 2240, 2240, 2246, 2271, 2271, 2271, 2308, 2308,
  2313, 2351, 2351, 2351, 2411, 2411, 2411, 2453, 2453, 2453,
  2453, 2462, 2462, 2535, 2535, 2535, 2535, 2582, 2582, 2582,
  2582, 2608, 2608, 2608, 2608, 2608, 2608, 2672, 2672, 2672,
  2672, 2672, 2672, 2699, 2699, 2699, 2699, 2713, 2713, 2715,
  2715, 2721, 2730, 2730, 2730, 2731, 2732, 2732, 2732, 2732,
  2738, 2747, 2775, 2778, 2778, 2778, 2783, 2788, 2790, 2790,
  2793, 2793, 2794, 2794, 2795, 2814, 2814, 2814, 2814, 2829,
  2829, 2829, 2829, 2843, 2843, 2843, 2843, 2843, 2843, 2862,
  2862, 2862, 2862, 2862, 2862, 2881, 2881, 2881, 2881, 2881,
  2881, 2904, 2904, 2904, 2904, 2919, 2919, 2919, 2923, 2946,
  2946, 2946, 2949, 2949, 2968, 2977, 2977, 2977, 2983, 2983,
  2983, 2989, 2990, 2990, 2990, 2993, 2993, 3063, 3063, 3063,
  3063, 3107, 3107, 3107, 3107, 3165, 3165, 3165, 3167, 3167,
  3167, 3243, 3243, 3243, 3243, 3243, 3243, 3279, 3279, 3280,
  3280, 3290, 3290, 3381, 3381, 3381, 3381, 3426, 3426, 3426,
  3426, 3484, 3484, 3484, 3484, 3484, 3485, 3563, 3563, 3563,
  3563, 3563, 3563, 3607, 3607, 3607, 3607, 3612, 3612, 3649,
  3649, 3649, 3649, 3674, 3674, 3675, 3675, 3702, 3702, 3702,
  3702, 3702, 3702, 3735, 3735, 3735, 3735, 3735, 3735, 3746,
  3746, 3746, 3746, 3749, 3749, 3757, 3765, 3776, 3783, 3784,
  3788, 3795, 3796, 3799, 3799, 3802, 3811, 3817, 3820, 3825,
  3834, 3834, 3846, 3848, 3851, 3863, 3873, 3877, 3880, 3880,
  3881, 3968, 3968, 3968, 3968, 4023, 4023, 4023, 4036, 4090,
  4090, 4090, 4133, 4133, 4133, 4197, 4197, 4197, 4251, 4255,
  4255, 4294, 4294, 4294, 4294, 4300, 4301, 4301, 4301, 4301,
  4301, 4301, 4301, 4301, 4301, 4302, 4302, 4302, 4302, 4302,
  4302, 4303, 4303, 4303, 4303, 4303, 4303, 4345, 4345, 4345,
  4345, 4345, 4345, 4413, 4413, 4413, 4413, 4499, 4499, 4499,
  4505, 4552, 4552, 4552, 4552, 4552, 4552, 4611, 4611, 4611,
  4611, 4611, 4611, 4647, 4647, 4647, 4647, 4647, 4647, 4724,
  4724, 4780, 4780, 4837, 4837, 4837, 4921, 4969, 4969, 5002,
  5058, 5081, 5115, 5171, 5253, 5258, 5259, 5259, 5372, 5408,
  5408, 5450, 5450, 5456, 5456, 5535, 5535, 5535, 5535, 5583,
  5583, 5583, 5627, 5668, 5668, 5668, 5668, 5668, 5668, 5734,
  5734, 5734, 5802, 5803, 5803, 5836, 5836, 5853, 5853, 5861,
  5861, 5861, 5861, 5861, 5862, 5862, 5862, 5862, 5863, 5863,
  5863, 5864, 5869, 5874, 5911, 5911, 5915, 5915, 5922, 5929,
  5932, 5932, 5934, 5934, 5934, 5934, 5934, 5963, 5963, 5963,
  5963, 5998, 5998, 5998, 5998, 6036, 6036, 6036, 6036, 6036,
  6036, 6056, 6056, 6056, 6056, 6056, 6056, 6056, 6056, 6056,
  6056, 6057, 6057, 6114, 6114, 6114, 6114, 6144, 6144, 6144,
  6187, 6234, 6234, 6234, 6234, 6234, 6234, 6266, 6266, 6266,
  6283, 6283, 6283, 6284, 6284, 6284, 6284, 6284, 6284, 6284,
  6284, 6284, 6284, 6289, 6289, 6289, 6289, 6289, 6289, 6289,
  6289, 6289, 6289, 6289, 6289, 6289, 6289, 6289, 6289, 6289,
  6289, 6289, 6289, 6291, 6291, 6305, 6305, 6305, 6305, 6316,
  6316, 6316, 6316, 6318, 6318, 6318, 6318, 6318, 6318, 6331,
  6331, 6331, 6331, 6331, 6331, 6337, 6337, 6337, 6337, 6337,
  6337, 6343, 6343, 6343, 6343, 6350, 6350, 6350, 6350, 6358,
  6358, 6358, 6359, 6359, 6359, 6365, 6365, 6365, 6365, 6365,
  6365, 6365, 6365, 6365, 6365, 6365, 6365,
}
WORD_TAILS = {
  11528, 64, 32842, 179, 580, 583, 33380, 33944, 1491, 4108,
  4452, 4722, 7633, 41060, 8338, 11409, 12526, 47204, 14572, 47667,
  47763, 47780, 53828, 21106, 21305, 25164, 57938, 552, 4641, 7299,
  7314, 7630, 8306, 8614, 8616, 12434, 13443, 47661, 17539, 17554,
  50435, 19587, 19725, 52689, 53860, 32964, 33267, 3095, 3203, 3217,
  3428, 4595, 8340, 8818, 12301, 12429, 45331, 12567, 14372, 47603,
  47652, 47661, 15053, 53619, 21075, 25746, 2304, 3218, 6418, 14770,
  17668, 5399, 8740, 47571, 14884, 14987, 17683, 52369, 33037, 384,
  484, 612, 676, 37299, 6289, 6404, 6702, 8548, 41382, 8658,
  8787, 8800, 11411, 11416, 11734, 14756, 47544, 14880, 50308, 36867,
  3203, 3217, 3218, 11395, 12419, 17539, 18034, 51556, 19527, 4242,
  4516, 74, 33324, 568, 612, 34444, 3217, 3531, 4583, 37427,
  38916, 6155, 6403, 6413, 6609, 8210, 41000, 41101, 41165, 41284,
  41636, 11011, 11019, 11288, 44184, 44499, 44502, 44504, 12043, 14482,
  14515, 14560, 14605, 47524, 47526, 14789, 47747, 48352, 52241, 52369,
  19922, 20832, 20882, 242, 269, 594, 33572, 33937, 1299, 34148,
  1490, 1592, 4515, 4531, 8260, 8292, 8398, 8612, 8622, 8786,
  8824, 13582, 47526, 14993, 48484, 48504, 53828, 2509, 4113, 4452,
  4531, 39051, 39057, 39268, 39480, 6739, 8548, 8562, 8576, 8580,
  8653, 8772, 10482, 43364, 10898, 13323, 13330, 13463, 46552, 13963,
  14354, 14436, 14692, 14770, 19587, 19602, 19714, 19730, 20004, 20056,
  54539, 18016, 68, 33331, 4106, 4664, 7427, 7442, 8205, 8614,
  8775, 13440, 14899, 15371, 15499, 48484, 48504, 50637, 18578, 18706,
  19601, 52600, 20498, 338, 1489, 2179, 3201, 3537, 4107, 4114,
  4160, 37280, 6155, 6411, 6603, 6605, 6611, 39556, 8210, 8331,
  8562, 41540, 12419, 12435, 45521, 14595, 47488, 47684, 17426, 50200,
  17682, 50646, 18706, 18893, 19595, 20056, 20882, 2515, 2706, 3330,
  7299, 7309, 40082, 41060, 10371, 10385, 10390, 15501, 15505, 15618,
  18440, 18456, 18578, 51347, 19601, 19729, 8563, 44050, 12301, 14699,
  14738, 14744, 14756, 14776, 17664, 17679, 19473, 52482, 36110, 36115,
  6289, 6387, 6801, 11522, 11730, 13938, 17419, 17425, 17426, 17666,
  18060, 19922, 23821, 33035, 595, 4530, 4658, 4659, 8205, 8653,
  47363, 15058, 33043, 33092, 33315, 33316, 583, 38539, 8614, 13443,
  47428, 8203, 8562, 8614, 8652, 11410, 14770, 242, 8626, 8292,
  8612, 14594, 14692, 14948, 14951, 53796, 131, 1042, 1163, 1170,
  1675, 2136, 2386, 35277, 36036, 36216, 4466, 6283, 39128, 7794,
  8562, 8749, 8818, 8992, 8996, 10371, 43153, 10386, 11282, 11378,
  11384, 11395, 11409, 11410, 11602, 11608, 11634, 11640, 11666, 44440,
  11840, 13323, 13426, 13432, 13458, 13522, 46382, 13650, 13746, 17458,
  17508, 17522, 17539, 17553, 17554, 17586, 50372, 17666, 17746, 17810,
  17816, 17842, 50637, 17956, 18180, 18443, 18563, 18577, 18578, 51458,
  51467, 51469, 51474, 18770, 19022, 19044, 52295, 19587, 19602, 52452,
  19698, 19722, 19917, 20088, 20842, 22898, 24707, 57812, 25617, 32839,
  114, 120, 338, 402, 408, 430, 434, 33315, 562, 33363,
  626, 658, 659, 1487, 2386, 3211, 3222, 3340, 4167, 4274,
  37048, 4594, 4658, 4664, 4722, 5395, 5574, 38925, 6163, 6291,
  39181, 6611, 6796, 39565, 41156, 41382, 11288, 11335, 11456, 11524,
  11620, 11634, 44408, 44502, 11890, 12306, 46151, 13426, 13432, 13732,
  13752, 13938, 50323, 17618, 17746, 17810, 50744, 18023, 18187, 18579,
  18892, 18899, 19058, 19474, 19595, 19698, 20064, 21643, 22200, 25739,
  296, 422, 376, 34148, 2191, 3192, 3218, 3219, 4451, 4658,
  5298, 5379, 6387, 6611, 9684, 10371, 10385, 10386, 11310, 11460,
  11602, 11634, 11640, 12295, 13323, 13426, 13458, 13508, 46286, 14724,
  14944, 15491, 15811, 50247, 17522, 17778, 17970, 50791, 18578, 51661,
  19601, 19602, 20082, 20088, 50, 32842, 32868, 210, 242, 269,
  33156, 33187, 33194, 33316, 33363, 612, 626, 33572, 36874, 4113,
  36883, 4146, 36995, 4239, 37283, 37458, 41359, 41379, 8614, 8616,
  41386, 8690, 41554, 41593, 14355, 14386, 47178, 14418, 14546, 14660,
  47523, 47555, 47564, 14799, 14962, 47821, 15058, 15064, 20611, 20625,
  20626, 20627, 20632, 20645, 53683, 21025, 21042, 21043, 53831, 33315,
  562, 563, 33363, 626, 1080, 3203, 3218, 3268, 5294, 6296,
  6360, 6404, 6500, 39570, 8562, 11282, 11410, 11634, 11730, 11890,
  11922, 12324, 12338, 13380, 13426, 13443, 13458, 13518, 13522, 13732,
  13752, 46738, 14116, 14467, 14674, 14738, 14770, 14898, 47699, 47719,
  14962, 47908, 15160, 17418, 17431, 50307, 17553, 17554, 17666, 50596,
  17869, 18776, 18892, 18893, 19032, 19527, 19595, 19704, 20679, 20836,
  53667, 21030, 21060, 21106, 21763, 22659, 55435, 22673, 22898, 23108,
  23683, 56465, 56466, 24593, 24928, 26066, 32836, 71, 83, 114,
  146, 210, 33027, 267, 33037, 33092, 33187, 435, 583, 594,
  626, 33444, 686, 715, 717, 760, 786, 804, 36867, 36874,
  4108, 4196, 36995, 4531, 4772, 4818, 8209, 8228, 41034, 41060,
  41093, 8337, 8402, 8555, 8594, 8612, 41382, 41386, 8632, 41546,
  8818, 47107, 14407, 14410, 14603, 47428, 14754, 14787, 47562, 47564,
  14916, 47719, 47821, 15058, 20749, 20755, 20868, 20915, 53831, 53860,
  1035, 1056, 2382, 2386, 35960, 36036, 4526, 5294, 5298, 39128,
  39268, 7538, 41315, 41331, 11314, 44228, 11480, 11602, 44376, 11616,
  11634, 44408, 12775, 12786, 45560, 46151, 13390, 13522, 13650, 46520,
  13938, 15122, 17421, 17458, 17555, 17607, 17618, 17677, 17728, 17732,
  17778, 17784, 17842, 50611, 17906, 17920, 17966, 17970, 17976, 17984,
  17988, 50771, 18488, 18563, 18578, 51448, 18770, 19058, 19064, 19527,
  19598, 20068, 20082, 20088, 20115, 20235, 24012, 57489, 416, 11286,
  17554, 19602, 22552, 1035, 1080, 1169, 34061, 34148, 1473, 2062,
  2276, 2664, 3219, 3268, 3620, 5298, 6275, 6289, 6290, 6296,
  8739, 8749, 10371, 10386, 11602, 11616, 11634, 11666, 11940, 12055,
  45195, 45198, 12782, 12786, 12792, 46091, 46200, 13443, 13457, 13458,
  13728, 13752, 46532, 13773, 13902, 13907, 13934, 13938, 13944, 15491,
  48273, 15506, 15821, 15826, 16019, 50195, 17458, 17522, 17539, 17553,
  17554, 17555, 50382, 17746, 17848, 17857, 50635, 17868, 17896, 17906,
  50744, 17988, 18020, 18034, 50852, 18563, 18578, 18770, 19044, 19058,
  52295, 19601, 20088, 20576, 20842, 20850, 53828, 21635, 21649, 21650,
  21764, 21771, 22659, 33348, 2059, 2700, 3089, 3203, 3217, 3218,
  3346, 8224, 11393, 11616, 11630, 11634, 11724, 13892, 13934, 13938,
  14891, 17539, 17554, 17664, 17666, 17874, 18034, 18449, 19040, 164,
  165, 33037, 33041, 33130, 33167, 33203, 33234, 484, 498, 547,
  548, 33324, 561, 562, 33331, 568, 33348, 588, 626, 722,
  36879, 36883, 36938, 37002, 4239, 37009, 4274, 4448, 4494, 4659,
  37458, 37459, 4792, 4818, 37592, 8206, 41034, 8270, 8292, 41093,
  41315, 41320, 41323, 8577, 8580, 41359, 41376, 8612, 8614, 8622,
  8626, 8690, 8746, 8748, 41519, 8753, 8818, 8868, 8888, 14410,
  14418, 14609, 47428, 14696, 14735, 14794, 14834, 47651, 47652, 47684,
  15058, 20530, 53322, 20640, 20645, 20690, 20879, 20882, 53674, 21035,
  21037, 21041, 53860, 24932, 24964, 466, 3217, 38929, 11520, 12439,
  13383, 17472, 17960, 18510, 19587, 19602, 21651, 54530, 54539, 74,
  100, 33036, 33167, 402, 422, 426, 434, 498, 558, 568,
  33351, 33359, 33362, 676, 680, 722, 786, 36877, 36881, 4115,
  4234, 4274, 4275, 4580, 4650, 4818, 41034, 41125, 41345, 8580,
  8612, 41382, 8618, 8690, 14346, 47178, 14450, 14546, 14721, 14735,
  47524, 14762, 14834, 47684, 47719, 14962, 47747, 14995, 47780, 47821,
  15122, 20530, 20554, 20611, 20626, 20879, 53670, 20906, 32839, 83,
  370, 376, 33363, 616, 626, 1280, 1380, 1568, 2120, 2392,
  35264, 2514, 3090, 3203, 3217, 3218, 3223, 3533, 4210, 6605,
  7634, 8370, 8397, 8562, 8626, 8754, 10371, 10386, 11282, 11378,
  11410, 11416, 11522, 11640, 11725, 44497, 11890, 12064, 12306, 12324,
  12334, 12338, 12433, 12434, 45203, 12472, 45314, 45440, 12782, 12786,
  13383, 13422, 13443, 13458, 13504, 13570, 13650, 13746, 13924, 13934,
  14467, 14468, 14674, 14706, 14721, 14834, 14962, 15371, 15384, 15491,
  15505, 15506, 15904, 15940, 50187, 17522, 17539, 17553, 17554, 17608,
  17664, 17746, 17752, 17810, 17842, 17844, 50616, 17906, 17988, 18579,
  19040, 19044, 19058, 19602, 20064, 53319, 53447, 53603, 53683, 20964,
  20978, 53811, 21095, 21645, 54417, 21650, 54419, 21656, 21773, 22659,
  22673, 22898, 23556, 23683, 23698, 24721, 24952, 25076, 25741, 11,
  50, 32842, 32947, 210, 324, 33167, 402, 33188, 33194, 484,
  33351, 594, 33380, 33444, 33483, 722, 804, 33592, 36874, 36876,
  4206, 36995, 37002, 4235, 37007, 4242, 4484, 4580, 37363, 4690,
  37459, 4818, 8242, 8266, 8323, 8337, 41106, 41348, 8591, 8612,
  41551, 47114, 14410, 14515, 14756, 14762, 14776, 47562, 14797, 14834,
  14884, 47698, 14991, 47811, 47821, 15058, 15140, 20554, 53348, 53387,
  20627, 20865, 20879, 21060, 53831, 53843, 23143, 25075, 1080, 1153,
  1155, 1169, 1170, 34050, 1299, 3192, 5298, 8614, 10386, 11335,
  11411, 11415, 11634, 11640, 11666, 11744, 11890, 12557, 15499, 15619,
  15840, 17458, 17479, 17522, 17528, 17539, 17553, 17554, 17555, 17664,
  17668, 17678, 17778, 50552, 17976, 50756, 50852, 18104, 18562, 18680,
  18770, 18930, 19527, 19601, 19608, 19716, 19725, 19730, 20088, 20111,
  2051, 35172, 11543, 12292, 12305, 12306, 12434, 13570, 19058, 19917,
  562, 2194, 2272, 3090, 35960, 4768, 5304, 7538, 41336, 41528,
  41560, 11410, 11640, 12301, 12305, 12434, 12722, 12786, 46148, 46200,
  13522, 13582, 17539, 17553, 17554, 17666, 17842, 18034, 19587, 19601,
  19602, 20108, 20516, 20530, 20915, 21779, 22962, 25731, 25746, 370,
  33139, 434, 562, 568, 33383, 1030, 1041, 1299, 1650, 34438,
  1682, 34451, 2053, 2059, 34840, 2386, 2513, 2520, 2616, 3083,
  4210, 4498, 4594, 38033, 5574, 6162, 8260, 8376, 8397, 8780,
  8787, 41592, 10574, 44056, 11443, 11538, 11634, 44640, 11940, 12563,
  12737, 12749, 12754, 12945, 13329, 13580, 46660, 13938, 15827, 48743,
  17464, 17792, 17976, 18583, 18770, 19601, 19927, 20548, 54539, 22541,
  14376, 14692, 14952, 15058, 370, 568, 814, 2179, 2193, 2194,
  2200, 2515, 2656, 3667, 4722, 6387, 39187, 10371, 10386, 11634,
  11640, 12433, 12434, 45432, 13329, 13443, 46225, 13458, 13518, 13522,
  13528, 13656, 13938, 14436, 15864, 15950, 17553, 17604, 17746, 50808,
  18510, 18514, 18680, 18770, 52295, 20056, 20078, 20088, 21517, 21522,
  21635, 54417, 21650, 21971, 22200, 22541, 23812, 25741, 59192, 1080,
  1376, 1568, 1582, 2386, 36036, 3288, 3538, 4658, 5298, 6290,
  6350, 6360, 6404, 6528, 8568, 41382, 11332, 11395, 11410, 11634,
  44408, 11729, 11890, 12419, 12434, 13330, 13444, 13504, 13728, 13777,
  14738, 14898, 15160, 15491, 15505, 15506, 15512, 17810, 18040, 18563,
  18578, 19587, 19601, 19602, 20088, 53299, 20548, 53447, 20864, 21024,
  21060, 21650, 22648, 22667, 22673, 22788, 22962, 55736, 23096, 23108,
  24717, 25731, 25741, 25745, 25746, 50, 165, 32947, 210, 33037,
  324, 33152, 402, 33194, 484, 715, 33485, 722, 786, 36867,
  36876, 4113, 4306, 37458, 8323, 8337, 41106, 41139, 41323, 8568,
  41386, 8690, 41636, 14611, 14699, 47524, 14795, 14799, 14834, 14930,
  47780, 47821, 20530, 20690, 20739, 20882, 20906, 20964, 21060, 24579,
  57489, 57720, 114, 370, 2059, 2067, 2296, 2386, 2392, 2674,
  3203, 3218, 4466, 4722, 5298, 10386, 11520, 11634, 11640, 11844,
  12334, 45464, 12786, 45560, 13380, 13458, 13522, 13650, 13742, 14734,
  15491, 15505, 15506, 15716, 15864, 17952, 18003, 18060, 51544, 19058,
  51832, 19527, 21651, 549, 4225, 4459, 4467, 114, 4658, 41382,
  13458, 39057, 39268, 6692, 17539, 17778, 50552, 17842, 50791, 18563,
  51339, 18578, 52365, 19601, 21650, 1155, 14776, 4480, 6275, 6289,
  6290, 8275, 8376, 8818, 20548, 20563, 17668, 5251, 114, 4691,
  6275, 6289, 17555, 3217, 3538, 39155, 10701, 36947, 8614, 419,
  612, 34262, 35985, 36947, 4312, 4488, 5389, 8292, 41572, 14820,
  20580, 21092, 21649, 54418, 267, 1048, 1155, 1169, 1494, 1682,
  2180, 4515, 4664, 8754, 8818, 12433, 12435, 14632, 14948, 48760,
  32851, 612, 3203, 36310, 3716, 4480, 37272, 42456, 13960, 17867,
  19076, 52369, 52792, 21960, 21976, 18701, 83, 7175, 7619, 7633,
  8274, 14407, 14436, 15096, 53259, 53519, 33348, 36947, 6611, 13458,
  47204, 14916, 17539, 50641, 20563, 20686, 53747, 10385, 51224, 19601,
  19919, 7313, 7426, 7634, 7947, 13330, 17874, 100, 4530, 37299,
  4659, 37432, 8275, 8562, 8804, 14660, 4658, 32851, 371, 402,
  2187, 4115, 4178, 37427, 41316, 8612, 41555, 8818, 14770, 15379,
  15499, 15826, 19915, 52768, 20580, 20851, 21025, 17426, 17681, 34148,
  2179, 2193, 2194, 34963, 2674, 3192, 3203, 3217, 3218, 3268,
  4512, 5298, 8562, 41395, 8754, 41528, 41575, 10371, 10385, 10386,
  10513, 11634, 44612, 12419, 46168, 13518, 13522, 13773, 16657, 17411,
  50244, 17480, 17496, 17539, 17553, 17554, 17810, 19058, 52235, 19587,
  19602, 19832, 20088, 20160, 20679, 53619, 53664, 20914, 21156, 21650,
  54737, 22162, 22962, 23683, 23698, 24721, 25124, 25731, 25746, 562,
  595, 626, 2059, 2323, 4210, 4466, 4900, 8397, 8627, 11523,
  11616, 11634, 11640, 11725, 11890, 12676, 12945, 46148, 13426, 13752,
  14501, 17419, 17664, 17800, 17842, 17848, 17976, 19058, 19467, 19527,
  19587, 19715, 19921, 52882, 20497, 20594, 54417, 55441, 24952, 626,
  33937, 1572, 2276, 2292, 2706, 3268, 4274, 37219, 4515, 37432,
  5265, 5266, 38503, 38520, 39155, 11281, 11335, 11395, 11409, 11410,
  44179, 11634, 11640, 11666, 11672, 44647, 11916, 46091, 46151, 13426,
  13443, 46225, 13458, 13586, 13650, 13752, 17539, 17553, 17554, 17810,
  17976, 50771, 18023, 18514, 51448, 19058, 19527, 19832, 21649, 21650,
  23683, 23697, 56466, 59192, 4451, 14883, 74, 210, 267, 273,
  33092, 33112, 33156, 426, 434, 498, 33316, 33351, 586, 626,
  722, 728, 786, 4108, 4114, 4170, 4241, 4242, 37011, 37447,
  4818, 41034, 8323, 8337, 41106, 41382, 8627, 8690, 8755, 8804,
  8818, 47123, 47178, 14418, 14482, 14546, 14758, 47555, 47569, 14834,
  47648, 14904, 14930, 47761, 14995, 47821, 15058, 20530, 20626, 53413,
  53507, 20804, 20824, 20868, 20879, 53670, 20906, 20945, 53831, 53860,
  21112, 24632, 24721, 25188, 370, 402, 408, 34827, 35474, 4333,
  6296, 6360, 8562, 8787, 11378, 11520, 11522, 11524, 11534, 11602,
  11640, 13938, 14450, 14706, 14962, 14968, 17432, 17464, 50244, 17518,
  17522, 17554, 50372, 17614, 17746, 17796, 17810, 18020, 50791, 18034,
  50808, 50828, 19008, 19012, 20850, 53667, 20915, 21025, 21042, 21632,
  22898, 23683, 56466, 24721, 74, 267, 33156, 418, 33194, 498,
  626, 33411, 786, 36874, 4227, 4241, 4242, 4483, 4512, 37447,
  4722, 8209, 41091, 41106, 8555, 8772, 8778, 8807, 8825, 8996,
  9017, 14410, 14546, 14755, 14770, 47539, 14884, 14919, 47699, 14951,
  47821, 47908, 53523, 20879, 24721, 1624, 36036, 4466, 6155, 6360,
  6788, 11634, 44408, 12419, 12434, 13426, 13512, 13518, 13650, 13656,
  46520, 17421, 17778, 17873, 50744, 18212, 18232, 18563, 18564, 18578,
  18699, 19032, 19064, 19917, 59192, 1080, 34148, 3331, 5284, 41336,
  8626, 8818, 11271, 11282, 11392, 11410, 11634, 11727, 12320, 12419,
  12433, 12434, 12546, 12557, 45440, 12696, 12786, 12947, 13445, 13522,
  13600, 13765, 15491, 15506, 15864, 17458, 18023, 51346, 18930, 19032,
  19587, 19601, 19602, 19921, 20594, 20600, 53444, 20915, 21042, 21074,
  21284, 21304, 21643, 22866, 22872, 23026, 24721, 25731, 25745, 25746,
  434, 562, 2382, 4434, 4440, 37444, 4691, 11378, 11523, 12672,
  12696, 12755, 13458, 13459, 13570, 13572, 13576, 13583, 13760, 13860,
  13870, 13938, 13970, 14436, 14595, 17415, 17810, 17816, 19022, 20111,
  626, 808, 4242, 47699, 14987, 24939, 33203, 1155, 1169, 1170,
  3192, 5746, 6611, 6788, 11378, 11634, 11890, 12427, 12676, 15821,
  17522, 17778, 17784, 17874, 50791, 18034, 19602, 54413, 21649, 21650,
  25998, 32868, 114, 273, 419, 434, 33316, 568, 33362, 33572,
  36876, 4109, 4132, 4196, 4243, 4530, 8210, 41060, 8627, 8825,
  14348, 14355, 47140, 14386, 14738, 47564, 47672, 47698, 47780, 15058,
  15140, 20611, 20626, 20632, 20868, 20941, 21106, 25063, 555, 583,
  626, 722, 47492, 114, 370, 626, 3448, 4658, 5265, 41382,
  11378, 11404, 11442, 11640, 13315, 13457, 13522, 13573, 13728, 14126,
  14450, 14456, 14488, 14514, 47288, 14770, 47684, 14936, 15370, 17419,
  17539, 17554, 50372, 17988, 19698, 20064, 20676, 21027, 21112, 22541,
  22962, 50, 32836, 32868, 114, 32947, 267, 33037, 402, 33187,
  434, 33203, 33252, 33255, 33359, 33362, 33380, 33444, 33464, 786,
  33572, 36883, 4132, 36995, 37005, 37011, 4882, 8292, 8306, 41093,
  8371, 41323, 41348, 41368, 41379, 8626, 41444, 8690, 8787, 8807,
  8818, 47117, 14355, 14605, 47564, 14820, 47698, 14937, 14962, 47759,
  14995, 47780, 47819, 47821, 15058, 20530, 20619, 20645, 20868, 20879,
  53683, 78, 430, 434, 33315, 562, 672, 2386, 37458, 37459,
  41060, 41315, 8548, 41331, 41540, 11270, 11282, 11335, 11410, 11442,
  11634, 11640, 11762, 12334, 12672, 12696, 13508, 13528, 13656, 13752,
  15864, 17604, 18066, 18680, 19032, 19054, 19058, 19064, 20056, 20064,
  20088, 24707, 25043, 1155, 1170, 17419, 17554, 17869, 17874, 18066,
  21635, 21650, 34067, 2186, 2386, 3218, 3667, 5401, 5746, 8532,
  8562, 8754, 41528, 9362, 9512, 10386, 10508, 11275, 11395, 11409,
  11410, 11634, 11666, 11725, 11730, 11890, 11940, 12299, 12434, 12696,
  13088, 13380, 13426, 46200, 13522, 13650, 13656, 15736, 48632, 17522,
  50296, 17539, 17548, 17554, 17746, 17810, 17906, 17912, 17976, 50759,
  18034, 18930, 51812, 19064, 52295, 19587, 19601, 19602, 20679, 20844,
  20850, 20915, 54413, 21649, 21650, 21954, 22659, 22866, 23108, 24707,
  25613, 25731, 58507, 25745, 25746, 114, 120, 370, 498, 33315,
  562, 33331, 615, 626, 676, 33464, 3217, 36036, 4210, 4466,
  5746, 5752, 8754, 8787, 11543, 44398, 11634, 11666, 11731, 11762,
  11940, 12299, 12546, 12557, 46148, 13508, 13728, 13880, 17458, 17464,
  17522, 17554, 17869, 18041, 19698, 22659, 22673, 23555, 23683, 23697,
  23698, 24331, 2386, 3203, 3217, 3218, 6386, 6387, 9217, 10371,
  43153, 10386, 11634, 44408, 11890, 11916, 13426, 46276, 13752, 13938,
  15854, 15864, 17539, 17553, 17554, 19527, 21635, 21650, 33315, 562,
  568, 33848, 1490, 2386, 2706, 4658, 6157, 8266, 8787, 10371,
  10386, 10392, 10580, 10892, 11378, 11395, 11410, 11416, 44408, 12419,
  12433, 12434, 12440, 13443, 13457, 13458, 46232, 13650, 46545, 14407,
  14450, 14488, 14514, 14674, 14680, 14834, 14962, 15491, 15506, 17415,
  17419, 17426, 50276, 17796, 17842, 50756, 18003, 18563, 18578, 19040,
  19058, 52363, 52600, 20679, 53667, 21032, 21042, 53828, 21643, 54417,
  22648, 22898, 1080, 5298, 38072, 6289, 11282, 11602, 11608, 11634,
  45069, 12546, 45315, 45521, 12775, 12786, 12792, 12946, 46151, 13650,
  13938, 17522, 17778, 50744, 18003, 18034, 18770, 51544, 19527, 3616,
  3630, 4512, 8614, 12429, 12722, 14595, 15491, 15505, 15506, 15584,
  15826, 17431, 18893, 12328, 12338, 7633, 8337, 8568, 41382, 14770,
  20114, 36875, 4114, 41420, 8659, 11395, 11409, 11410, 14706, 24939,
  11726, 17420, 4098, 4107, 4748, 4754, 8194, 8844, 32964, 206,
  402, 1668, 8292, 8612, 12567, 15499, 48504, 420, 499, 1495,
  2705, 2706, 35991, 3332, 4595, 4659, 5265, 5399, 5664, 6500,
  6611, 8653, 10371, 10596, 11288, 11411, 46225, 13587, 48787, 18579,
  19595, 52369, 19730, 21028, 21037, 3330, 13570, 19474, 100, 612,
  10371, 14770, 14776, 11410, 11411, 18568, 51844, 19812, 2296, 4498,
  8323, 8338, 47672, 19812, 547, 1491, 2059, 2386, 3203, 3218,
  6360, 8562, 10386, 11279, 12338, 12696, 15373, 15491, 15505, 15506,
  19922, 53683, 22659, 26424, 434, 1163, 4594, 4658, 11634, 44408,
  12696, 13752, 17667, 17746, 17752, 17976, 19058, 20088, 55435, 1155,
  1170, 5298, 38072, 7171, 11890, 12696, 13518, 13650, 13736, 21635,
  21649, 21650, 21656, 2386, 4882, 7602, 8626, 41395, 8787, 10371,
  10385, 10386, 10392, 44408, 11890, 11896, 18060, 20836, 21075, 22898,
  22904, 24707, 3268, 6155, 6796, 41028, 41048, 9874, 11407, 12334,
  12786, 12792, 13390, 13650, 13920, 15821, 17419, 17427, 17547, 17873,
  20088, 8202, 8562, 10514, 11525, 11538, 11744, 13608, 14132, 14770,
  15602, 15818, 15840, 16019, 17427, 17792, 18003, 18034, 18656, 21032,
  21522, 57354, 25042, 26062, 1025, 3268, 4466, 4530, 4594, 11640,
  13317, 13422, 13924, 15634, 17458, 17828, 19527, 21643, 24707, 33096,
  434, 4192, 14988, 422, 1170, 2386, 2392, 3182, 11634, 11698,
  11730, 11890, 13330, 13408, 13426, 13458, 13522, 13581, 13650, 13656,
  13778, 14922, 19587, 19602, 52856, 22802, 14758, 14789, 21113, 32842,
  498, 676, 4099, 4227, 37003, 4242, 4459, 37235, 41124, 8775,
  8818, 47178, 14699, 14962, 14995, 47821, 15058, 21035, 33120, 1490,
  7538, 8612, 11282, 15652, 17792, 18578, 19922, 11, 179, 275,
  8555, 14756, 14797, 3538, 3730, 3892, 10792, 11274, 18066, 594,
  626, 11410, 11543, 33931, 1280, 34257, 2179, 2193, 2194, 2386,
  3203, 35981, 3218, 36196, 6157, 39057, 8739, 8754, 8824, 10385,
  10386, 10482, 12306, 12338, 12419, 12433, 12434, 12544, 12786, 13320,
  46148, 13426, 13458, 46424, 15499, 15629, 48708, 17479, 17522, 17528,
  17554, 50372, 17614, 17746, 18060, 50848, 51345, 19022, 19058, 52295,
  19587, 52369, 52375, 19684, 19688, 19698, 19704, 19780, 20068, 20594,
  53447, 21522, 21635, 21649, 21650, 22962, 23697, 23928, 57489, 25231,
  25617, 25731, 25746, 71, 114, 178, 32952, 338, 33112, 434,
  498, 33267, 33325, 33348, 33351, 33363, 33444, 36036, 4167, 4434,
  4658, 4664, 5746, 5752, 38923, 6290, 6360, 6419, 12301, 12672,
  12676, 45517, 12945, 13426, 13586, 13934, 14756, 15505, 15968, 19684,
  20111, 21636, 54411, 54417, 21773, 22802, 23826, 416, 547, 562,
  1163, 1169, 1568, 2323, 2386, 3538, 4292, 4530, 5265, 5746,
  6157, 6289, 39155, 10371, 10381, 10385, 10386, 44034, 11730, 11890,
  45102, 12338, 12419, 12429, 12434, 45331, 12722, 12754, 12786, 13432,
  13443, 46221, 46225, 13458, 46286, 13522, 13581, 13650, 13944, 14770,
  15619, 15864, 18788, 18930, 19058, 19474, 52369, 19684, 20004, 21635,
  21645, 54417, 21650, 54531, 22052, 33152, 430, 71, 114, 178,
  402, 408, 434, 615, 1041, 33848, 1155, 1170, 34827, 2386,
  2514, 2700, 2706, 3213, 3218, 36036, 4690, 5746, 38520, 6157,
  6290, 6360, 6400, 39170, 6413, 6605, 6610, 8626, 11634, 11640,
  13457, 13508, 13522, 14392, 14674, 14738, 14770, 47544, 14834, 14840,
  47684, 14962, 15491, 15505, 15506, 15864, 17421, 17522, 17682, 17976,
  18571, 51345, 18578, 19474, 19714, 20078, 52882, 20679, 20754, 20964,
  21042, 21048, 53828, 21080, 21106, 21635, 54417, 21650, 21656, 22659,
  55441, 55672, 57355, 658, 1155, 1170, 2307, 2386, 35160, 2596,
  3330, 5280, 5298, 6275, 6290, 11634, 11922, 12429, 12786, 45560,
  46097, 46151, 13508, 13512, 13522, 15629, 16018, 50247, 17539, 17554,
  17559, 17624, 17667, 17746, 18680, 19058, 19064, 19602, 23698, 4370,
  41382, 12775, 17554, 50434, 18578, 18701, 18706, 19008, 19714, 20064,
  2070, 2179, 2194, 2286, 2404, 2606, 35852, 36216, 5376, 6290,
  39170, 6528, 6802, 8306, 8562, 8594, 8626, 41764, 42449, 43153,
  10386, 10706, 11281, 11410, 11634, 11890, 11896, 12306, 12320, 12334,
  12440, 12672, 12696, 13330, 13331, 13443, 13458, 13504, 13508, 46286,
  13528, 46336, 46338, 13656, 46456, 13728, 46545, 13892, 13920, 48484,
  17426, 50247, 17490, 17554, 17604, 17664, 17728, 17746, 17778, 17784,
  50744, 50759, 18034, 18577, 18770, 51661, 19032, 19058, 52295, 19587,
  19601, 19602, 19608, 19725, 20068, 20082, 20270, 20850, 20899, 21156,
  21645, 21778, 23683, 23698, 23820, 23826, 57380, 57809, 25171, 25745,
  25746, 370, 33144, 434, 33203, 440, 626, 632, 2112, 35851,
  36096, 3330, 4722, 8632, 11378, 11522, 44493, 11890, 12434, 12754,
  13426, 13970, 15058, 50264, 17554, 50372, 50451, 17764, 50744, 18450,
  18690, 18852, 18893, 51800, 52235, 19587, 52369, 19602, 19698, 19714,
  19730, 20004, 52782, 22659, 22898, 26414, 470, 2606, 3192, 3268,
  3346, 36435, 4530, 5298, 5304, 39155, 10371, 10386, 10912, 11335,
  11409, 11410, 11520, 11602, 44376, 11634, 11730, 11890, 12419, 12433,
  12434, 45314, 13380, 13426, 13443, 46225, 13458, 13528, 13580, 13586,
  13636, 13650, 46545, 13938, 13944, 46738, 17539, 17554, 17677, 18023,
  18208, 18542, 51345, 19032, 19058, 51832, 19601, 19602, 19730, 20004,
  20082, 23683, 56465, 56466, 4484, 434, 626, 2272, 2386, 35851,
  35979, 3212, 3218, 6360, 6795, 7825, 8562, 8740, 41555, 10386,
  11275, 44049, 11282, 11378, 11384, 11410, 11634, 11640, 11890, 12696,
  13315, 13330, 13412, 46232, 13650, 13924, 46695, 14407, 14450, 47224,
  14467, 14770, 14776, 14898, 47684, 14962, 15491, 15505, 15506, 15512,
  50187, 17432, 17547, 17554, 17842, 17988, 18584, 51800, 52363, 19602,
  19603, 19698, 19704, 52485, 52689, 20068, 52846, 20626, 20835, 20851,
  53667, 53683, 53805, 53828, 53848, 53863, 21635, 21649, 21650, 54532,
  22659, 22673, 23812, 2317, 2386, 2392, 2513, 2606, 2706, 35960,
  3616, 5298, 5736, 39128, 9482, 11335, 11347, 11410, 11416, 11616,
  11634, 11880, 45464, 12786, 46151, 14770, 50187, 17559, 17746, 50520,
  17956, 18563, 18577, 18578, 51448, 51458, 18776, 19032, 19047, 19058,
  19064, 19587, 19601, 19602, 20082, 25610, 26424, 363, 13319, 13330,
  17959, 19698, 434, 1473, 2286, 2596, 3345, 4776, 8195, 8370,
  8562, 8736, 41636, 43139, 10385, 10400, 12419, 12433, 12434, 13752,
  15506, 15844, 15864, 17490, 17554, 17682, 17752, 51211, 51832, 19467,
  19527, 19602, 20088, 54283, 21521, 54411, 21650, 22200, 22529, 498,
  562, 615, 2386, 4210, 36984, 41159, 11640, 15505, 17419, 17522,
  17528, 50852, 18104, 19058, 20088, 20868, 54417, 22162, 22667, 55441,
  55672, 23128, 23154, 24210, 22660, 34961, 35044, 2386, 3730, 36932,
  5304, 38520, 39155, 7435, 12328, 12344, 13458, 13600, 13773, 46695,
  15378, 15864, 18568, 19090, 19601, 19715, 20004, 20014, 20088, 21515,
  23683, 23698, 1080, 34148, 34168, 2386, 3083, 3192, 3218, 3730,
  4466, 7638, 41540, 41560, 12291, 12429, 13380, 13458, 13459, 14770,
  47684, 17664, 17810, 50791, 18563, 18578, 52295, 19587, 19602, 20108,
  20914, 21508, 21522, 54411, 22552, 1080, 3217, 3218, 36036, 10371,
  10386, 11634, 12338, 12429, 50756, 52856, 352, 44493, 12775, 10381,
  10892, 17539, 18578, 51474, 19058, 19597, 19698, 4103, 4456, 37444,
  4882, 8818, 9363, 14482, 14696, 35473, 36877, 7313, 7716, 10385,
  17536, 19459, 19467, 19469, 19603, 20840, 3217, 36216, 4557, 4748,
  8844, 14898, 14993, 2706, 5131, 5251, 38033, 52365, 4227, 7180,
  8868, 11395, 11409, 11410, 17554, 12546, 11395, 11409, 13650, 488,
  786, 17426, 3213, 3217, 3332, 4354, 4365, 4748, 41636, 11282,
  14552, 576, 1169, 1572, 37056, 4530, 8818, 41421, 51347, 19714,
  7299, 12775, 19715, 25731, 25746, 242, 370, 4530, 37408, 8612,
  8614, 8844, 19587, 52482, 370, 422, 612, 34067, 2066, 35985,
  4099, 38925, 8331, 11727, 13586, 17682, 8337, 12546, 40081, 19473,
  52369, 6387, 46148, 15588, 18571, 19058, 52334, 19587, 52369, 19662,
  20064, 25739, 26066, 370, 568, 612, 4530, 4658, 4659, 8612,
  14595, 14702, 20836, 41382, 11411, 13443, 46225, 1494, 8292, 8580,
  47524, 2179, 2193, 2194, 2200, 2272, 2386, 2674, 3620, 4109,
  4557, 38925, 6275, 39057, 6290, 8562, 8626, 41395, 8754, 8768,
  11395, 11406, 44177, 11410, 11634, 11640, 11666, 11672, 11864, 12768,
  46176, 13443, 46219, 13458, 13504, 13522, 46338, 13732, 13934, 13938,
  48139, 15378, 48273, 15864, 17426, 17479, 17539, 17553, 17554, 17556,
  50496, 17746, 17752, 17867, 17970, 17976, 50756, 18034, 50808, 18084,
  18656, 18668, 51808, 51812, 19058, 19064, 52295, 19597, 19602, 19698,
  19725, 52494, 19832, 20056, 52856, 53828, 21517, 21635, 21650, 21763,
  21778, 22659, 22898, 22962, 23698, 24707, 24708, 24721, 32836, 32839,
  210, 338, 344, 370, 434, 33323, 562, 563, 632, 1042,
  34829, 2386, 35851, 4434, 4466, 4530, 4594, 4658, 4772, 10253,
  10386, 10692, 11890, 46091, 13380, 13426, 13728, 13732, 13736, 46520,
  14770, 47544, 15864, 50247, 17524, 50443, 17682, 17746, 50520, 17810,
  17976, 17988, 51544, 18898, 19054, 19058, 19064, 52235, 19601, 19731,
  52856, 22660, 22803, 196, 33348, 4557, 8203, 8775, 11735, 47524,
  14770, 47544, 47726, 14962, 24928, 24932, 33198, 2386, 35160, 2515,
  2699, 36932, 4658, 37496, 6360, 8614, 10258, 10371, 10385, 10386,
  11269, 11335, 11395, 11400, 11410, 11634, 44499, 12786, 46151, 13443,
  13458, 13464, 13522, 13646, 13650, 13728, 13752, 13920, 13934, 13938,
  13967, 14770, 47762, 15491, 15505, 15506, 15635, 17028, 17862, 19044,
  19474, 52295, 19698, 19704, 19917, 20064, 54739, 23691, 56580, 59168,
  32836, 196, 259, 33037, 275, 33188, 33194, 434, 33203, 583,
  588, 33380, 626, 632, 768, 786, 33568, 36867, 4114, 36883,
  4132, 4146, 4512, 8256, 8323, 8337, 8338, 14404, 14450, 14762,
  14834, 14962, 15058, 15122, 53322, 20690, 20865, 53636, 53647, 20882,
  20888, 20906, 53831, 71, 2386, 2392, 3192, 4498, 4696, 4722,
  6296, 6340, 8564, 41395, 41540, 10371, 43153, 10386, 10392, 44049,
  11395, 11409, 11410, 11534, 11538, 44352, 11634, 12047, 12050, 12434,
  13380, 13400, 13426, 13458, 13522, 14407, 14450, 14656, 14706, 15506,
  15840, 48632, 16015, 50247, 17539, 17553, 17554, 17624, 17752, 18034,
  18563, 18577, 18578, 18707, 19012, 19058, 20078, 20088, 53319, 20658,
  20851, 53667, 21042, 21106, 21112, 55441, 23698, 402, 416, 422,
  33194, 580, 612, 626, 33485, 786, 37005, 4594, 4672, 4676,
  37458, 4856, 4882, 41028, 8266, 41060, 8323, 8337, 8338, 8402,
  41348, 8590, 8591, 8618, 41395, 8653, 41425, 8772, 41548, 8888,
  41764, 47140, 14450, 14476, 14514, 14692, 14734, 14738, 47524, 14758,
  47557, 14834, 47684, 14936, 47747, 47780, 47819, 15058, 47864, 20580,
  53668, 364, 7190, 14354, 24647, 2368, 2386, 3288, 5298, 38072,
  10371, 10386, 10560, 11282, 11395, 11409, 11410, 11634, 11762, 11768,
  44612, 12306, 12786, 46151, 13522, 13528, 13650, 13938, 15364, 15371,
  48395, 48632, 50308, 17553, 50372, 17778, 17970, 50756, 18008, 51448,
  19032, 20078, 20082, 52856, 6552, 11725, 14595, 17554, 23698, 23812,
  825, 1376, 15602, 32842, 114, 165, 33035, 33092, 344, 356,
  33132, 435, 554, 33331, 583, 584, 594, 786, 4109, 37005,
  4459, 4653, 37432, 37459, 37508, 41034, 8306, 41107, 8357, 41323,
  41331, 8626, 8627, 8690, 8692, 8740, 8746, 8755, 41572, 8818,
  14605, 14611, 47712, 47716, 14951, 33832, 34051, 2179, 34961, 2194,
  2386, 2509, 35857, 3336, 36110, 3351, 3533, 5746, 6162, 6275,
  6290, 6336, 6418, 8306, 8562, 8626, 41400, 41540, 9223, 9362,
  10371, 10385, 10386, 11410, 44408, 12429, 12548, 12786, 13380, 46151,
  13432, 13444, 46276, 13528, 13650, 13938, 48387, 17539, 17553, 17554,
  18930, 18936, 19467, 19469, 19587, 19595, 19601, 19602, 19684, 52494,
  20088, 21635, 21643, 54413, 21649, 21650, 21773, 22673, 25037, 25731,
  25732, 25746, 58833, 32839, 32851, 114, 32888, 33132, 370, 402,
  498, 556, 562, 608, 676, 1154, 33931, 1283, 1682, 1683,
  34831, 2116, 2509, 2664, 2670, 35473, 2707, 3085, 3331, 3448,
  3735, 3844, 4210, 4216, 4274, 4434, 4440, 4466, 4691, 4772,
  38033, 5395, 5399, 5688, 38923, 6295, 7169, 8376, 41165, 8626,
  8868, 9478, 44055, 44056, 44290, 12303, 12439, 45331, 12567, 13323,
  13426, 13458, 46230, 13581, 13938, 14886, 48152, 15499, 48504, 15858,
  15918, 17421, 18061, 18451, 51347, 18582, 51469, 18707, 18902, 19058,
  19527, 19716, 20014, 52792, 53828, 54411, 21651, 22148, 22659, 4114,
  4748, 41390, 14721, 20865, 57732, 370, 435, 608, 2179, 2193,
  2194, 2386, 35985, 3218, 36036, 3288, 4466, 5265, 5298, 38244,
  5746, 39155, 39171, 39377, 11395, 11410, 11620, 11634, 12419, 12434,
  13426, 13522, 13650, 46660, 14962, 48269, 15505, 51341, 18577, 18578,
  18664, 18770, 51544, 19602, 20280, 54283, 21635, 21645, 54417, 21650,
  54419, 24587, 32839, 114, 402, 434, 562, 33363, 1155, 1170,
  34061, 1380, 34259, 2386, 35160, 35982, 6289, 39556, 8562, 8568,
  11410, 11634, 12301, 12430, 12786, 13412, 13422, 14450, 14514, 14674,
  14680, 14738, 47512, 14916, 47699, 14962, 15491, 15505, 15506, 17028,
  18578, 18701, 19474, 19826, 52689, 20676, 53447, 53667, 53828, 21075,
  53860, 21106, 21635, 21649, 21650, 22541, 55416, 22659, 22667, 22673,
  57355, 1163, 1170, 1380, 2276, 2386, 3186, 35960, 3217, 5298,
  38968, 8614, 8626, 11395, 44177, 11410, 12299, 12320, 12429, 12696,
  45521, 12786, 13458, 13522, 13570, 46520, 13938, 13944, 15492, 50187,
  18578, 18680, 18770, 19058, 51832, 19698, 20088, 33937, 1293, 1380,
  1491, 1568, 1572, 2386, 2592, 3218, 3316, 36216, 4704, 38033,
  5266, 6162, 6289, 6290, 6360, 7425, 8384, 8562, 41395, 8807,
  10385, 10386, 10514, 44035, 11407, 11410, 11640, 11656, 11723, 44493,
  11744, 11840, 11890, 44664, 11940, 11950, 12320, 12434, 12440, 13426,
  46200, 13457, 13522, 15619, 15825, 15864, 17421, 17522, 17604, 17682,
  17752, 17874, 18701, 51800, 19480, 19587, 19596, 19602, 52493, 20241,
  53316, 53336, 53664, 21048, 21635, 21649, 21650, 54737, 21976, 54968,
  22659, 22673, 23698, 24721, 50, 114, 355, 33124, 33135, 376,
  399, 402, 434, 33203, 484, 33316, 33317, 559, 562, 33336,
  626, 4515, 37284, 37299, 7566, 7872, 8653, 14501, 47459, 14756,
  47567, 47571, 47588, 47652, 47661, 14993, 47763, 47819, 15058, 17414,
  17420, 50191, 17540, 50326, 17676, 17679, 17859, 50817, 18060, 20512,
  20590, 20594, 53413, 20843, 20882, 20978, 21029, 21088, 21092, 21106,
  370, 402, 408, 562, 626, 1676, 2126, 2674, 35853, 3268,
  3288, 3724, 4210, 36984, 4434, 4466, 4498, 4594, 4658, 6574,
  6788, 8612, 8772, 8780, 41764, 11271, 11634, 12562, 13426, 13728,
  13777, 46660, 13924, 15371, 15616, 15832, 15968, 15986, 17416, 17553,
  17586, 17604, 17669, 17677, 17878, 50828, 50852, 18094, 19460, 20082,
  20111, 54413, 21649, 22541, 22659, 55441, 23698, 32842, 32868, 114,
  32888, 32947, 242, 33092, 334, 33112, 356, 33131, 371, 33156,
  402, 426, 33252, 547, 33316, 33322, 33327, 33444, 33483, 716,
  786, 4101, 36881, 4210, 37005, 37007, 37009, 37011, 37221, 37227,
  4515, 4813, 4818, 8323, 8337, 41106, 41139, 8555, 8594, 41380,
  8626, 41400, 8690, 41508, 8746, 8753, 41523, 8864, 8868, 8882,
  14347, 14355, 47178, 14481, 14482, 47524, 47562, 14795, 14802, 47571,
  14834, 47652, 47667, 14948, 14962, 47763, 47780, 47821, 15058, 47832,
  50307, 50326, 50817, 50822, 20554, 20914, 20915, 21063, 21106, 24721,
  24952, 1803, 2386, 3203, 3218, 3428, 37060, 4772, 5746, 6386,
  39155, 6411, 6528, 6578, 10258, 11378, 11602, 11608, 11634, 44408,
  11730, 11890, 11896, 11936, 12305, 12320, 46148, 13458, 13462, 13508,
  13522, 13650, 46738, 17539, 50317, 17554, 18443, 18578, 19473, 19587,
  19602, 20114, 23698, 56935, 56952, 25617, 25731, 25745, 25746, 355,
  33380, 4109, 4243, 4306, 4365, 4463, 4594, 4818, 8306, 8323,
  8337, 41106, 8357, 41323, 8591, 8594, 8618, 8626, 8627, 8690,
  8747, 8753, 41523, 8804, 8818, 8868, 14347, 14919, 20498, 20842,
  53611, 53674, 50, 32842, 33037, 324, 402, 33190, 33203, 498,
  33351, 33380, 626, 632, 33444, 786, 4210, 37002, 37007, 37011,
  37363, 4818, 41028, 41034, 41060, 8337, 41348, 8594, 41368, 41382,
  8618, 8690, 8818, 14386, 14482, 14546, 14799, 47588, 14834, 14919,
  47719, 14962, 15058, 20530, 20611, 20626, 20645, 20690, 53647, 20882,
  53670, 20906, 21039, 21042, 53831, 24721, 57720, 25060, 32842, 33131,
  371, 556, 33331, 33351, 804, 36881, 37227, 4467, 4818, 41316,
  41514, 8804, 41575, 47178, 14546, 47428, 14670, 47448, 14707, 14948,
  21106, 32842, 180, 210, 33035, 33092, 344, 498, 33316, 33323,
  615, 36874, 4170, 37009, 8266, 8292, 41125, 8676, 8690, 8818,
  14386, 14546, 14787, 14794, 47567, 14803, 47652, 47667, 14962, 47763,
  15058, 47832, 20530, 53413, 20690, 338, 498, 33272, 562, 676,
  33937, 2386, 2404, 3090, 5137, 5138, 5728, 5752, 39128, 8562,
  11277, 44049, 11374, 11395, 11410, 44291, 11725, 11730, 11922, 44708,
  12306, 13329, 13412, 13458, 13522, 46338, 13688, 13752, 14951, 14968,
  15825, 15864, 17458, 17553, 17554, 17614, 50744, 18016, 18034, 18066,
  20679, 20818, 20850, 53667, 20978, 20984, 21042, 21060, 53863, 22659,
  22673, 32836, 32868, 232, 324, 363, 402, 422, 426, 434,
  33316, 33322, 562, 588, 612, 626, 33485, 786, 36874, 36881,
  36938, 4178, 36995, 37227, 4467, 37283, 37299, 4562, 4652, 4818,
  8256, 41028, 41048, 8323, 8331, 8337, 41106, 8357, 41284, 8536,
  8548, 41323, 8563, 41380, 8626, 8632, 41508, 8760, 41572, 8818,
  8825, 8882, 11283, 11288, 44307, 47371, 47428, 14789, 47562, 47563,
  47565, 14801, 47652, 47667, 14962, 47763, 17414, 17427, 50200, 50308,
  17670, 17683, 17862, 18052, 20594, 20611, 20626, 20868, 20888, 21037,
  21042, 53811, 21088, 20481, 53251, 20499, 20737, 53507, 21096, 50,
  32842, 32933, 32964, 210, 216, 259, 33037, 33041, 33092, 33124,
  33130, 33131, 33167, 33187, 420, 422, 33194, 487, 33316, 33322,
  562, 33331, 583, 33380, 626, 676, 786, 4099, 36874, 36875,
  36876, 4227, 37003, 37007, 37009, 4365, 4448, 4452, 4498, 4526,
  4531, 4594, 4644, 37421, 4722, 4818, 8263, 41034, 8338, 41125,
  8548, 41323, 8563, 41382, 41386, 41395, 8676, 8746, 8754, 14355,
  47178, 14594, 14660, 47460, 14720, 47503, 47524, 47544, 47555, 14794,
  47563, 14799, 14820, 14834, 47652, 47658, 47660, 47672, 14930, 14991,
  14993, 47763, 47780, 15058, 50191, 50198, 50200, 17540, 17551, 17558,
  17664, 50447, 17871, 17878, 17880, 18060, 50835, 20530, 53322, 20594,
  53368, 53413, 20843, 53647, 53670, 53674, 20914, 53683, 20960, 20964,
  57700, 24936, 676, 2386, 2596, 3537, 3672, 4196, 4728, 38929,
  8614, 8627, 41572, 8818, 11336, 11424, 11602, 44376, 11640, 12290,
  12672, 12786, 46520, 13967, 48273, 15904, 17415, 17419, 17426, 17522,
  17553, 17586, 50372, 17624, 50552, 17952, 18664, 20000, 50, 196,
  210, 269, 356, 392, 33167, 426, 434, 498, 547, 33324,
  563, 583, 615, 626, 786, 36881, 36883, 4196, 37007, 37011,
  37227, 37363, 41139, 8402, 8555, 8594, 41380, 41382, 8618, 41444,
  41515, 41543, 9017, 47565, 47567, 47651, 47652, 47661, 14962, 53670,
  2180, 11751, 13394, 13763, 13927, 50831, 1080, 1170, 1376, 34148,
  34254, 1489, 2195, 2276, 35091, 2386, 35160, 2514, 4466, 5376,
  8384, 8562, 41395, 10258, 43149, 10385, 10386, 10509, 11282, 11409,
  11410, 11602, 11608, 44408, 44493, 11914, 11922, 12419, 12433, 12434,
  12562, 12696, 12786, 46286, 13522, 13528, 13632, 13650, 13912, 13934,
  15378, 15491, 48273, 15506, 15633, 15634, 50296, 17539, 17554, 17604,
  17842, 17874, 17875, 17906, 17976, 17992, 18034, 18040, 18577, 18770,
  19012, 51812, 51832, 19601, 20088, 53683, 20964, 22968, 23683, 23697,
  56466, 23826, 24013, 25120, 26400, 32839, 338, 370, 402, 562,
  568, 33348, 626, 2290, 2296, 3192, 4498, 4530, 4536, 37479,
  11415, 11522, 11634, 11640, 45550, 12786, 45555, 13383, 13426, 46227,
  13508, 13568, 13742, 13773, 46545, 46660, 46695, 13938, 15492, 15619,
  17476, 17810, 17842, 17952, 17976, 50756, 18784, 19040, 19058, 19064,
  19698, 20000, 24178, 420, 33194, 722, 4132, 4160, 4242, 37043,
  4301, 37137, 37252, 37412, 37444, 4704, 4818, 4824, 41034, 41093,
  41159, 8555, 8612, 41382, 41386, 8626, 8651, 41507, 14692, 14758,
  47661, 14895, 47684, 14994, 50308, 50326, 50625, 17860, 50646, 18060,
  20594, 20690, 20768, 53633, 53647, 21248, 57732, 33312, 1280, 2059,
  2386, 35851, 3203, 3218, 4658, 5298, 39057, 39155, 6605, 11364,
  11395, 11409, 11410, 11634, 11879, 11890, 12419, 45201, 12434, 45315,
  13440, 13443, 13458, 46276, 13522, 13752, 13938, 15960, 50307, 17554,
  19469, 19527, 19601, 19684, 19730, 52580, 20004, 26424, 114, 120,
  33363, 35864, 3192, 4114, 5298, 6162, 8548, 8562, 10264, 10371,
  43149, 10386, 11277, 11281, 11634, 11640, 11922, 12043, 12338, 12434,
  46091, 13443, 13457, 13458, 13504, 13522, 46338, 13732, 13970, 14706,
  47719, 14962, 48153, 15491, 15492, 15505, 15506, 48386, 17028, 50247,
  17672, 17970, 17988, 17994, 50766, 18020, 18034, 18066, 52235, 19587,
  19596, 19601, 19602, 53319, 53447, 21042, 21106, 22659, 55435, 55441,
  22962, 56578, 23821, 24707, 32836, 32842, 32851, 32868, 33035, 33037,
  33043, 33167, 402, 498, 33351, 594, 676, 715, 786, 36867,
  36883, 4227, 4237, 4242, 4434, 37283, 4690, 4818, 4882, 8195,
  40971, 40996, 8260, 41034, 8323, 8337, 41106, 8516, 41323, 8594,
  8612, 8658, 8676, 8690, 8804, 14355, 47467, 14735, 14752, 47567,
  14820, 14951, 14962, 47763, 15012, 15058, 15122, 53316, 53322, 20611,
  20625, 20626, 20690, 20843, 53624, 53647, 53674, 21074, 53843, 53863,
  24960, 25171, 562, 1035, 1042, 1080, 1155, 1169, 1170, 2386,
  5746, 5752, 44303, 11620, 12547, 45464, 45521, 13330, 13443, 46225,
  13458, 13570, 13752, 15716, 17028, 17454, 17586, 17592, 17842, 17906,
  18770, 52689, 20072, 20088, 20114, 23698, 269, 422, 36874, 4227,
  4237, 37011, 4650, 4655, 41028, 8402, 8555, 41380, 8626, 41515,
  41555, 8818, 8947, 41382, 10386, 15371, 15491, 15506, 15826, 17554,
  17874, 35985, 11277, 580, 34961, 4480, 13316, 13329, 52768, 1163,
  1169, 1568, 8202, 15491, 499, 556, 568, 1041, 1283, 1495,
  2063, 35172, 2707, 35985, 3331, 36484, 5251, 38163, 5399, 8376,
  41421, 41572, 8818, 41592, 9228, 11288, 11539, 12301, 12435, 15494,
  15501, 15629, 17670, 18456, 18579, 18583, 19716, 52491, 22659, 22989,
  25871, 4515, 15491, 48273, 51347, 33805, 4206, 6275, 6289, 6290,
  8203, 41380, 32964, 4658, 40081, 41382, 53259, 21039, 21048, 4648,
  8548, 52369, 4114, 20832, 6155, 39556, 6802, 11410, 44179, 44291,
  44497, 11844, 44676, 44708, 12786, 12792, 13408, 13458, 15491, 15506,
  15619, 48593, 17426, 17664, 17687, 18066, 18084, 18578, 19058, 19064,
  19714, 53619, 20915, 3330, 4594, 4658, 4664, 6157, 6290, 8562,
  8626, 8632, 11281, 11378, 11916, 13316, 13323, 13426, 13508, 13581,
  46540, 13938, 46724, 17458, 17560, 50372, 17984, 50756, 17998, 18003,
  18036, 50852, 19040, 19058, 19527, 19601, 23683, 23698, 370, 419,
  1170, 34833, 2179, 2194, 35982, 4818, 4824, 39179, 39377, 11409,
  44384, 11624, 12429, 13458, 13570, 46859, 47456, 14706, 48273, 50187,
  17550, 17600, 18036, 50834, 18450, 18563, 18578, 51475, 51665, 51808,
  19460, 52235, 20064, 21522, 54531, 23693, 34827, 3392, 39556, 41028,
  8306, 8548, 11281, 11410, 11890, 11936, 12433, 45331, 19587, 52369,
  19602, 53319, 20676, 22659, 55435, 22673, 8614, 2372, 2382, 2386,
  2392, 3203, 3217, 3218, 3268, 3346, 38033, 5746, 6275, 39057,
  6290, 39373, 7630, 8370, 8562, 8626, 41555, 8818, 8868, 10371,
  10381, 10386, 11395, 11410, 11602, 11634, 11640, 44665, 13426, 13443,
  13458, 13688, 13728, 13938, 17522, 17554, 17810, 17842, 17906, 18034,
  18040, 18680, 18930, 51812, 52295, 52369, 20082, 21635, 21649, 21650,
  23683, 23693, 23698, 26062, 355, 370, 434, 562, 33336, 33444,
  1080, 1169, 36036, 4210, 36984, 4434, 4530, 4536, 4594, 4600,
  4691, 5746, 41159, 41507, 8754, 10258, 11378, 11634, 11640, 11890,
  13383, 13426, 17991, 19832, 74, 33124, 402, 498, 33317, 626,
  4107, 36883, 37003, 4237, 4458, 4460, 4463, 4530, 37412, 4722,
  41031, 41125, 41316, 8594, 41380, 8626, 8632, 8690, 8691, 41515,
  8753, 8754, 8775, 41546, 8787, 41572, 8818, 8824, 9017, 47460,
  14735, 47567, 14834, 14891, 47684, 14926, 20978, 2386, 35981, 35985,
  3218, 36310, 36455, 37219, 6387, 10514, 11378, 11395, 11410, 11634,
  11640, 11890, 12786, 12792, 46148, 46151, 13426, 46200, 13443, 13458,
  13522, 13650, 13778, 14116, 15491, 48273, 15506, 17539, 17553, 17554,
  17952, 18563, 18573, 51345, 18578, 18930, 51704, 52295, 19684, 19704,
  52856, 21635, 21650, 25741, 355, 3268, 10381, 11378, 11442, 45069,
  12338, 45197, 13650, 13656, 14450, 47224, 14467, 14481, 14514, 14706,
  14834, 15160, 17522, 50296, 17746, 50531, 17810, 17816, 50744, 50756,
  50771, 50791, 53603, 53667, 54413, 22659, 74, 498, 33383, 4106,
  36938, 4530, 4691, 41382, 41555, 41572, 8818, 47526, 47716, 14951,
  20902, 24721, 24952, 18003, 1154, 13568, 13773, 17666, 17879, 11404,
  11723, 35059, 7630, 10560, 12429, 13650, 15818, 15821, 15864, 17522,
  17842, 22659, 22898, 22962, 23026, 242, 434, 33325, 562, 33363,
  6354, 11634, 11762, 13920, 18578, 19730, 37219, 10386, 3211, 3428,
  6402, 6418, 10371, 10379, 10386, 11602, 13650, 53670, 21042, 53863,
  22898, 2112, 2392, 10584, 11277, 12696, 18034, 8740, 12544, 15864,
  17586, 24845, 25741, 1154, 34336, 1682, 17874, 19058, 19064, 19474,
  1171, 11335, 13394, 13458, 13522, 13528, 15864, 19730, 14968, 13323,
  13443, 13458, 14595, 14738, 17675,
}
ANSWER_STARTS = {
  0, 0, 10, 14, 21, 21, 23, 28, 29, 30,
  30, 30, 49, 56, 63, 63, 68, 68, 76, 79,
  81, 83, 85, 91, 91, 91, 92, 108, 108, 108,
  108, 122, 122, 122, 122, 127, 127, 127, 152, 152,
  152, 165, 165, 165, 188, 188, 188, 204, 204, 204,
  204, 204, 204, 219, 219, 219, 219, 220, 220, 220,
  252, 255, 255, 255, 274, 274, 274, 290, 290, 290,
  319, 319, 319, 323, 323, 323, 323, 324, 324, 330,
  330, 330, 330, 342, 342, 342, 342, 349, 349, 349,
  349, 349, 349, 355, 355, 355, 373, 373, 373, 377,
  377, 377, 377, 378, 378, 384, 384, 384, 384, 384,
  384, 384, 384, 385, 386, 386, 391, 392, 398, 398,
  398, 400, 405, 406, 406, 406, 408, 408, 413, 413,
  413, 425, 425, 425, 425, 429, 429, 429, 429, 444,
  444, 444, 466, 466, 466, 475, 475, 475, 487, 487,
  487, 492, 492, 492, 492, 492, 492, 497, 497, 497,
  497, 498, 498, 498, 499, 502, 502, 502, 513, 513,
  514, 518, 518, 518, 550, 550, 550, 557, 557, 557,
  557, 557, 557, 567, 567, 567, 567, 573, 573, 573,
  573, 576, 576, 576, 576, 576, 576, 588, 588, 588,
  588, 588, 588, 595, 595, 595, 595, 595, 595, 595,
  595, 596, 598, 598, 598, 598, 598, 598, 598, 598,
  598, 600, 604, 604, 604, 604, 604, 605, 605, 605,
  606, 606, 606, 606, 606, 607, 607, 607, 607, 609,
  609, 609, 609, 610, 610, 610, 610, 610, 610, 612,
  612, 612, 612, 612, 612, 614, 614, 614, 614, 614,
  614, 615, 615, 615, 615, 615, 615, 615, 616, 617,
  617, 617, 617, 617, 623, 624, 624, 624, 624, 624,
  624, 624, 624, 624, 624, 624, 624, 640, 640, 640,
  640, 653, 653, 653, 653, 663, 663, 663, 664, 664,
  664, 678, 678, 678, 678, 678, 678, 683, 683, 683,
  683, 685, 685, 704, 704, 704, 704, 717, 717, 717,
  717, 728, 728, 728, 728, 728, 728, 749, 749, 749,
  749, 749, 749, 757, 757, 757, 757, 757, 757, 763,
  763, 763, 763, 769, 769, 769, 769, 775, 775, 775,
  775, 775, 775, 783, 783, 783, 783, 783, 783, 786,
  786, 786, 786, 787, 787, 788, 789, 791, 792, 792,
  794, 794, 794, 794, 794, 794, 795, 796, 798, 798,
  800, 800, 803, 803, 805, 808, 808, 810, 810, 810,
  811, 829, 829, 829, 829, 843, 843, 843, 847, 858,
  858, 858, 871, 871, 871, 881, 881, 881, 901, 901,
  901, 911, 911, 911, 911, 911, 911, 911, 911, 911,
  911, 911, 911, 911, 911, 911, 911, 911, 911, 911,
  911, 911, 911, 911, 911, 911, 911, 927, 927, 927,
  927, 927, 927, 941, 941, 941, 941, 963, 963, 963,
  965, 978, 978, 978, 978, 978, 978, 994, 994, 994,
  994, 994, 994, 1001, 1001, 1001, 1001, 1001, 1001, 1016,
  1016, 1036, 1036, 1044, 1044, 1044, 1085, 1093, 1093, 1099,
  1122, 1134, 1148, 1158, 1190, 1192, 1192, 1192, 1247, 1254,
  1254, 1274, 1274, 1275, 1275, 1290, 1290, 1290, 1290, 1300,
  1300, 1300, 1321, 1330, 1330, 1330, 1330, 1330, 1330, 1346,
  1346, 1346, 1373, 1373, 1373, 1378, 1378, 1384, 1384, 1385,
  1385, 1385, 1385, 1385, 1386, 1386, 1386, 1386, 1386, 1386,
  1386, 1386, 1388, 1388, 1396, 1396, 1398, 1398, 1400, 1404,
  1405, 1405, 1405, 1405, 1405, 1405, 1405, 1413, 1413, 1413,
  1413, 1418, 1418, 1418, 1418, 1433, 1433, 1433, 1433, 1433,
  1433, 1440, 1440, 1440, 1440, 1440, 1440, 1440, 1440, 1440,
  1440, 1440, 1440, 1448, 1448, 1448, 1448, 1454, 1454, 1454,
  1469, 1482, 1482, 1482, 1482, 1482, 1482, 1494, 1494, 1494,
  1501, 1501, 1501, 1501, 1501, 1501, 1501, 1501, 1501, 1501,
  1501, 1501, 1501, 1501, 1501, 1501, 1501, 1501, 1501, 1501,
  1501, 1501, 1501, 1501, 1501, 1501, 1501, 1501, 1501, 1501,
  1501, 1501, 1501, 1501, 1501, 1502, 1502, 1502, 1502, 1504,
  1504, 1504, 1504, 1505, 1505, 1505, 1505, 1505, 1505, 1507,
  1507, 1507, 1507, 1507, 1507, 1507, 1507, 1507, 1507, 1507,
  1507, 1507, 1507, 1507, 1507, 1508, 1508, 1508, 1508, 1508,
  1508, 1508, 1508, 1508, 1508, 1508, 1508, 1508, 1508, 1508,
  1508, 1508, 1508, 1508, 1508, 1508, 1508,
}
-- END GENERATED WORDS
