-- random_range: the engine's generator from a seed, every draw in the
-- same order: small and large ranges, negative ones, the whole 32-bit
-- range (its span wraps to 0), lo == hi (which draws nothing).
-- diff: frames=8 start=Dice seed=12345
Dice = object {}

rolls = array(40)
n = 0
same = 0

function roll(lo, hi)
  n = n + 1
  rolls[n] = random_range(lo, hi)
end

function Dice:room_start()
  for k = 1, 6 do
    roll(1, 6)
    roll(-100, 100)
    roll(0, 1)
    roll(math.mininteger, math.maxinteger)
    roll(-2000000000, 2000000000)
    same = same + random_range(7, 7)
    wait(1)
  end
  roll(math.maxinteger - 1, math.maxinteger)
  roll(math.mininteger, math.mininteger + 2)
end
