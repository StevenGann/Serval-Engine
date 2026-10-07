-- Spawning and killing: Create runs in the drain after the spawn, before
-- the instance's first Step (a spawn from a Step reaction runs its Create
-- in the second drain and steps the next frame); kill queues Destroy,
-- which runs the Destroy reaction, then halts the behaviour and destroys
-- the entity; killing twice is harmless; a freed slot comes back last, with
-- a new generation in the handle; a spawned instance with C_SPR gets its
-- object's sprite.
-- diff: frames=8 start=Boss
Boss = object {}
Minion = object { components = C_POS }
Spark = object { components = C_POS | C_SPR, sprite = 7 }

order = array(40)
n = 0
first = none
second = none
reborn = none
waited = 0

function log(code)
  n = n + 1
  order[n] = code
end

function Boss:room_start()
  first = spawn(Minion, 1, 0)
  second = spawn(Minion, 2, 0)
  log(1)
  wait(1)
  log(2)
  kill(first)
  kill(first)
  log(3)
  wait(1)
  reborn = spawn(Minion, 3, 0)
  kill(second)
  wait(2)
  for m in instances(Minion) do kill(m) end
  log(4)
end

function Minion:create()
  log(10 + math.floor(self.x))
  wait(5)
  waited = waited + 1
end

function Minion:step()
  log(20 + math.floor(self.x))
  if self.x == 2 and n < 12 then spawn(Spark, 9, 0) end
end

function Minion:destroy()
  log(30 + math.floor(self.x))
end

function Spark:create()
  log(40)
end

function Spark:step()
  log(50)
  kill(self)
end
