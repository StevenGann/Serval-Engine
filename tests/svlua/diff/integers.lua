-- Integer arithmetic at the edges of 32 bits: wrapping + - * and unary
-- minus, floored // and % with negative operands (and mininteger // -1),
-- shifts both ways with counts of 32 and more, the bitwise operators and
-- comparisons. Every pair of operands from two ROM arrays, a pair a frame.
-- diff: frames=82 start=Probe print=1,2,40,81,82
Probe = object {}

as = { 7, -7, 0, 1, -1, math.maxinteger, math.mininteger, 123456789, -98765 }
bs = { 2, -2, 3, -3, 31, 32, -32, 33, math.mininteger }

sum = array(81)
diff = array(81)
prod = array(81)
quot = array(81)
rest = array(81)
shl = array(81)
shr = array(81)
bits = array(81)
neg = 0
order = 0

function Probe:room_start()
  local k = 1
  for i = 1, #as do
    for j = 1, #bs do
      local a, b = as[i], bs[j]
      sum[k] = a + b
      diff[k] = a - b
      prod[k] = a * b
      quot[k] = a // b
      rest[k] = a % b
      shl[k] = a << b
      shr[k] = a >> b
      bits[k] = (a & b) ~ (a | ~b)
      neg = -a
      order = 0
      if a < b then order = order + 1 end
      if a <= b then order = order + 2 end
      if a > b then order = order + 4 end
      if a >= b then order = order + 8 end
      if a == b then order = order + 16 end
      if a ~= b then order = order + 32 end
      k = k + 1
      wait(1)
    end
  end
end
