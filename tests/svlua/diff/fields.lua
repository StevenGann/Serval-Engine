-- Instance fields of every type (integer, fixed, boolean, entity; unset
-- ones read as 0, 0.0, false and none), shared by name across objects,
-- read through other entities; the engine's properties, stored in their
-- arrays' types (frame wraps at 8 bits, depth is signed 16, flags and angle
-- unsigned 16, scale fixed point in 8.8, tags the game components).
-- diff: frames=3 start=Probe attach=Hero@8,16 attach=Pet@30,40
Probe = object {}
Hero = object { components = C_POS | C_VEL | C_SPR | C_BODY | C_ANIM | C_GAME(1), sprite = 2 }
Pet = object { components = C_POS | C_SPR | C_GAME(3) | C_GAME(14), sprite = 5 }

hp_seen = 0
fast_seen = 0.0
ok = false
best = none
tags_seen = 0

function Hero:create()
  self.hp = 10
  self.speed = 1.5
  self.awake = true
  self.frame = 300
  self.depth = -40000
  self.flags = 70000
  self.angle = -1
  self.scale = 1.25
  self.body_w = 260
  self.body_h = 12
  self.anim_time = 9
  self.anim_step = 2
  self.vx = -0.75
end

function Pet:create()
  self.hp = self.hp + 3
  self.tags = self.tags | 2
  wait(1)
  for h in instances(Hero) do
    self.friend = h
    h.hp = h.hp - 1
    hp_seen = h.hp + self.hp
    fast_seen = h.speed * 2 + h.vx
    ok = h.awake and not self.awake
    best = self.friend
    tags_seen = h.tags * 100 + self.tags
  end
  self.x = self.x + self.scale
end

function Probe:room_start()
  wait(2)
  for p in instances(Pet) do
    p.scale = 0.5
    p.speed = p.speed + 0.25
  end
end
