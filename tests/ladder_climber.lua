-- ladder_climber.lua: a map body that climbs the first ladder it touches, as
-- a scripted game would, reading MAP_CONTACT_LADDER from body_contact by its
-- C name (serval/map.h). tests/map_slope_tests.c attaches it at the foot of
-- a ladder and checks that it ends up standing on the ladder's top.
Climber = object { components = C_POS | C_VEL | C_BODY | C_MAPBODY }

touched = false -- it has touched a ladder
climbed = 0     -- frames it spent climbing
state = 0       -- 0 before the ladder, 1 climbing, 2 over its top

function Climber:create()
  self.body_w = 10
  self.body_h = 14
end

-- Climbs with no gravity while it touches the ladder; over its top, gravity
-- again: it lands on the top, which holds it up.
function Climber:step()
  local ladder = self.body_contact & MAP_CONTACT_LADDER ~= 0
  if ladder then
    touched = true
  end
  if state == 0 and ladder then
    state = 1
    self.body_gravity = BODY_GRAVITY(0)
  end
  if state == 1 then
    if ladder then
      self.vy = -1
      climbed = climbed + 1
    else
      state = 2
      self.body_gravity = BODY_GRAVITY(16)
      self.vy = 0
    end
  end
end
