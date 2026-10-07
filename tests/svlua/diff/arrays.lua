-- Arrays: RAM arrays (zeroed, written and read back, # their length) and
-- ROM tables stored in each kind (s8, u8, s16, u16, s32, and fixed values),
-- indices computed at run time, a multiple assignment that indexes with the
-- value before it changes (Lua evaluates every target's index first).
-- diff: frames=4 start=Probe
Probe = object {}

small = { -128, 0, 127 }             -- s8
bytes = { 0, 200, 255 }              -- u8
shorts = { -32768, 1000, 32767 }     -- s16
words = { 0, 40000, 65535 }          -- u16
longs = { math.mininteger, -70000, math.maxinteger }  -- s32
speeds = { 0.5, -1.25, 3.75 }        -- fixed, as 256ths

t = array(6)
f = array(3)
local u = array(4)
n = 0
i = 2
total = 0

function Probe:room_start()
  for k = 1, 3 do
    t[k] = small[k] + bytes[k]
    t[k + 3] = shorts[k] + words[k]
  end
  wait(1)
  for k = 1, #longs do
    u[k] = longs[k] // 2
    f[k] = speeds[k] * 2
  end
  u[#u] = #small + #t
  wait(1)
  i, t[i] = i + 1, 99
  n = #t + #f
  for k = #t, 1, -1 do total = total + t[k] end
  t[(total % #t) + 1] = -1
end
