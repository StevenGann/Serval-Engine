-- Globals with initial values, set by Init:room_start before its own code
-- (the VM zeroes globals when it loads a blob), and top-level constants.
Init = object {}

local LIVES <const> = 3
local SPEED <const> = 1.25
local ORIGIN <const> = FIELD_TOP + 8
local HALF <const> = SCREEN_W // 3     -- not computed by the assembler: inline
local GREETING <const> = "HELLO" .. ", " .. LIVES

lives = LIVES
speed = SPEED
alive = true
hero = none
origin = ORIGIN

function Init:room_start()
  print(1, 1, GREETING)
  speed = speed * 2
  origin = origin + HALF
end
