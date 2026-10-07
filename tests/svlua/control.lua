-- Control flow: if, while, repeat, numeric for (constant and run-time
-- steps, fixed-point loops), break and goto.
Probe = object {}

n = 0
lo = 0
hi = 0
step = 0
x = 0.0

function Probe:room_start()
  while n < 10 do
    n = n + 1
    if n == 5 then break end
  end
  repeat
    local left = n - 1
    n = left
  until left <= 0          -- until sees the body's locals
  for i = 1, 10 do n = n + i end
  for i = hi, lo, -1 do n = n + i end
  for i = lo, hi, 3 do n = n + i end
  for i = lo, hi, step do n = n + i end
  for v = 0.0, 1.0, 0.25 do x = x + v end
  for i = 1, x do n = n + i end                -- an integer loop with a fixed limit
  for i = 1, 3 do
    if i == 2 then goto continue end
    n = n + i
    ::continue::
  end
  for i = 1, 3 do i = i * 2; n = n + i end     -- the body assigns i: a copy
end
