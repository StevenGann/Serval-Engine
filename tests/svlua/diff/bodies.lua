-- The body's properties, as C's pools hold them: body_bounce and
-- body_friction integers wrapping at 8 bits, body_max_fall a speed in fixed
-- point stored in 16 unsigned bits, body_gravity signed 8 bits written with
-- BODY_GRAVITY(n) as in C, body_contact read-only (no physics runs here, so
-- it reads 0); read back through other entities and tested bit by bit.
-- diff: frames=3 attach=Ball@8,16 attach=Crate@30,40
Ball = object { components = C_POS | C_VEL | C_BODY }
Crate = object { components = C_POS | C_BODY | C_GAME(2) }

bounce_seen = 0
fall_seen = 0.0
gravity_seen = 0
floating = false
landed = true
contacts = 0

function Ball:create()
  self.body_w = 8
  self.body_h = 8
  self.body_bounce = 255   -- a perfect bounce
  self.body_friction = 300 -- wraps: 44
  self.body_max_fall = 2.75
  self.body_gravity = BODY_GRAVITY(8)
end

function Crate:create()
  self.body_bounce = -32         -- wraps: 224
  self.body_max_fall = 255.75    -- the largest a u16 holds
  self.body_gravity = BODY_GRAVITY(-112)
  wait(1)
  for b in instances(Ball) do
    bounce_seen = b.body_bounce + self.body_bounce
    fall_seen = b.body_max_fall * 2 + self.body_max_fall
    gravity_seen = b.body_gravity * 1000 + self.body_gravity
    floating = b.body_gravity == BODY_GRAVITY(0)
    landed = b.body_contact & MAP_CONTACT_FLOOR ~= 0
    contacts = b.body_contact | self.body_contact
    b.body_gravity = BODY_GRAVITY(0)
    b.body_max_fall = -1.0 -- wraps: 255.0
    b.body_friction = b.body_friction + 1
  end
  self.body_gravity = self.body_gravity - 1 -- wraps: 127, BODY_GRAVITY(143)
end
