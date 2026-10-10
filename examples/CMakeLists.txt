serval_add_rom(hello SOURCES hello/main.c TITLE "SERVAL HELLO" GAME_CODE "SVHE")
target_link_libraries(hello PRIVATE serval_warnings)

serval_add_rom(bunnymark SOURCES bunnymark/main.c bunnymark/bunnymark.c
               TITLE "BUNNYMARK" GAME_CODE "SVBM")
target_link_libraries(bunnymark PRIVATE serval_warnings)

serval_add_rom(pong SOURCES pong/main.c TITLE "PONG" GAME_CODE "SVPG")
target_link_libraries(pong PRIVATE serval_warnings)

serval_add_rom(asteroids SOURCES asteroids/main.c asteroids/scores.c TITLE "ASTEROIDS"
               GAME_CODE "SVAS")
target_link_libraries(asteroids PRIVATE serval_warnings)

serval_add_rom(platformer SOURCES platformer/main.c platformer/game.c platformer/level.c
               platformer/player.c platformer/objects.c platformer/art.c platformer/sound.c
               platformer/stage_overworld.c platformer/art_overworld.c
               platformer/sound_overworld.c platformer/stage_underground.c
               platformer/art_underground.c platformer/sound_underground.c
               platformer/stage_treetops.c platformer/art_treetops.c
               platformer/sound_treetops.c platformer/stage_castle.c platformer/art_castle.c
               platformer/sound_castle.c platformer/boss.c
               TITLE "SERVAL DASH" GAME_CODE "SVPF" SAVE FLASH64K)
target_link_libraries(platformer PRIVATE serval_warnings)

serval_add_rom(breakout SOURCES breakout/main.c breakout/game.c breakout/play.c
               breakout/levels.c breakout/scores.c breakout/art.c breakout/sound.c
               TITLE "PAW BREAKER" GAME_CODE "SVBK")
target_link_libraries(breakout PRIVATE serval_warnings)

serval_add_rom(shmup SOURCES shmup/main.c shmup/game.c shmup/player.c shmup/enemies.c
               shmup/boss.c shmup/stage.c shmup/art.c shmup/sound.c shmup/scores.c
               TITLE "STAR VELDT" GAME_CODE "SVSH")
target_link_libraries(shmup PRIVATE serval_warnings)

serval_add_rom(blackjack SOURCES blackjack/main.c blackjack/game.c blackjack/rules.c
               blackjack/table.c blackjack/bank.c blackjack/art.c blackjack/sound.c
               TITLE "BLACKJACK" GAME_CODE "SVBJ")
target_link_libraries(blackjack PRIVATE serval_warnings)

serval_add_rom(fireflies SOURCES fireflies/main.c fireflies/art.c fireflies/sound.c
               TITLE "FIREFLIES" GAME_CODE "SVFF")
# The game's logic is a script in the Lua subset (docs/lua.md), compiled and
# assembled at build time (tools/svlua.py, tools/svm.py) into
# fireflies_script.c and fireflies_script.h; its names in ALL_CAPS come from
# these headers, the game's and the engine's.
serval_add_script(fireflies fireflies/fireflies.lua
                  HEADERS fireflies/game.h serval/ecs.h serval/core.h serval/sprites.h
                          serval/path.h serval/screen.h)
target_link_libraries(fireflies PRIVATE serval_warnings)

# One scene per screen effect, each in its own file (effects.h).
serval_add_rom(effects SOURCES
               effects/main.c
               effects/art.c
               effects/blend_fx.c
               effects/palette_fx.c
               effects/sprite_tiles_fx.c
               effects/stream_fx.c
               effects/raster_fx.c
               TITLE "EFFECTS" GAME_CODE "SVFX")
target_link_libraries(effects PRIVATE serval_warnings)

# Tracker music and sampled sound effects. Its modules and WAV samples are
# made at build time by tools/make-example-audio.py (original, synthesized:
# nothing is downloaded), and mmutil builds them into jukebox_bank.c and
# jukebox_bank.h (serval_add_soundbank(), cmake/Serval.cmake).
serval_add_rom(jukebox SOURCES jukebox/main.c TITLE "JUKEBOX" GAME_CODE "SVJB")
target_link_libraries(jukebox PRIVATE serval_warnings)
set(jukebox_audio theme.mod calm.mod coin.wav laser.wav boom.wav engine.wav)
list(TRANSFORM jukebox_audio PREPEND "${CMAKE_CURRENT_BINARY_DIR}/jukebox_audio/")
add_custom_command(
    OUTPUT ${jukebox_audio}
    COMMAND "${SERVAL_PYTHON_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/make-example-audio.py"
            jukebox "${CMAKE_CURRENT_BINARY_DIR}/jukebox_audio"
    DEPENDS "${PROJECT_SOURCE_DIR}/tools/make-example-audio.py"
    COMMENT "Making the jukebox's modules and samples"
    VERBATIM)
serval_add_soundbank(jukebox jukebox_bank ${jukebox_audio})

# Headless benchmark: the same game run by bench.c instead of main.c.
serval_add_rom(bunnymark_bench SOURCES bunnymark/bench.c bunnymark/bunnymark.c
               TITLE "BUNNYBENCH" GAME_CODE "SVBB")
target_link_libraries(bunnymark_bench PRIVATE serval_warnings)

# Padding and header, no libc.a in the map, no BLX (cmake/ServalRomChecks.cmake).
if(COMMAND serval_add_rom_checks)
    serval_add_rom_checks(hello bunnymark pong asteroids platformer breakout shmup blackjack
                          fireflies bunnymark_bench)
    serval_add_rom_checks(effects)
    serval_add_rom_checks(jukebox)
endif()
