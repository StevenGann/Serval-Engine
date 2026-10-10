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

-- An ID past 65535, which reaches C as 65535 (no bank has it), and a
-- handle past 65535, which reaches it as 0 and stops nothing.
function Strays:room_start()
  strays = sfx_play(65536 + SFX_BEEP)
  sfx_stop(65536 + hum)
end
