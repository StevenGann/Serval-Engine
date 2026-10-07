-- Booleans: and, or and not as jumps (short circuit), as values, and in
-- conditions with zero tests.
Probe = object {}

a = false
b = false
n = 0
e = none

function Probe:step()
  if a and b then n = 1 end
  if a or b then n = 2 end
  if not (a and b) then n = 3 end
  if (a or b) and not (n > 2 or n < -2) then n = 4 end
  if n ~= 0 and e == none then n = 5 end
  a = a and b        -- a value: DUP, JZ, DROP
  b = a or n > 1
  b = not a
  b = true and a     -- folded: a
  b = false and a    -- folded: false (a isn't evaluated)
  if n == 0 then n = 6 elseif n > 10 then n = 7 else n = 8 end
end
