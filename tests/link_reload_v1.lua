-- link_reload_v1.lua: the running version of the hot reload tests' script
-- (tests/link_reload_tests.c: the debug link's agent, its reload with plans,
-- docs/debug-link.md#hot-reload). link_reload_v2.lua is its edit, built twice
-- (with and without KEEP_LAYOUT, this file's .svdbg); link_reload_plan.py
-- plans v1 -> v2 from both builds' maps with tools/svlink.py's planner, as a
-- debugger does. What each object is for:

Doomed = object { components = C_POS }  -- gone in v2: its instance is detached, no Destroy
Walker = object { components = C_POS }  -- waits inside stroll(), which moves in v2: kept
Counter = object { components = C_POS } -- its create changes in v2: restarted, fields kept
Clock = object {}                       -- a thread on `ticks`: kept only with KEEP_LAYOUT
Pacer = object {}                       -- a thread whose room_start changes: restarted

score = 10
lives = 3
ticks = 0
starts = 0
destroyed = 0
history = array(4)
steps = { 5, 6, 7 }

function Doomed:create()
  self.fate = lives
  while true do
    wait(1)
  end
end

function Doomed:destroy()
  destroyed = destroyed + 1
end

-- The walk, in a function, so that the waiting behaviour's stack has a
-- return point (in Walker:create). v2 moves it, its code unchanged.
function stroll(who, pace)
  local walked = 0
  while true do
    wait(pace)
    who.x = who.x + 1
    walked = walked + 1
  end
end

function Walker:create()
  stroll(self, 1)
end

function Walker:step()
  self.seen = self.seen + steps[2]
end

function Counter:create()
  starts = starts + 1
  self.runs = self.runs + 1
  while true do
    wait(1)
    self.x = self.x + 2
  end
end

function Clock:room_start()
  while true do
    wait(1)
    ticks = ticks + 1
  end
end

function Pacer:room_start()
  starts = starts + 1
  while true do
    wait(2)
    score = score + 1
    history[1] = score
  end
end
