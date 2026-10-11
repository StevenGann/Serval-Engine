-- scriptmark.lua: the script benchmark's scene (bench.c runs it). A busy
-- scene in the style of what Studio Advance's event editor emits, made busy:
-- behaviours that wait (the wanderers, the puffs, the Director), a Step
-- reaction on every moving entity (each a call of one shared function),
-- collisions that kill and spawn, a HUD number twice a second, random
-- numbers, ROM tables and instance fields. About 630 ops a frame.
--
-- Changing it changes the benchmark's workload: record a new baseline
-- (docs/development.md#script-benchmark).

Director = object {}
Wanderer = object { components = C_POS | C_VEL | C_SPR | C_BODY | C_WANDERER,
                    sprite = SPR_WANDERER }
Bouncer = object { components = C_POS | C_VEL | C_SPR | C_BODY | C_BOUNCER,
                   sprite = SPR_BOUNCER }
Food = object { components = C_POS | C_SPR | C_BODY | C_FOOD, sprite = SPR_FOOD }
Puff = object { components = C_POS | C_SPR, sprite = SPR_PUFF }

eaten = 0
food_count = 0
puffs = 0
resting = 0

DIR_X = { 1.0, 0.75, 0.0, -0.75, -1.0, -0.75, 0.0, 0.75 }
DIR_Y = { 0.0, 0.75, 1.0, 0.75, 0.0, -0.75, -1.0, -0.75 }

-- Keeps an entity inside the field: back inside, its velocity reversed.
function bounce(e)
  if e.x < FIELD_L and e.vx < 0 then
    e.x = FIELD_L
    e.vx = -e.vx
  elseif e.x > FIELD_R and e.vx > 0 then
    e.x = FIELD_R
    e.vx = -e.vx
  end
  if e.y < FIELD_T and e.vy < 0 then
    e.y = FIELD_T
    e.vy = -e.vy
  elseif e.y > FIELD_B and e.vy > 0 then
    e.y = FIELD_B
    e.vy = -e.vy
  end
end

-- Wanders: a direction from the tables at one of four speeds, held for 15-45
-- frames; every 20 legs it rests for half a second.
function Wanderer:create()
  self.body_w = 8
  self.body_h = 8
  self.energy = 20
  while true do
    local d = random_range(1, 8)
    local speed = 0.5 + random_range(0, 3) * 0.25
    self.vx = DIR_X[d] * speed
    self.vy = DIR_Y[d] * speed
    wait(random_range(15, 45))
    self.energy = self.energy - 1
    if self.energy <= 0 then
      self.vx = 0.0
      self.vy = 0.0
      resting = resting + 1
      wait(30)
      resting = resting - 1
      self.energy = 20
    end
  end
end

function Wanderer:step()
  bounce(self)
end

function Wanderer:collision(food)
  eaten = eaten + 1
  self.energy = self.energy + 5
  kill(food)
end

function Bouncer:create()
  self.body_w = 8
  self.body_h = 8
  local d = random_range(1, 8)
  self.vx = DIR_X[d] * 1.5
  self.vy = DIR_Y[d] * 1.5
end

-- Every 64 frames a bouncer leaves a puff behind, while there are fewer than
-- MAX_PUFFS.
function Bouncer:step()
  bounce(self)
  self.age = self.age + 1
  if self.age % 64 == 0 and puffs < MAX_PUFFS then
    spawn(Puff, self.x, self.y)
  end
end

function Food:create()
  self.body_w = 4
  self.body_h = 4
end

function Food:destroy()
  food_count = food_count - 1
end

function Puff:create()
  puffs = puffs + 1
  wait(random_range(10, 20))
  kill(self)
end

function Puff:destroy()
  puffs = puffs - 1
end

-- The scene: the wanderers, a frame later the bouncers, then food whenever
-- there is less than MAX_FOOD, and the HUD (eaten, resting) twice a second.
function Director:room_start()
  for i = 1, WANDERERS do
    spawn(Wanderer, random_range(FIELD_L, FIELD_R), random_range(FIELD_T, FIELD_B))
  end
  wait(1)
  for i = 1, BOUNCERS do
    spawn(Bouncer, random_range(FIELD_L, FIELD_R), random_range(FIELD_T, FIELD_B))
  end
  local tick = 0
  while true do
    if food_count < MAX_FOOD then
      spawn(Food, random_range(FIELD_L, FIELD_R), random_range(FIELD_T, FIELD_B))
      food_count = food_count + 1
    end
    tick = tick + 1
    if tick % 30 == 0 then
      text_print_number(1, 0, eaten, 5)
      text_print_number(8, 0, resting, 2)
    end
    wait(1)
  end
end
