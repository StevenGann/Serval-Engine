-- pads.lua: the jukebox's sound pads and its pause button, a script in the
-- Lua subset (docs/lua.md), compiled at build time (serval_add_script() in
-- examples/CMakeLists.txt). It plays the sound bank's samples and holds and
-- resumes its music through the VM's sound calls, naming the samples by the
-- bank's header (jukebox_bank.h); main.c keeps the menu, and shows
-- `playing` on its bottom line.
--
-- A frame with no button pressed costs a handful of ops: one query for all
-- the buttons, and one of the 16 handles checked.
Pads = object {}

local any_pad <const> = BUTTON_START | BUTTON_A | BUTTON_B | BUTTON_L | BUTTON_R

recent = array(16) -- the latest effects' handles (0 until there are 16)
next_slot = 1
playing = 0        -- how many of them still play
hum = 0            -- the engine hum's handle while R is held, else 0
laser_left = true  -- where the next laser goes

local function remember(sfx)
  recent[next_slot] = sfx
  next_slot = next_slot % #recent + 1
end

local function pressed()
  if button_pressed(BUTTON_START) then
    if music_paused() then music_resume() else music_pause() end
  end
  if button_pressed(BUTTON_A) then
    remember(sfx_play(SFX_COIN))
  end
  if button_pressed(BUTTON_B) then
    -- Panned left and right in turn, 0.8 to 1.25 times its pitch.
    local pan = 96
    if laser_left then pan = -96 end
    remember(sfx_play_ex(SFX_LASER, 220, pan, random_range(205, 320) / 256, 1))
    laser_left = not laser_left
  end
  if button_pressed(BUTTON_L) then
    -- Priority 2: it takes a channel from the others when all are busy.
    remember(sfx_play_ex(SFX_BOOM, 255, 0, random_range(230, 282) / 256, 2))
  end
  if button_pressed(BUTTON_R) then
    hum = sfx_play_ex(SFX_ENGINE, 200, 0, 1.0, 1) -- loops until stopped
    remember(hum)
  end
end

function Pads:room_start()
  local scan, seen = 1, 0
  while true do
    if button_pressed(any_pad) then pressed() end
    if hum ~= 0 and not button_down(BUTTON_R) then
      sfx_stop(hum)
      hum = 0
    end
    -- One handle a frame: the whole count every 16 frames.
    if sfx_playing(recent[scan]) then seen = seen + 1 end
    if scan == #recent then
      playing = seen
      scan, seen = 1, 0
    else
      scan = scan + 1
    end
    wait(1)
  end
end
