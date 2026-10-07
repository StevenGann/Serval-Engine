-- Numeric for at the integer limits (Lua 5.4 counts the iterations before
-- the first, so nothing overflows), negative and large steps, a run-time
-- step and limit evaluated once, fixed-point loops, while, repeat (whose
-- condition sees the body's locals), break, goto continue, nested loops.
-- diff: frames=14 start=Probe
Probe = object {}

counts = array(10)
lasts = array(10)
lo = 2147483640
step = 3
fsum = 0.0
wsum = 0
rsum = 0
nested = 0
skipped = 0
local hi = 5

function Probe:room_start()
  local n, last = 0, 0
  for i = math.maxinteger - 5, math.maxinteger, 2 do n = n + 1; last = i end
  counts[1], lasts[1] = n, last
  n, last = 0, 0
  for i = math.mininteger + 4, math.mininteger, -3 do n = n + 1; last = i end
  counts[2], lasts[2] = n, last
  wait(1)
  n, last = 0, 0
  for i = lo, math.maxinteger, step do n = n + 1; last = i end
  counts[3], lasts[3] = n, last
  wait(1)
  n, last = 0, 0
  for i = 10, 1 do n = n + 1; last = i end
  counts[4], lasts[4] = n, last
  n, last = 0, 0
  for i = 1, hi do hi = hi + 1; n = n + 1; last = i end
  counts[5], lasts[5] = n, last
  wait(1)
  n, last = 0, 0
  for i = -7, 7, 1073741824 do n = n + 1; last = i end
  counts[6], lasts[6] = n, last
  n, last = 0, 0
  for i = 5, -5, -4 do n = n + 1; last = i end
  counts[7], lasts[7] = n, last
  wait(1)
  for v = 0.0, 1.0, 0.25 do fsum = fsum + v end
  for v = 1, 0, -0.5 do fsum = fsum + v * 10 end
  wait(1)
  n = 0
  for i = 1, 2.5 do n = n + i end
  counts[8] = n
  wait(1)
  local w = 0
  while w < 10 do
    w = w + 1
    if w % 3 == 0 then goto continue end
    wsum = wsum + w
    ::continue::
  end
  wait(1)
  repeat
    local r = rsum + 4
    rsum = r
  until r > 15
  wait(1)
  for i = 1, 4 do
    for j = i, 4 do
      if j == 3 then break end
      nested = nested * 10 + j
    end
  end
  wait(1)
  for i = 1, 6 do
    if i % 2 == 1 then
      skipped = skipped + 1
      goto next
    end
    counts[9] = counts[9] + i
    ::next::
  end
end
