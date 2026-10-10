-- sampled_audio_vm.lua: a script's tracker music and sampled effects,
-- through the VM's SYS calls (17 to 30) to Maxmod, with the sound bank of
-- the sampled audio test ROM (test_bank.h: tone.mod, a held note; beep.wav,
-- 0.1 s at 1000 Hz; hum.wav, a looped 500 Hz). Each object is a thread
-- tests/rom/sampled_audio_vm_tests.c starts for one case and checks, from C,
-- what it played; the globals keep what the script's queries answered.
Music = object {}
Louder = object {}
Effects = object {}
Stop = object {}
Strays = object {}
Pads = object {}

playing = false -- music_playing() after music_play
paused = false  -- music_paused() after music_pause
resumed = true  -- music_paused() after music_resume
ended = true    -- music_playing() after music_stop
hum = 0         -- the hum's handle
beep = 0        -- the beep's handle
humming = false -- sfx_playing(hum)
beeping = false -- sfx_playing(beep), a frame after it began
stopped = true  -- sfx_playing(hum) after sfx_stop
strays = 0      -- what sfx_play of an ID no bank has returned
recent = array(16)

-- Plays the module looped, pauses it, resumes it two frames later and
-- stops it two frames after that.
function Music:room_start()
  music_play(MOD_TONE, true)
  playing = music_playing()
  music_pause()
  paused = music_paused()
  wait(2)
  music_resume()
  resumed = music_paused()
  wait(2)
  music_stop()
  ended = music_playing()
end

-- The module at half volume, then at 300, which C would wrap to 44 and
-- the VM clamps to 255: full volume.
function Louder:room_start()
  music_play(MOD_TONE, true)
  music_set_volume(128)
  wait(10)
  music_set_volume(300)
end

-- The hum an octave up, all to the left, and the beep at its own pitch.
function Effects:room_start()
  hum = sfx_play_ex(SFX_HUM, 255, -128, 2.0, 0)
  humming = sfx_playing(hum)
  beep = sfx_play(SFX_BEEP)
  wait(1)
  beeping = sfx_playing(beep)
end

function Stop:room_start()
  sfx_stop(hum)
  stopped = sfx_playing(hum)
end

-- The jukebox's pads (examples/jukebox/pads.lua, with this bank's samples)
-- as a frame finds them with no button pressed, for their cost: one query
-- for all the buttons, and one of the 16 handles checked.
local any_pad <const> = BUTTON_START | BUTTON_A | BUTTON_B | BUTTON_L | BUTTON_R

local function pressed()
  if button_pressed(BUTTON_START) then
    if music_paused() then music_resume() else music_pause() end
  end
  if button_pressed(BUTTON_A) then recent[1] = sfx_play(SFX_BEEP) end
  if button_pressed(BUTTON_B) then
    recent[2] = sfx_play_ex(SFX_BEEP, 220, -96, random_range(205, 320) / 256, 1)
  end
  if button_pressed(BUTTON_L) then
    recent[3] = sfx_play_ex(SFX_BEEP, 255, 0, random_range(230, 282) / 256, 2)
  end
  if button_pressed(BUTTON_R) then hum = sfx_play_ex(SFX_HUM, 200, 0, 1.0, 1) end
end

function Pads:room_start()
  local scan, seen = 1, 0
  while true do
    if button_pressed(any_pad) then pressed() end
    if hum ~= 0 and not button_down(BUTTON_R) then
      sfx_stop(hum)
      hum = 0
    end
    if sfx_playing(recent[scan]) then seen = seen + 1 end
    if scan == #recent then
      strays = seen
      scan, seen = 1, 0
    else
      scan = scan + 1
    end
    wait(1)
  end
end

-- An ID past 65535, which reaches C as 65535 (no bank has it), and a
-- handle past 65535, which reaches it as 0 and stops nothing.
function Strays:room_start()
  strays = sfx_play(65536 + SFX_BEEP)
  sfx_stop(65536 + hum)
end
