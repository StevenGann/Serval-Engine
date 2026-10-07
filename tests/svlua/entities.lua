-- Entities: properties and instance fields (GETP, SETP), spawn and kill,
-- waits, the engine functions (SYS), print, and loops over instances (NEXTI).
Bullet = object { components = C_POS | C_VEL | C_SPR, sprite = SPR_BULLET }
Enemy = object { components = C_POS | C_SPR | C_BODY | C_GAME(2), sprite = SPR_ENEMY }

hits = 0
target = none

function Enemy:create()
  self.hp = 3                        -- an instance field
  self.speed = 0.75                  -- another, fixed
  self.body_w = 16
  self.tags = self.tags | 1
  path_start(self, 0, PATH_MIRROR_X)
  wait_move()
  wait(30)
  wait_anim()
  path_stop(self)
end

function Enemy:collision(bullet)
  self.hp = self.hp - 1
  bullet.frame = self.frame + 1
  if self.hp <= 0 then
    kill(self)
    hits = hits + 1
  end
  kill(bullet)
end

function Enemy:step()
  self.x = self.x + self.speed
  self.anim_time = 0
  if button_pressed(BUTTON_A) then
    target = spawn(Bullet, self.x, self.y - 8)
    target.vy = -2
    play_sound(SND_SHOOT)
  end
  if button_down(BUTTON_B) and target ~= none then
    camera_set(math.floor(target.x) - 120, 0)
  end
  print(0, 0, "HITS")
  print(5, 0, hits, 3)
  local n = random_range(1, 6)
  brightness(n - 16)
end

function Bullet:anim_end()
  for e in instances(Enemy) do
    if e.hp > 2 then e.hp = 2 end
  end
  music_play(SONG_WIN)
  music_pause()
  music_resume()
  music_stop()
end
