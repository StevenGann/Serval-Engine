-- An entity's object as a value: compared with objects' names and with
-- other entities' objects (== and ~=), in a Collision reaction telling what
-- it met, in an instances() loop, and right after a spawn; two names
-- compare as constants.
-- diff: frames=4 attach=Hero@10,10 attach=Coin@12,12 attach=Spike@14,14 attach=Coin@60,60
-- diff: attach=Hero@13,13 collide=Hero:Coin collide=Hero:Spike collide=Hero:Hero
Hero = object { components = C_POS | C_BODY }
Coin = object { components = C_POS | C_BODY }
Spike = object { components = C_POS | C_BODY }
Gem = object { components = C_POS | C_BODY }

coins = 0
spikes = 0
heroes = 0
others = 0
kinds = 0
gem_is_gem = false
names = false

function Hero:create() self.body_w = 8; self.body_h = 8 end
function Coin:create() self.body_w = 8; self.body_h = 8 end

function Spike:create()
  self.body_w = 8; self.body_h = 8
  local g = spawn(Gem, 100, 100)
  gem_is_gem = g.object == Gem and g.object ~= Spike
end

function Hero:collision(other)
  if other.object == Coin then
    coins = coins + 1
  elseif other.object == Spike then
    spikes = spikes + 1
  elseif other.object == self.object then
    heroes = heroes + 1
  else
    others = others + 1
  end
end

function Gem:step()
  kinds = 0
  for c in instances(Coin) do
    if c.object == Coin and c.object ~= self.object then kinds = kinds + 1 end
  end
  names = Coin ~= Spike and not (Gem == Hero)
end
