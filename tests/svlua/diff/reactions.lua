-- Behaviours with reactions on top: a Collision reaction and Step
-- reactions run while each instance's Create behaviour waits, change state
-- the behaviour reads when it resumes, and leave its wait and its locals
-- as they were; the Destroy reaction of a waiting behaviour, then its halt.
-- Collisions come from the runner's pairs (body_overlap after movement).
-- diff: frames=40 start=Game movement collide=Coin:Hero attach=Hero@0,40 attach=Coin@30,40 attach=Coin@60,40 attach=Coin@200,90
Game = object {}
Hero = object { components = C_POS | C_VEL | C_BODY }
Coin = object { components = C_POS | C_BODY }

score = 0
hits = 0
steps = 0
coins_left = 0
spins = 0
over = false

function Hero:create()
  self.body_w = 8
  self.body_h = 8
  self.vx = 2.5
end

function Hero:step()
  steps = steps + 1
  if self.x > 100 then self.vx = 0.0 end
end

function Coin:create()
  self.body_w = 6
  self.body_h = 6
  local turns = 0
  while true do
    turns = turns + 1
    self.spin = turns
    wait(3)
    if self.taken then
      spins = spins + turns
      kill(self)
      return
    end
  end
end

function Coin:collision(hero)
  if not self.taken then
    self.taken = true
    hits = hits + 1
    score = score + math.floor(hero.x)
  end
end

function Coin:destroy()
  score = score + 1000
end

function Game:room_start()
  wait(30)
  for c in instances(Coin) do coins_left = coins_left + 1; kill(c) end
  over = true
end
