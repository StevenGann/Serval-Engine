-- Saves, seeding, trigonometry and formatted text, steering a script. Saves:
-- integer and fixed arrays written and read back, another version and another
-- size refused, an erase, bad slots and versions (the C function or the VM
-- warns; nothing changes). Seeding: the same sequence twice, and 0 as the
-- engine's fixed seed. Trigonometry: every quadrant's sine and cosine and
-- angle_of's tiny and huge vectors, exactly (the stub runs trig.c's tables).
-- diff: frames=24 start=Hud attach=Tick@0,0 warnings=allowed
Hud = object {}
Tick = object { components = C_POS }

scores = array(3)
speeds = array(2)
big = array(501)
wrote = 0
read = 0
sum = 0
speed = 0.0
other_version = 0
other_size = 0
erased = 0
bad = 0
first = 0
again = 0
zero = 0
sines = 0
cosines = 0
aims = 0
turned = 0
frames_seen = 0
size = 0
version = 0

function Hud:room_start()
  for i = 1, 3 do scores[i] = i * 100 end
  speeds[1] = 1.5
  speeds[2] = -0.25
  if save_write(0, scores, 2) then wrote = wrote + 1 end
  if save_write(1, speeds, 2) then wrote = wrote + 10 end
  if save_write(7, scores, 65535) then wrote = wrote + 100 end
  scores[2] = 0
  speeds[1] = 0.0
  read = save_read(0, scores, 2) + 10 * save_read(1, speeds, 2)
  sum = scores[1] + scores[2] + scores[3]
  speed = speeds[1] + speeds[2]
  other_version = save_read(0, scores, 3)
  other_size = save_read(0, speeds, 2)
  size = save_slot_size(0) + save_slot_size(1) * 100
  version = save_slot_version(7)
  save_erase(7)
  erased = save_read(7, scores, 65535) * 10 + save_slot_size(7)
  if not save_write(8, scores, 1) then bad = bad + 1 end
  if not save_write(-1, scores, 1) then bad = bad + 2 end
  if not save_write(0, scores, 65536) then bad = bad + 4 end
  if not save_write(0, scores, -1) then bad = bad + 8 end
  if not save_write(0, big, 1) then bad = bad + 16 end
  bad = bad + 100 * save_read(8, scores, 1) + 1000 * save_slot_size(9)

  random_seed(4242)
  first = random_range(0, 100000)
  random_seed(4242)
  again = random_range(0, 100000)
  random_seed(0)
  zero = random_range(0, 100000)

  text_print_format(0, 0, "%03d/%d", sum, first)
  text_print_format(0, 1, "%c%c", 65 + read, 66)
  text_print_format(0, 2, "100%%")
  text_print_line(1, 3, "LINE")
  text_clear_area(0, 0, 2, 2)
  text_set_color(COLOR_RGB(255, 255, 255), 0)
  text_set_style(1)
  text_set_style_color(2, 31, 992)
end

function Tick:step()
  frames_seen = frames_seen + 1
  for i = 1, 32 do
    local a = (frames_seen * 32 + i) * 211
    sines = sines + math.floor(fx_sin(a) * 256) + math.floor(fx_sin(a + 65536) * 256)
    cosines = cosines + math.floor(fx_cos(a) * 256)
    local dx = (i - 16) * 0.75 + frames_seen
    local dy = (frames_seen - 12) * 3.5 - i
    aims = aims + angle_of(dx, dy) + angle_of(dx * 300, dy * 0.25)
  end
  aims = aims + angle_of(0.00390625, 1000) + angle_of(-20000, -0.5) + angle_of(0, 0)
  turned = ANGLE_DEG(-1) + ANGLE_DEG(360)
end
