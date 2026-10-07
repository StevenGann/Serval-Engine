-- Input and engine calls: button_down while a button is held,
-- button_pressed only on the frame it goes down; the calls the platform
-- makes (text, numbers with and without a width, sounds, brightness), in
-- order, each frame.
-- diff: frames=8 start=Pad buttons=2:BUTTON_A:3 buttons=4:BUTTON_B|BUTTON_LEFT:2 buttons=7:BUTTON_A
Pad = object {}

downs = 0
presses = 0
combo = 0

function Pad:room_start()
  print(1, 1, "PAD")
  brightness(-8)
  while true do
    wait(1)
    if button_down(BUTTON_A) then
      downs = downs + 1
      print(2, 2, downs)
    end
    if button_pressed(BUTTON_A) then
      presses = presses + 1
      play_sound(presses)
      print(3, 3, presses, 4)
    end
    if button_down(BUTTON_B) and button_down(BUTTON_LEFT) then
      combo = combo + 1
      brightness(combo)
    end
    if button_pressed(BUTTON_B | BUTTON_START) then print(4, 4, "B!") end
  end
end
