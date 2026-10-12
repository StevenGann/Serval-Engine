-- link_reload_v2.lua: the edit of link_reload_v1.lua that the hot reload
-- tests reload (tests/link_reload_tests.c). Against v1: Doomed is gone, so
-- the others renumber; a global (`bonus`) and an array (`extra`) are added in
-- the middle, which renumbers the later ones unless the build keeps v1's
-- layout (KEEP_LAYOUT); stroll() moves below the handlers, unchanged;
-- Counter's create and Pacer's room_start change.

Walker = object { components = C_POS }
Counter = object { components = C_POS }
Clock = object {}
Pacer = object {}

score = 10
bonus = 7
lives = 3
ticks = 0
starts = 0
destroyed = 0
extra = array(2)
history = array(4)
steps = { 5, 6, 7 }

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
    self.x = self.x + 3
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
    score = score + 5
    history[1] = score
    extra[2] = lives + bonus
  end
end

function stroll(who, pace)
  local walked = 0
  while true do
    wait(pace)
    who.x = who.x + 1
    walked = walked + 1
  end
end
