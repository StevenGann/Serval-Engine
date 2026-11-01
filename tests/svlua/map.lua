-- The map and text (SYS MAP_* and TEXT_*): a platformer's player breaking
-- bricks overhead, collecting gems and finding spikes underfoot, by
-- metatile (map_cell, map_set_cell) and by world pixel (map_collision_at,
-- map_tags_in); MT_* and TAG_* are the game's own constants. The HUD is
-- cleared, shadowed and centered.
Player = object { components = C_POS }

local TITLE <const> = "SERVAL DASH"

gems = 0

function Player:create()
  text_clear()
  text_set_shadow(true)
  text_print_centered(0, TITLE)
end

function Player:step()
  local mx = math.floor(self.x) // 16
  local my = math.floor(self.y) // 16
  if map_cell(mx, my - 1) == MT_BRICK then
    map_set_cell(mx, my - 1, MT_EMPTY)
  end
  if map_tags_in(mx * 16, my * 16, 16, 16) & TAG_GEM ~= 0 then
    map_set_cell(mx, my, MT_EMPTY)
    gems = gems + 1
  end
  if map_collision_at(mx * 16 + 8, my * 16 + 16) & TAG_SPIKES ~= 0 then
    text_set_shadow(false)
    text_print_centered(10, "OUCH")
  end
end
