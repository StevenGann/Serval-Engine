-- The map and text: a platformer's runner on the runner's (and the stub's)
-- test playfield, 8 x 4 metatiles: bricks (tag 0) at (1, 1) and (2, 1), gems
-- (tag 1) at (3, 0) and (5, 1), spikes (solid, tag 2) and lava (empty, tag
-- 3) in the floor, row 3. The queries' results steer the script: bricks broken by
-- map_set_cell, gems collected and counted, hazards found under the feet,
-- the collision outside the map (solid to the sides, empty above and below),
-- rectangles clipped at the map's edges, a cell set back to its original,
-- and a metatile the layer doesn't have (the VM warns; nothing changes).
-- diff: frames=9 start=Hud attach=Runner@0,32 map warnings=allowed
Hud = object {}
Runner = object { components = C_POS }

local BRICK <const> = 2
local GEM <const> = 3
local TAG_BRICK <const> = 1 << 4
local TAG_GEM <const> = 1 << 5
local TAG_SPIKES <const> = 1 << 6
local TAG_LAVA <const> = 1 << 7
local TITLE <const> = "SERVAL DASH"

gems = 0
bricks = 0
hurt = 0
solid = 0
edges = 0
outside = 0

function Hud:room_start()
  text_clear()
  text_set_shadow(true)
  text_print_centered(0, TITLE)
  text_print_centered(19, "GEMS " .. "0")
  wait(4)
  text_set_shadow(false)
  text_print_centered(10, "")
  -- the collision outside the map
  if map_collision_at(-1, 20) == 1 and map_collision_at(128, 20) == 1 then outside = 1 end
  if map_collision_at(20, -1) == 0 and map_collision_at(20, 64) == 0 then outside = outside + 2 end
  if map_collision_at(-16, -16) == 1 then outside = outside + 4 end
  -- rectangles clipped at the edges, none with no area
  edges = map_tags_in(-1000, -1000, 1060, 1010) -- row 0, columns 0-3: the gem
  if map_tags_in(0, 0, 0, 16) == 0 and map_tags_in(0, 0, 16, -1) == 0 then edges = edges + 1 end
  if map_tags_in(math.mininteger, 0, math.maxinteger, 64) == 0 then edges = edges + 2 end
  if map_tags_in(47, 63, math.maxinteger, math.maxinteger) == TAG_SPIKES | TAG_LAVA then
    edges = edges + 4
  end
  -- a cell set back to its original, a metatile the layer doesn't have
  map_set_cell(0, 0, GEM)
  map_set_cell(0, 0, 0)
  map_set_cell(7, 2, 70000)
  map_set_cell(7, 2, -1)
  map_set_cell(8, 0, BRICK)
  text_print_centered(19, "DONE")
end

-- Runs right along row 2, a metatile a frame and off the map's right edge
-- on the last frame, looking at what is overhead (bricks it breaks, gems it
-- collects) and underfoot (hazards, and whether the floor is solid).
function Runner:step()
  local mx = math.floor(self.x) // 16
  local my = math.floor(self.y) // 16
  if map_cell(mx, my - 1) == BRICK then
    map_set_cell(mx, my - 1, 0)
    bricks = bricks + 1
  end
  if map_tags_in(mx * 16, my * 16 - 16, 16, 16) & TAG_GEM ~= 0 then
    map_set_cell(mx, my - 1, 0)
    gems = gems + 1
    text_print_number(5, 19, gems)
  end
  local below = map_tags_in(mx * 16 + 4, my * 16 + 16, 8, 1)
  if below & TAG_SPIKES ~= 0 then hurt = hurt + 1 end
  if below & TAG_LAVA ~= 0 then hurt = hurt + 100 end
  if map_collision_at(mx * 16 + 8, my * 16 + 16) & 15 == 1 then solid = solid + 1 end
  if map_tags_in(mx * 16, my * 16 - 16, 16, 16) & TAG_BRICK ~= 0 then bricks = bricks + 1000 end
  self.x = self.x + 16
end
