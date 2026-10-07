-- Globals start at their initial values: the blob carries them (header flag
-- bit 0) and vm_load sets them, so no code runs for them. An object named
-- Init is an object like any other. Top-level constants: numbers become
-- .const lines, strings are folded.
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
count = 0                             -- 0: no initial value needed
lowest = math.mininteger
highest = -math.maxinteger
drift = -0.5
spread = (FIELD_TOP + 8) * 2          -- with spaces: parenthesized in .globals
local level = 2                       -- a top-level local is a global too
local paused = false

function Init:room_start()
  text_print(1, 1, GREETING)
  speed = speed * 2
  origin = origin + HALF
  if not paused then level = level + count end
end
