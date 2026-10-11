-- debug_break(), the debug link's script breakpoint: with no debugger
-- attached it changes nothing a script can see (the VM logs BRK and goes
-- on; under real Lua it does nothing), in a behaviour, a loop and a function.
-- diff: frames=3 start=Probe
Probe = object {}

n = 0

function bump(k)
  debug_break()
  return k + 1
end

function Probe:room_start()
  n = 1
  debug_break()
  wait(1)
  for i = 1, 3 do
    debug_break()
    n = n + bump(i)
  end
end
