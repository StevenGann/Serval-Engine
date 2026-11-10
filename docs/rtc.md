# The real-time clock

`rtc.h` reads the clock some cartridges carry (the Seiko S-3511A in the Pokemon games and others), so a game can tell how much time passed while it was off: a creature that gets hungry, a crop that grows, a daily reward. It reads the clock and nothing else; there is no way to set it.

## The API

```c
typedef struct RtcTime { u16 year; u8 month, day, hour, minute, second; } RtcTime;

bool rtc_present(void);
bool rtc_read(RtcTime* out);
u32  rtc_seconds(void);
bool rtc_was_reset(void);
u32  rtc_seconds_from_time(const RtcTime* t);
void rtc_time_from_seconds(u32 s, RtcTime* out);
```

- `rtc_present()`: a working clock was found. Without one (a cartridge without the chip, an emulator that does not give the game one, the host) `rtc_read()` zeroes `*out` and returns false, and `rtc_seconds()` returns 0. Games check `rtc_present()` once and offer their game without time when it is false.
- `rtc_read()` fills in the date and time (2000 to 2099) and returns true. If the chip returns nonsense twice running, it returns false with the last good time, and a debug build warns once.
- `rtc_seconds()`: the same time as seconds since 2000-01-01 00:00:00, 0 to 3155759999 (2099-12-31 23:59:59). This is what a game stores: two seconds counts subtract.
- `rtc_was_reset()`: the chip had lost its power before this run (a dead battery, or a cartridge never powered), so its time is not the real date. Games treat the first run after that as a new start.
- `rtc_seconds_from_time()` and `rtc_time_from_seconds()` convert between the two, for showing a date or building one. A field out of range gives 0, a count past 3155759999 gives 2099-12-31 23:59:59, and a debug build warns once. They do not touch the clock and work on every platform.

## How the clock is found

The first call to `rtc_present()`, `rtc_read()`, `rtc_seconds()` or `rtc_was_reset()` probes, and the answer is kept for the run:

1. Read the status byte. If its power bit (7) is set, remember `rtc_was_reset()`, reset the chip and write status `0x40`.
2. If bit 6 (24-hour mode) is clear, write `0x40` and read the status again; still clear means there is no chip.
3. Read the date and time. Every nibble must be a decimal digit, the month 1 to 12, the day within that month, the hour at most 23 and the minute and second at most 59. A cartridge without a chip, where the port reads as open bus (all ones) or zeros, fails here. The weekday is ignored.

A game that never calls these functions does not link the clock at all.

## Elapsed time, not the date

The clock is the player's, and the player can change it. Games should use it for elapsed time (`rtc_seconds()` now minus the value saved last time), and put limits on what elapsed time can do (a creature that cannot starve in a single absence). The date is not proof of anything. Time zones and daylight saving time are the game's problem: the chip knows neither, and tells the local time its owner set.

There is no setter on purpose: games that set the clock are games that can be cheated with it, a clock set by a game is wrong for every other game on the cartridge, and the engine would have to promise behaviour that flash carts and emulators do not keep.

## When the clock goes backwards

The player can set the clock back, a battery swap can reset it, and daylight saving time moves it an hour. When `rtc_seconds()` is lower than on the previous call, a debug build warns once ("the cartridge clock went backwards") and returns the lower value. Games treat a negative difference as no time passing (`now > before ? now - before : 0`); the same for a value read from a save that is later than now.

## Emulators and flash carts

- mGBA: the clock is off unless the game is told it has one: Tools, Game Overrides, Realtime clock, or `[override.CODE]` with `hardware=1` in `config.ini` (CODE being the game code).
- VBA-M and ares find the ID string `SIIRTC_V001` that the engine puts in the ROM and give the game a clock.
- NanoBoyAdvance and SkyEmu always have a clock.
- Flash carts differ: some have a clock of their own, wired like the real chip, and some none. Test on yours.

## The web

A web build has a clock that always works: `rtc_present()` is true and the time is the browser's local time. `rtc_was_reset()` is false.

## Testing with a fake clock

The tests swap in their own chip, as the save tests do their own memory: `serval_rtc_device` (`src/core/rtc_internal.h`) points to a `RtcDevice` of four functions (read and write the status byte, reset, read the seven BCD bytes) and `serval_rtc_forget()` makes the next call probe again. `tests/rtc_tests.c` has a fake chip that behaves like the real one, with the power bit that clears on read. Game code written against `rtc.h` tests its time handling by taking the seconds as a parameter.
