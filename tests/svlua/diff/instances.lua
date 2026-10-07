-- instances(Object): a loop over an object's attached instances in slot
-- order, those whose Create is still queued included; a kill (queued) in
-- the loop doesn't cut it short, and an instance spawned during the loop
-- into a later slot is visited by the same loop.
-- diff: frames=4 start=Census attach=Ant attach=Bee attach=Ant attach=Ant attach=Bee
Census = object {}
Ant = object { components = C_POS }
Bee = object { components = C_POS }

ants = 0
bees = 0
visited = array(12)
v = 0
last = none

function Census:room_start()
  for a in instances(Ant) do
    ants = ants + 1
    v = v + 1
    visited[v] = math.floor(a.x) + 1
    a.x = a.x + 10
    if ants == 1 then kill(a) end
    if ants == 2 then spawn(Ant, 50, 0) end
    last = a
  end
  wait(1)
  for b in instances(Bee) do bees = bees + 1; kill(b) end
  for a in instances(Ant) do
    v = v + 1
    visited[v] = math.floor(a.x) + 1
  end
  wait(1)
  for b in instances(Bee) do bees = bees + 100 end
end
