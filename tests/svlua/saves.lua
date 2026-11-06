-- Saves, seeding, trigonometry and formatted text: what they compile to.
Hud = object {}

scores = array(2)
wrote = false
result = 0
dx = 0.0
dy = 0.0
aim = 0

function Hud:room_start()
  wrote = save_write(0, scores, 1)
  result = save_read(0, scores, 1)
  save_erase(result)
  result = save_slot_version(0) + save_slot_size(0)
  random_seed(result)
  dx = fx_cos(ANGLE_DEG(45))
  dy = fx_sin(ANGLE_DEG(-45))
  aim = angle_of(dx, 1)
  text_print_format(0, 1, "%03d/%d", result, 9)
  text_print_format(0, 2, "%c", 65)
  text_print_format(0, 3, "100%%")
  text_print_line(0, 4, "LINE")
  text_clear_area(0, 0, 30, 1)
  text_set_color(COLOR_RGB(255, 0, 0), 0)
  text_set_style(1)
  text_set_style_color(1, 0, 0)
end
