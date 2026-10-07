-- Frames: locals in handlers, parameters and locals in functions (ENTER p,
-- n), calls with and without results (CALL, RET, RETV), nesting and
-- recursion, and math.min/max/abs.
Probe = object {}

result = 0

function fact(n)
  if n <= 1 then return 1 end
  return n * fact(n - 1)
end

function clamp(v, lo, hi)
  return math.max(lo, math.min(v, hi))
end

function hypot2(a, b)
  local aa = a * a
  local bb = b * b
  return aa + bb
end

function report(v)
  print(1, 1, v)
end

function Probe:room_start()
  local total = 0
  for i = 1, 3 do
    local square = hypot2(i, i)
    total = total + square
  end
  result = fact(5) + clamp(total, 0, 100) + math.abs(-total)
  report(result)
  fact(3)       -- a result nobody uses: dropped
end
