-- Arrays: RAM (array(n)) and ROM (a table of constants, in the narrowest
-- kind), 1-based a[i] as 0-based LDA and STA, and #a as LEN.
Probe = object {}

scores = array(8)
waves = { 3, 5, 8, 13 }               -- u8? s8: every element fits -128..127
speeds = { 0.5, 1.5, -2.25 }          -- fixed, scaled by 256: s16
big = { 1000, 70000 }                 -- s32
total = 0

function Probe:room_start()
  for i = 1, #scores do
    scores[i] = waves[(i - 1) % #waves + 1] * 2
  end
  scores[1] = scores[#scores]
  total = scores[3] + big[2]
  local v = speeds[2] + speeds[total % 3 + 1]
  total = total + math.floor(v)
  scores[total % 8 + 1], total = total, 0   -- the index is taken before total changes
end
