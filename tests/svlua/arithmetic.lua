-- Arithmetic: + - * on integers and fixed values, unary -, /, //, %, the
-- bitwise operators, comparisons, and constant folding.
Probe = object {}

n = 0
m = 0
f = 0.0
g = 0.0
b = false

function Probe:step()
  n = n + m * 3 - -m        -- ADD SUB MUL NEG
  f = f + g * 0.5 - 2       -- fixed: FXMUL, and 2 converted when it is folded
  f = f * n                 -- fixed times an integer: MUL
  f = g + n                 -- a variable integer converted: PUSH 256, MUL
  g = n / m                 -- / of two integers: FXDIV, a fixed result
  g = f / 3                 -- / of fixed by an integer: FXDIV after FX(3)
  n = n // m + n % m        -- IDIV IMOD: floored, as Lua's
  g = f // 1.5              -- fixed //: IDIV, then a whole fixed value
  n = n & m | n ~ m         -- AND OR XOR
  n = ~n << m               -- BNOT LSH
  n = n >> m                -- LSH with the count negated
  n = n >> 3                -- a constant count: folded to -3
  b = n < m and f >= g      -- comparisons
  b = n == m or f ~= g
  -- folded: none of these needs an operation
  n = 7 // 2 + 7 % -2 * 10 + (1 << 31 >> 31)
  f = 1.5 * 2 + 7 / 2 + 2 ^ 3
  n = SCREEN_W // 2 + (FLAGS & 7) * 3 - ~MASK   -- header constants: the assembler computes them
  f = SCREEN_W / 4 + 0.25
end
