-- link_probe.lua: a script for tests/link_tests.c (the debug link's agent).
-- Each frame the Probe's step copies every property of the entity in
-- `target` into the globals read_x to read_body_contact, as GETP reads them,
-- so the tests can compare them with what the agent's ENTITY reports and see
-- what its SET_PROPERTY wrote. Its create waits forever, so a Probe has a
-- behaviour (a context, which ENTITIES reports), and gives it two instance
-- fields, hp and mana.
Probe = object { components = C_POS }

target = none
read_x = 0.0
read_y = 0.0
read_vx = 0.0
read_vy = 0.0
read_sprite = 0
read_frame = 0
read_flags = 0
read_angle = 0
read_depth = 0
read_scale = 0.0
read_body_w = 0
read_body_h = 0
read_tags = 0
read_anim_time = 0
read_anim_step = 0
read_body_bounce = 0
read_body_friction = 0
read_body_max_fall = 0.0
read_body_gravity = 0
read_body_contact = 0
waited = 0 -- frames the Probe's create has waited

function Probe:create()
  self.hp = 7
  self.mana = -3
  while true do
    wait(1)
    waited = waited + 1
  end
end

function Probe:step()
  if target ~= none then
    read_x = target.x
    read_y = target.y
    read_vx = target.vx
    read_vy = target.vy
    read_sprite = target.sprite
    read_frame = target.frame
    read_flags = target.flags
    read_angle = target.angle
    read_depth = target.depth
    read_scale = target.scale
    read_body_w = target.body_w
    read_body_h = target.body_h
    read_tags = target.tags
    read_anim_time = target.anim_time
    read_anim_step = target.anim_step
    read_body_bounce = target.body_bounce
    read_body_friction = target.body_friction
    read_body_max_fall = target.body_max_fall
    read_body_gravity = target.body_gravity
    read_body_contact = target.body_contact
  end
end
