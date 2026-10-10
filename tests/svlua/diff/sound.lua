-- Sound: tracker music and sampled effects against the runner's (and the
-- stub's) stand-in for the mixer. The calls' results steer the script:
-- booleans in conditions, handles kept in globals and instance fields and
-- passed back, a fresh handle each frame; pitches fixed and integer; and
-- values outside C's ranges, which the VM clamps (warning once per kind) or
-- maps (an ID past a u16 to 65535, a handle to 0, a negative speed to 1).
-- diff: frames=6 start=Jukebox attach=Ship warnings=allowed
Jukebox = object {}
Ship = object { components = C_POS }

local theme <const> = 0
local jump <const> = 1
local hum <const> = 2

engine = 0
score = 0
heard = 0

function Jukebox:room_start()
  music_play(theme, true)
  engine = sfx_play_ex(hum, 160, -40, 0.75, 2)
  wait(1)
  music_pause()
  if music_paused() and music_playing() then score = score + 1 end
  wait(1)
  if music_paused() then music_resume() end
  music_set_volume(200)
  music_set_speed(125)
  if not music_paused() then score = score + 2 end
  wait(1)
  music_set_volume(300)
  music_set_speed(-5)
  sfx_set_volume(-20)
  music_play(70000, false)
  if sfx_playing(engine) then score = score + 4 end
  sfx_stop(engine)
  if not sfx_playing(engine) then score = score + 8 end
  wait(1)
  sfx_stop_all()
  music_stop()
  if not music_playing() then score = score + 16 end
end

function Ship:step()
  self.shot = sfx_play(jump)
  if sfx_playing(self.shot) then heard = heard + 1 end
  local h = sfx_play_ex(jump, 255, 300, 2, 999)
  sfx_stop(h)
  if sfx_playing(h) then heard = heard + 1000 end
  self.last = h
  sfx_stop(-1)
end
