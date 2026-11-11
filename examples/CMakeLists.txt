serval_add_rom(hello SOURCES hello/main.c TITLE "SERVAL HELLO" GAME_CODE "SVHE")
target_link_libraries(hello PRIVATE serval_warnings)

serval_add_rom(bunnymark SOURCES bunnymark/main.c bunnymark/bunnymark.c
               TITLE "BUNNYMARK" GAME_CODE "SVBM")
target_link_libraries(bunnymark PRIVATE serval_warnings)

serval_add_rom(pong SOURCES pong/main.c pong/art.c TITLE "PONG" GAME_CODE "SVPG")
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
# lz77: the castle's tileset and the dragon's tiles, LZ77-compressed at build
# time (tools/lz77.py) into castle_lz77.c and castle_lz77.h.
serval_add_lz77(platformer castle_lz77 platformer/castle_tiles.inc platformer/dragon_tiles.inc)

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

serval_add_rom(servle SOURCES servle/main.c servle/art.c servle/sound.c
               TITLE "SERVLE" GAME_CODE "SVSL")
# A five-letter word-guessing game whose rules are all a script in the Lua
# subset, as fireflies' are; its names in ALL_CAPS come from these headers.
serval_add_script(servle servle/servle.lua
                  HEADERS servle/game.h serval/ecs.h serval/core.h serval/sprites.h
                          serval/screen.h serval/save.h)
target_link_libraries(servle PRIVATE serval_warnings)

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
serval_add_rom(jukebox SOURCES jukebox/main.c jukebox/art.c jukebox/show.c TITLE "JUKEBOX"
               GAME_CODE "SVJB")
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
# vm sound: its effect pads, a Lua script naming the bank's samples
# (pads_script.c and pads_script.h)
serval_add_script(jukebox jukebox/pads.lua
                  HEADERS "${CMAKE_CURRENT_BINARY_DIR}/jukebox_bank.h" serval/core.h)

serval_add_rom(raycaster SOURCES raycaster/main.c raycaster/game.c raycaster/world.c
               raycaster/view.c raycaster/art.c raycaster/sound.c raycaster/title.c
               TITLE "WISP WARREN" GAME_CODE "SVRC")
target_link_libraries(raycaster PRIVATE serval_warnings)

serval_add_rom(racer SOURCES racer/main.c racer/game.c racer/screens.c racer/hud.c
               racer/records.c racer/view.c racer/objects.c racer/art.c racer/road.c
               racer/track.c racer/sound.c
               TITLE "SAVANNA RUN" GAME_CODE "SVRR")
target_link_libraries(racer PRIVATE serval_warnings)

# BEAT PAWS, a rhythm game. Its songs and their charts are made at build time
# from the same notes by tools/make-example-audio.py's rhythm set (original,
# synthesized): the modules, built into rhythm_bank.c and rhythm_bank.h, and
# rhythm_charts.h beside the bank's header.
serval_add_rom(rhythm SOURCES rhythm/main.c rhythm/game.c rhythm/screens.c rhythm/calibrate.c
               rhythm/records.c rhythm/play.c rhythm/judge.c rhythm/songs.c rhythm/art.c
               rhythm/sound.c TITLE "BEAT PAWS" GAME_CODE "SVBP")
target_link_libraries(rhythm PRIVATE serval_warnings)
target_include_directories(rhythm PRIVATE rhythm)
set(rhythm_audio rhythm_title.mod rhythm_calibrate.mod rhythm_calm.mod rhythm_stride.mod
    rhythm_rush.mod)
list(TRANSFORM rhythm_audio PREPEND "${CMAKE_CURRENT_BINARY_DIR}/rhythm_audio/")
add_custom_command(
    OUTPUT ${rhythm_audio} "${CMAKE_CURRENT_BINARY_DIR}/rhythm_charts.h"
    COMMAND "${SERVAL_PYTHON_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/make-example-audio.py"
            rhythm "${CMAKE_CURRENT_BINARY_DIR}/rhythm_audio"
            --header-dir "${CMAKE_CURRENT_BINARY_DIR}"
    DEPENDS "${PROJECT_SOURCE_DIR}/tools/make-example-audio.py"
    COMMENT "Making Beat Paws' songs and charts"
    VERBATIM)
serval_add_soundbank(rhythm rhythm_bank ${rhythm_audio})
target_sources(rhythm PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/rhythm_charts.h")

# PAWS & KINGS, chess and checkers: a portable core (both games' rules, one
# search for both, the save; tested on the host in tests/) and the GBA side
# (the art drawn at boot, the board view).
serval_add_rom(boardgames SOURCES boardgames/main.c boardgames/chess_board.c
               boardgames/chess_rules.c boardgames/chess_eval.c boardgames/checkers.c
               boardgames/checkers_eval.c boardgames/search.c boardgames/match.c
               boardgames/records.c boardgames/art.c boardgames/view.c
               boardgames/play.c boardgames/screens.c boardgames/sound.c
               TITLE "PAWS & KINGS" GAME_CODE "SVPK")
target_link_libraries(boardgames PRIVATE serval_warnings)

# SERVAL DEN, a virtual serval living by the cartridge clock: a portable
# core (the pet's life, the litter, the album, the save; tested on the host in
# tests/) and the GBA side (the clock, the art, the den). The art is drawn
# ahead of time: art_data.c is generated by art_gen (tests/CMakeLists.txt,
# host preset) from art_draw.c, which the ROM doesn't compile.
serval_add_rom(den SOURCES den/main.c den/pet.c den/litter.c den/album.c den/den_save.c
               den/clock.c den/clock_check.c den/art.c den/art_data.c den/den.c den/sound.c den/menu.c
               den/status.c den/naming.c den/yarn.c den/yarn_rules.c den/climb.c den/climb_rules.c
               den/pounce.c den/pounce_rules.c
               TITLE "SERVAL DEN" GAME_CODE "SVDN")
target_link_libraries(den PRIVATE serval_warnings)

# Headless benchmark: the same game run by bench.c instead of main.c.
serval_add_rom(bunnymark_bench SOURCES bunnymark/bench.c bunnymark/bunnymark.c
               TITLE "BUNNYBENCH" GAME_CODE "SVBB")
target_link_libraries(bunnymark_bench PRIVATE serval_warnings)

# Padding and header, no libc.a in the map, no BLX (cmake/ServalRomChecks.cmake).
if(COMMAND serval_add_rom_checks)
    serval_add_rom_checks(hello bunnymark pong asteroids platformer breakout shmup blackjack
                          fireflies servle bunnymark_bench)
    serval_add_rom_checks(effects)
    serval_add_rom_checks(jukebox)
    serval_add_rom_checks(raycaster)
    serval_add_rom_checks(racer)
    serval_add_rom_checks(rhythm)
    serval_add_rom_checks(boardgames)
    serval_add_rom_checks(den)
endif()
