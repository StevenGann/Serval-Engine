-- fireflies.lua: the fireflies example's game logic in the Lua subset
-- (docs/lua.md), translated from the hand-written listing
-- examples/fireflies/fireflies.svm. tools/svlua.py compiles it to a listing
-- that examples/fireflies/main.c runs unchanged: the same objects in the same
-- order (main.c starts OBJ_ROOM and OBJ_SPAWNER as threads), the same
-- globals in the same order (main.c reads G_RESTART), the same component
-- tags (C_PLAYER and C_FIREFLY from game.h, which main.c's collision pairs
-- look for), and the random numbers drawn in the same order, so a round
-- plays out frame for frame as the listing's does.
--
-- Names in ALL_CAPS that this file doesn't define come from the game's C
-- headers: game.h and the engine's ecs.h, core.h, sprites.h, path.h and
-- screen.h (svm.py asm --header, as serval_add_script's HEADERS for the
-- listing).

-- Objects -------------------------------------------------------------------

-- Room and Spawner have no components: main.c runs their room_start handlers
-- as threads (vm_start). The others are entities the scripts spawn.
Room = object {}     -- the round: HUD, timer, music, the end and the restart request
Spawner = object {}  -- a new firefly every 40-90 frames, at most 8 at once
Player = object { components = C_POS | C_VEL | C_SPR | C_ANIM | C_BODY | C_PLAYER,
                  sprite = SPR_SERVAL_IDLE }   -- the serval, walked with the D-pad
Firefly = object { components = C_POS | C_VEL | C_SPR | C_ANIM | C_BODY | C_FIREFLY,
                   sprite = SPR_FIREFLY }      -- wanders, blinks, fades out; caught on touch
Sparkle = object { components = C_POS | C_SPR | C_ANIM,
                   sprite = SPR_SPARKLE }      -- the burst where a firefly was caught
Resting = object { components = C_POS | C_SPR | C_ANIM,
                   sprite = SPR_SERVAL_SIT }   -- the serval sitting when time is up

-- Globals: the scripts' shared state, all set by the Room when a round
-- starts. C reads G_RESTART only.

score = 0          -- fireflies caught this round
time = 0           -- seconds left
live = 0           -- fireflies alive (their create and destroy keep count)
playing = false    -- true while the round runs
restart = false    -- set by the Room when START is pressed after the round
spawn_min = 0      -- the Spawner's delay range, in frames; shrinks every 10 points
spawn_max = 0
player = none      -- the serval

-- Numbers -------------------------------------------------------------------

local SPEED <const> = 1.5  -- the serval, in pixels per frame

-- HUD and messages: text cells (30 x 20).
local SCORE_COL <const> = 7  -- "SCORE" at 1
local TIME_COL <const> = 28  -- "TIME" at 23
local HINT_COL <const> = 5   -- 20 characters, centered
local HINT_ROW <const> = 6   -- above the serval's ears
local TIME_UP_ROW <const> = 8
local CAUGHT_ROW <const> = 10
local START_ROW <const> = 12
local BLANK_LINE <const> = "                    "

local PLAYER_X <const> = (SCREEN_W - SERVAL_BODY_W) // 2
local PLAYER_Y <const> = (FIELD_TOP + SCREEN_H - SERVAL_BODY_H) // 2

-- Where fireflies appear (their top-left, in pixels).
local SPAWN_LEFT <const> = 8
local SPAWN_RIGHT <const> = SCREEN_W - 16
local SPAWN_TOP <const> = FIELD_TOP + 16
local SPAWN_BOTTOM <const> = SCREEN_H - 32
local SPAWN_CLEAR <const> = 40  -- pixels between a new firefly and the serval, at least

-- The Spawner's delay, in frames: 40-90 at first; every 10 points both ends
-- come down, until the longest is at most SPAWN_MAX_FLOOR.
local SPAWN_MIN_START <const> = 40
local SPAWN_MAX_START <const> = 90
local SPAWN_MIN_STEP <const> = 5
local SPAWN_MAX_STEP <const> = 12
local SPAWN_MAX_FLOOR <const> = 42

