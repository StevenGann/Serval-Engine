-- An object's declared fields and their defaults (Studio Advance's
-- per-instance settings): a plain spawn gives the instance the defaults, a
-- spawn with a table replaces some of them and may set fields no object
-- declares; every value is in place when the instance's Create runs, the
-- values that call a function (which may wait) included; the entity spawn
-- returns is the one the fields were set on; a default of 0 and a field
-- shared by two objects with different defaults.
-- diff: frames=5 start=Factory
Factory = object {}
Brick = object { components = C_POS, fields = { kind = 2, hits = MAX_ENT // 32, bonus = 0 } }
Gem = object { components = C_POS, fields = { kind = 7, hits = 1 } }

seen = array(12)
n = 0
last = none

function later(v)
  wait(1)
  return v
end

function Brick:create()
  n = n + 1
  seen[n] = self.kind * 1000 + self.hits * 100 + self.bonus * 10 + self.extra
end

function Gem:create()
  n = n + 1
  seen[n] = -(self.kind * 100 + self.hits * 10 + self.extra)
end

function Factory:room_start()
  local a = spawn(Brick, 1, 1)                                   -- kind 2, hits 4, bonus 0
  local b = spawn(Brick, 2, 2, { kind = 5 })                      -- hits still 4
  local c = spawn(Brick, 3, 3, { hits = n + 9, extra = 1, bonus = a.kind })
  local g = spawn(Gem, 4, 4, { extra = 3 })
  seen[10] = a.kind * 100 + b.kind * 10 + c.hits                  -- read back right away
  wait(1)                                                        -- the Creates ran
  local d = spawn(Brick, 5, 5, { hits = later(n), kind = n })    -- later waits a frame
  seen[11] = d.hits * 10 + d.kind
  last = d
  wait(1)
  for e in instances(Brick) do
    if e.kind == 2 then kill(e) end
  end
end
