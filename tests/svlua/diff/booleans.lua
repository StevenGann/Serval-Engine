-- Booleans and short-circuit evaluation: and/or evaluate their right side
-- only when Lua does (the calls count it), not, comparisons as values,
-- booleans in globals, locals, fields and as function results.
-- diff: frames=3 start=Probe attach=Flag
Probe = object {}
Flag = object {}

calls = 0
r1 = false
r2 = true
r3 = false
r4 = false
seen = false
local flips = 0

function yes() calls = calls + 1; return true end
function no() calls = calls + 10; return false end
function both(a, b) return a and b end

function Probe:room_start()
  r1 = no() and yes()
  r2 = yes() or no()
  r3 = not (yes() and no()) or no()
  if no() or yes() then calls = calls + 100 end
  if yes() and no() then calls = calls + 1000 end
  r4 = both(3 > 2, 2 >= 2) and not (1 == 2)
  local t = calls > 120
  wait(1)
  for e in instances(Flag) do
    e.on = t and r2
    seen = e.on
  end
  wait(1)
  while not r1 do
    flips = flips + 1
    r1 = flips >= 3
  end
end
