-- link_debug.lua: a script for tests/link_vm_tests.c (the debug link's agent,
-- its VM part: breakpoints, stepping, contexts and frames, faults,
-- debug_break()). A line the tests put a site on ends in "-- @site NAME":
-- tests/link_sites.py finds its statement start's pc in the maps
-- serval_add_script() writes beside the blob (.svmap, .svdbg), with every
-- function's statement starts and locals, as a debugger's planner does.
--
-- Looper, Walker and the fault objects run as the tests start them: Looper,
-- Deep, Heavy and Spinner as threads (vm_start), Walker attached to an
-- entity (its create waits; its step runs on top every frame).

Looper = object {}                     -- a loop, calls, a local, debug_break()
Walker = object { components = C_POS } -- a waiting behaviour with a Step reaction on top
Deep = object {}                       -- calls itself past VM_CALLS
Heavy = object {}                      -- calls itself with locals past VM_STACK
Spinner = object {}                    -- loops without waiting: over its budget

total = 0
result = 0
steps = 0
waited = 0
moved = 0
spins = 0

function add(a, b)
  local sum = a + b               -- @site ADD_SUM
  return sum                      -- @site ADD_RETURN
end

function twice(n)
  local once = add(n, n)          -- @site TWICE_ONCE
  return add(once, once)          -- @site TWICE_RETURN
end

function Looper:create()
  for i = 1, 10 do
    total = total + i             -- @site LOOP_BODY
  end
  result = twice(total)           -- @site LOOPER_TWICE
  local extra = 5                 -- @site LOOPER_EXTRA
  result = result + extra         -- @site LOOPER_ADD
  debug_break()                   -- @site LOOPER_BREAK
  total = total + 1               -- @site LOOPER_LAST
end

function Walker:create()
  local pace = 2                  -- a cell beneath the reactions on top
  while true do
    wait(pace)                    -- @site WALKER_WAIT
    waited = waited + 1           -- @site WALKER_COUNT
  end
end

function Walker:step()
  steps = steps + 1               -- @site WALKER_STEP
  moved = add(moved, 1)           -- @site WALKER_MOVE
end

function deep(n)
  return deep(n + 1)              -- @site DEEP_CALL
end

-- 20 cells a call: the fourth call's ENTER doesn't fit VM_STACK (64).
function heavy(n)
  local a = n
  local b = a
  local c = b
  local d = c
  local e = d
  local f = e
  local g = f
  local h = g
  local i = h
  local j = i
  local k = j
  local l = k
  local m = l
  local o = m
  local p = o
  local q = p
  local r = q
  local s = r
  local t = s
  return heavy(t + 1)
end

function Deep:create()
  result = deep(0)                -- @site DEEP_START
end

function Heavy:create()
  result = heavy(1)
end

function Spinner:create()
  while true do
    spins = spins + 1             -- @site SPIN
  end
end

Counter = object {}                    -- counts down by calling itself: steps at several depths
counted = 0

function count(n)
  if n == 0 then return 0 end     -- @site COUNT_BASE
  local below = count(n - 1)      -- @site COUNT_CALL
  return below + 1                -- @site COUNT_RETURN
end

function Counter:create()
  counted = count(3)              -- @site COUNTER_START
end

Burst = object {}                      -- spawns six Dots at once: six Creates queued in a phase
Dot = object { components = C_POS }

function Burst:create()
  for i = 1, 6 do
    spawn(Dot, 0, 0)
  end
end
