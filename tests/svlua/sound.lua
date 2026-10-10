-- Sound: tracker music and sampled effects (SYS MUSIC_* and SFX_*). IDs are
-- the sound bank's MOD_* and SFX_* constants; an effect's handle is an
-- integer, kept in a global or an instance field and passed back.
Ship = object { components = C_POS }

engine = 0 -- the engine hum's handle

function Ship:create()
  music_play(MOD_THEME, true)
  music_set_volume(192)
  engine = sfx_play_ex(SFX_ENGINE, 128, -32, 0.75, 1)
end

function Ship:step()
  if button_pressed(BUTTON_A) then
    self.shot = sfx_play(SFX_JUMP)
  end
  if button_pressed(BUTTON_B) and sfx_playing(self.shot) then
    sfx_stop(self.shot)
  end
  if button_pressed(BUTTON_START) then
    if music_paused() then music_resume() else music_pause() end
  end
  if not music_playing() then
    music_play(MOD_THEME, false)
    music_set_speed(125)
  end
  sfx_play_ex(SFX_JUMP, 255, 0, 2, 0) -- an integer pitch; the handle unused
end

function Ship:destroy()
  sfx_stop(engine)
  sfx_stop_all()
  sfx_set_volume(0)
  music_stop()
end
