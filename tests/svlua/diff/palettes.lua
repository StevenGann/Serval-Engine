-- Palette writes: sprite_set_colors and tileset_set_colors with ROM tables
-- and RAM arrays, the colors made with COLOR_RGB and with integer
-- arithmetic: a cycle that rotates a RAM array, a fade toward white mixed
-- channel by channel, and values past 16 bits, of which a call takes the low
-- 16 (a Color). Calls with a count of 0 and with fewer colors than the array
-- holds.
-- diff: frames=6 start=Water start=Flash
Water = object {}
Flash = object {}

water = { COLOR_RGB(0, 64, 160), COLOR_RGB(0, 96, 200), COLOR_RGB(32, 160, 248),
          COLOR_RGB(200, 232, 255) }
cycle = array(4)
odd = { 0x18001, -1, 0x7FFF }        -- s32: their low 16 bits
local base <const> = COLOR_RGB(248, 64, 32)
faded = array(2)
shift = 0

-- c mixed toward white by t sixteenths, each channel rounded down.
local function toward_white(c, t)
  local r, g, b = c & 31, (c >> 5) & 31, (c >> 10) & 31
  r = (r * (16 - t) + 31 * t) // 16
  g = (g * (16 - t) + 31 * t) // 16
  b = (b * (16 - t) + 31 * t) // 16
  return r | (g << 5) | (b << 10)
end

function Water:room_start()
  while true do
    for i = 1, #water do
      cycle[i] = water[(i + shift - 1) % #water + 1]
    end
    tileset_set_colors(1 * 16 + 1, cycle, #cycle)
    shift = shift + 1
    wait(1)
  end
end

function Flash:room_start()
  sprite_set_colors(2, 0, odd, #odd)
  sprite_set_colors(2, 5, water, 0)
  wait(1)
  for t = 0, 16, 4 do
    faded[1] = toward_white(base, t)
    faded[2] = toward_white(water[1], t)
    sprite_set_colors(0, 17, faded, 2)
    tileset_set_colors(3, faded, 1)
    wait(1)
  end
end
