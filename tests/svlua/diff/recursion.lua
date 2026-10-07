-- Functions: recursion (a frame per call, under VM_CALLS), mutual
-- recursion, arguments and locals that survive the calls they make, early
-- returns, results used in expressions, a function called from a waiting
-- behaviour across frames.
-- diff: frames=16 start=Probe
Probe = object {}

f8 = 0
fib5 = 0
evens = 0
mixed = 0
deep = 0
local kept = 0

function fact(n) if n <= 1 then return 1 end return n * fact(n - 1) end
function fib(n) if n < 2 then return n end return fib(n - 1) + fib(n - 2) end
function is_even(n) if n == 0 then return true end return is_odd(n - 1) end
function is_odd(n) if n == 0 then return false end return is_even(n - 1) end
function mix(a, b, c)
  local s = a * 100 + b * 10 + c
  local t = add3(c, b, a)
  if s < 0 then return -1 end
  return s - t
end
function add3(x, y, z) local w = x + y; return w + z end
function depth(n) if n == 0 then return 0 end return 1 + depth(n - 1) end
function pause_and_count(n)
  for i = 1, n do
    kept = kept + i
    wait(1)
  end
  return kept
end

function Probe:room_start()
  local before = 7
  f8 = fact(8)
  wait(1)
  fib5 = fib(5)
  wait(1)
  for i = 0, 6 do
    if is_even(i) then evens = evens + 1 end
    wait(1)
  end
  mixed = mix(1, 2, 3) + before
  deep = depth(12)
  wait(1)
  deep = deep + pause_and_count(3) * 1000
end