local FLIGHTS_MIN <const> = 3  -- a firefly's life, in flights (about 2.3 s each with its hover)
local FLIGHTS_MAX <const> = 4

local DEPTH_SERVAL <const> = 10  -- sys_render_by_depth: higher in front
local DEPTH_FIREFLY <const> = 20
local DEPTH_SPARKLE <const> = 30

local FADE_STEP <const> = 2  -- brightness change per frame in fades

-- Room ----------------------------------------------------------------------

function Room:room_start()
  -- A new round: every global set, whatever the last round left.
  score = 0
  live = 0
  restart = false
  time = ROUND_SECONDS
  playing = true
  spawn_min = SPAWN_MIN_START
  spawn_max = SPAWN_MAX_START
  play_sound(SND_START)
  music_play(SONG_DUSK)
  -- The HUD, and the last round's messages cleared.
  print(1, 0, "SCORE")
  print(TIME_COL - 5, 0, "TIME")
  print_score()
  print_time()
  print(HINT_COL, TIME_UP_ROW, BLANK_LINE)
  print(HINT_COL, CAUGHT_ROW, BLANK_LINE)
  print(HINT_COL, START_ROW, BLANK_LINE)
  print(HINT_COL, HINT_ROW, "CATCH THE FIREFLIES!")
  -- The serval, in the middle of the meadow.
  player = spawn(Player, PLAYER_X, PLAYER_Y)
  -- Fade in from black (where the last round, or the boot, left it).
  local level = SCREEN_BRIGHTNESS_MIN
  while true do
    brightness(level)
    if level == 0 then break end
    level = level + FADE_STEP
    wait(1)
  end

  -- Once a second: one second less on the HUD.
  repeat
    wait(60)
    time = time - 1
    print_time()
    if time == ROUND_SECONDS - 3 then
      print(HINT_COL, HINT_ROW, BLANK_LINE)  -- the hint goes after 3 seconds
    end
    if time <= 10 and time ~= 0 then
      play_sound(SND_TICK)  -- the last ten seconds tick
    end
  until time == 0

  -- Time is up. The Spawner and the fireflies see playing and stop.
  playing = false
  music_stop()
  play_sound(SND_TIME_UP)
  -- The serval sits down where it stands, facing the same way: a resting
  -- serval takes its place. It isn't a player (no C_PLAYER), so fireflies
  -- drifting into it are no longer caught.
  local resting = spawn(Resting, player.x, player.y)
  resting.flags = player.flags
  kill(player)
  print(11, TIME_UP_ROW, "TIME UP!")
  print(10, CAUGHT_ROW, "CAUGHT")
  print(17, CAUGHT_ROW, score)
  wait(90)
  print(9, START_ROW, "PRESS START")
  repeat wait(1) until button_pressed(BUTTON_START)
  -- Fade out, then ask C for a new round.
  level = 0
  repeat
    level = level - FADE_STEP
    brightness(level)
    wait(1)
  until level <= SCREEN_BRIGHTNESS_MIN
  restart = true  -- main.c restarts after this frame
end

-- Spawner -------------------------------------------------------------------

