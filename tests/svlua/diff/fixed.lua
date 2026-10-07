-- Fixed point: Lua's floats are 24.8 on the VM (docs/lua.md "Types"), so
-- results agree within a tolerance: products, quotients (/ always gives
-- one), integers mixed in, math.floor back to integers (exact when the
-- value is), fixed-point properties and a fixed for loop.
-- diff: frames=3 start=Calc attach=Mover@10,20 movement tolerance=0.02
Calc = object {}
Mover = object { components = C_POS | C_VEL }

a = 1.5
b = -2.25
p = 0.0
q = 0.0
r = 0.0
s = 0.0
whole = 0
half = 0.0
acc = 0.0

function Calc:room_start()
  p = a * b
  q = a / b
  r = 7 / 2
  s = (a + 3) * 0.5 - b
  whole = math.floor(s) + math.floor(-0.5)
  half = math.max(a, b) - math.min(a, 0.25)
  for v = 0.0, 1.0, 0.125 do acc = acc + v end
  wait(1)
  for m in instances(Mover) do
    m.vx = a / 4
    m.vy = -0.5
    acc = acc + m.x
  end
end
