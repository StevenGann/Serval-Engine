-- Waits and frame counts: wait(0) and negative waits don't wait, wait(1)
-- resumes the next frame, wait(n) after n frames; several behaviours
-- resume in pool order (the order their contexts were taken), so the
-- order they log in shows it.
-- diff: frames=12 start=First start=Second attach=Third
First = object {}
Second = object {}
Third = object {}

log = array(30)
at = 0
frames_a = 0
frames_b = 0

function note(who)
  at = at + 1
  log[at] = who
end

function First:room_start()
  note(1)
  wait(0)
  wait(-5)
  note(11)
  wait(1)
  note(12)
  wait(3)
  note(13)
  for k = 1, 4 do
    frames_a = frames_a + 1
    wait(1)
  end
  note(14)
end

function Second:room_start()
  note(2)
  wait(2)
  note(21)
  wait(2)
  note(22)
  while frames_b < 3 do frames_b = frames_b + 1; wait(1) end
  note(23)
end

function Third:create()
  note(3)
  wait(1)
  note(31)
  wait(4)
  note(32)
end