function Spawner:room_start()
  while true do
    wait(random_range(spawn_min, spawn_max))
    if not playing then return end
    if live < MAX_FIREFLIES then  -- else enough of them: try again later
      while true do
        -- Not on the serval, which would catch it at once: the distance
        -- between their middles, squared, must be at least SPAWN_CLEAR
        -- squared.
        local x = random_range(SPAWN_LEFT, SPAWN_RIGHT)
        local y = random_range(SPAWN_TOP, SPAWN_BOTTOM)
        local dx = x - math.floor(player.x) - (SERVAL_BODY_W // 2 - 4)
        local dy = y - math.floor(player.y) - (SERVAL_BODY_H // 2 - 4)
        if dx * dx + dy * dy >= SPAWN_CLEAR * SPAWN_CLEAR then
          spawn(Firefly, x, y)
          break
        end
        wait(1)  -- somewhere else, next frame
        if not playing then return end
      end
    end
  end
end

-- Player --------------------------------------------------------------------

function Player:create()
  self.sprite = SPR_SERVAL_IDLE
  self.body_w = SERVAL_BODY_W
  self.body_h = SERVAL_BODY_H
  self.depth = DEPTH_SERVAL
end

-- Every frame: the D-pad sets the velocity.
function Player:step()
  local vx, vy = 0.0, 0.0
  if button_down(BUTTON_LEFT) then
    vx = -SPEED
    self.flags = self.flags | SPRITE_FLIP_H  -- face left
  end
  if button_down(BUTTON_RIGHT) then
    vx = SPEED
    self.flags = self.flags & ~SPRITE_FLIP_H  -- face right
  end
  if button_down(BUTTON_UP) then vy = -SPEED end
  if button_down(BUTTON_DOWN) then vy = SPEED end
  self.vx = vx
  self.vy = vy
  -- Walking or standing. The sprite changes only when that changes, so its
  -- animation plays (and starts from its first frame).
  local sprite = SPR_SERVAL_IDLE
  if vx ~= 0 or vy ~= 0 then sprite = SPR_SERVAL_WALK end
  if self.sprite ~= sprite then
    self.sprite = sprite
    self.frame = 0
  end
end

-- Firefly -------------------------------------------------------------------

-- Create, then its whole life: it wanders along random paths until its time
-- is up (or the round's), then fades out.
function Firefly:create()
  self.body_w = 8
  self.body_h = 8
  self.depth = DEPTH_FIREFLY
  self.frame = random_range(0, FIREFLY_FRAMES - 1)  -- blinks out of step with the others
  live = live + 1
  for flight = 1, random_range(FLIGHTS_MIN, FLIGHTS_MAX) do
    if not playing then break end  -- the round is over
    path_start(self, random_range(0, PATH_COUNT - 1),
               random_range(0, PATH_MIRROR_X | PATH_MIRROR_Y))  -- mirrored at random
    wait_move()  -- until the path ends (it slows to a stop)
    wait(random_range(10, 40))  -- hover a moment
  end
  self.sprite = SPR_FIREFLY_FADE
  self.frame = 0
  wait_anim()  -- until the glow is gone
  kill(self)
end

-- Collision: the serval caught it. A reaction: it runs on top of the
-- waiting create, which carries on (until the kill ends it).
function Firefly:collision(serval)
  score = score + 1
  print_score()
  play_sound(SND_CHIME)
  -- A sparkle where it was: 16x16, centered on the 8x8 firefly.
  spawn(Sparkle, self.x - 4, self.y - 4)
  -- The serval turns to face it: flipped (facing left) if the firefly's
  -- middle is left of the serval's.
  local flags = serval.flags & ~SPRITE_FLIP_H
  if self.x + (4 - SERVAL_BODY_W // 2) < serval.x then
    flags = flags | SPRITE_FLIP_H
  end
  serval.flags = flags
  -- Every tenth: a jingle, and fireflies come a little faster.
  if score % 10 == 0 then
    play_sound(SND_JINGLE)
    if spawn_max > SPAWN_MAX_FLOOR then
      spawn_min = spawn_min - SPAWN_MIN_STEP
      spawn_max = spawn_max - SPAWN_MAX_STEP
    end
  end
  kill(self)  -- its destroy runs in this phase
end

-- Destroy (caught or faded): one fewer alive.
function Firefly:destroy()
  live = live - 1
end

-- Sparkle and the resting serval ----------------------------------------------

function Sparkle:create()
  self.depth = DEPTH_SPARKLE
  self.flags = random_range(0, SPRITE_FLIP_H | SPRITE_FLIP_V)  -- no two bursts alike
end

-- Animation End: its one-shot animation finished.
function Sparkle:anim_end()
  kill(self)
end

function Resting:create()
  self.depth = DEPTH_SERVAL
end

-- The HUD ---------------------------------------------------------------------

-- The score, over the last round's (0 after 23 would leave a 3 behind).
function print_score()
  print(SCORE_COL, 0, "   ")
  print(SCORE_COL, 0, score)
end

-- The seconds left (9 after 10 would leave a 0 behind).
function print_time()
  print(TIME_COL, 0, "  ")
  print(TIME_COL, 0, time)
end
