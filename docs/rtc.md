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
- `rtc_was_reset()`: the chip had lost its power before this run (a dead battery, or a cartridge never powered), so its time is not the real date. Games treat the first run after that as a new start. It is always false without a clock.
- `rtc_seconds_from_time()` and `rtc_time_from_seconds()` convert between the two, for showing a date or building one. A field out of range gives 0, a count past 3155759999 gives 2099-12-31 23:59:59, and a debug build warns once. They do not touch the clock and work on every platform; a game that only converts links no clock.

## How the clock is found

The first call to `rtc_present()`, `rtc_read()`, `rtc_seconds()` or `rtc_was_reset()` probes, and the answer is kept for the run:

1. Read the status byte. If its power bit (7) is set, remember `rtc_was_reset()`, reset the chip and write status `0x40`.
2. If bit 6 (24-hour mode) is clear, write `0x40` and read the status again; still clear means there is no chip.
3. Read the date and time. Every nibble must be a decimal digit, the month 1 to 12, the day within that month, the hour at most 23 and the minute and second at most 59. A cartridge without a chip, where the port reads as open bus (all ones) or zeros, fails here. The weekday is ignored.

When no clock is found, `rtc_was_reset()` is false even if step 1 saw a power bit: nothing was reset that the game can use.

Only `rtc_present()`, `rtc_read()`, `rtc_seconds()` and `rtc_was_reset()` link the clock (on the GBA, its driver and the ID string that tells emulators the game has one); a game that never calls them, or that only calls the two conversions, does not.

## What a read costs

Each transaction with the chip (a status read or write, a reset, the date-time read) masks interrupts (`REG_IME` off, restored after) for its whole length, so that a handler cannot stretch a bit of the serial protocol. One `rtc_read()` with a clock costs 4,653 CPU cycles in the `gba-ci` build and 3,619 in `gba-release`, almost all of it with interrupts off: about two scanlines. The first call also probes, and without a clock the probe alone costs 7,171 cycles (`gba-ci`) or 5,928 (`gba-release`). `rtc_seconds()` is one read.

Read the clock occasionally: once a second at most is plenty for anything a game does with it, and once at boot and once when a screen opens is usually enough. Do not read it in raster-timed code (a scanline effect, a VCount interrupt, `raster_*()` timing) or at a moment when a missed interrupt matters. The VBlank handler waits for a read to end, as it does for the engine's other critical sections.

## The cartridge's port

The clock is on the cartridge's GPIO port, three 16-bit registers in the ROM's address space at 0x080000C4 (data), 0x080000C6 (direction) and 0x080000C8 (control). A game that calls `rtc_*` claims them; a game that does not never touches them. The ROM bytes 0xC0-0xDF are zero in every ROM (the startup code reserves them, and `tools/check-rom.py` checks it), because emulators and flash carts write the port's state there.

After each transaction the control register is left at 0, so reads at those addresses return the ROM's bytes again, and pin 3's direction and level are kept as they were. Pins 0 to 2 belong to the clock (SCK, SIO, CS). A game that drives pin 3 itself (a rumble motor, a solar sensor's reset) with control set to 1 must set the control register again after any `rtc_*` call, and should not call `rtc_*` while it depends on that pin's level for timing.

## Elapsed time, not the date

The clock is the player's, and the player can change it. Games should use it for elapsed time (`rtc_seconds()` now minus the value saved last time), and put limits on what elapsed time can do (a creature that cannot starve in a single absence). The date is not proof of anything. Time zones and daylight saving time are the game's problem: the chip knows neither, and tells the local time its owner set.

There is no setter on purpose: games that set the clock are games that can be cheated with it, a clock set by a game is wrong for every other game on the cartridge, and the engine would have to promise behaviour that flash carts and emulators do not keep.

## When the clock goes backwards

The player can set the clock back, a battery swap can reset it, and daylight saving time moves it an hour. When `rtc_seconds()` is lower than on the previous call, a debug build warns once ("the cartridge clock went backwards") and returns the lower value. Games treat a negative difference as no time passing (`now > before ? now - before : 0`); the same for a value read from a save that is later than now.

## Emulators and flash carts

- mGBA: the clock is off unless the game is told it has one: Tools, Game Overrides, Realtime clock, or `[override.CODE]` with `hardware=1` in `config.ini` (CODE being the game code).
- VBA-M and ares find the ID string `SIIRTC_V001` that the engine puts in the ROM and give the game a clock.
- The capture tool built on mGBA's core (`rtc=SECONDS`, `rtcstep=FRAME:SECONDS`; `docs/development.md#tests`) gives a ROM mGBA's clock with a fake time, for tests.
- NanoBoyAdvance and SkyEmu always have a clock.
- Flash carts differ: some have a clock of their own, wired like the real chip, and some none. Test on yours.

**What has been verified, and against what.** The register protocol, the probe, the BCD decoding and the pin 3 and IME handling are checked against mGBA's clock (CTest `rtc_probe_capture`, which needs the capture tool and is skipped where it is missing, always in CI; `docs/development.md#tests`) and against a fake chip in the host and test ROM suites. mGBA's clock checks no timing, and it never reports power loss. So the bit timing (store counts and the 200 microsecond wait after a write, copied from the margins of agbabi's driver, which works on hardware) and the power-lost path (reset, status 0x40, `rtc_was_reset()`) are verified only against emulators and the fake chip, not on a cartridge. Test them on hardware before relying on them.

## The web

A web build has a clock that always works: `rtc_present()` is true and the time is the browser's local time. `rtc_was_reset()` is false. A test page takes the clock from its address, for deterministic runs: `#frames=N&rtc=SECONDS` starts it at SECONDS since 2000-01-01 00:00:00 (762525296 is 2024-02-29 12:34:56) and advances it one second per 60 frames, `&rtcstep=FRAME:SECONDS` (repeatable) adds SECONDS (negative to go back) from that frame on, and `&rtc=none` gives the game no clock, in any mode. `tools/web-shots.py` passes them on as `rtc=SECONDS`, `rtcstep=FRAME:SECONDS` and `rtc=none`. The web test page (`serval_web_tests`) must run with `rtc=762525296`, as CI does: its suite checks that time.

## Testing with a fake clock

The tests swap in their own chip, as the save tests do their own memory: `serval_rtc_device` (`src/core/rtc_internal.h`) points to a `RtcDevice` of four functions (read and write the status byte, reset, read the seven BCD bytes) and `serval_rtc_forget()` makes the next call probe again. `tests/rtc_tests.c` has a fake chip that behaves like the real one, with the power bit that clears on read. Game code written against `rtc.h` tests its time handling by taking the seconds as a parameter.
