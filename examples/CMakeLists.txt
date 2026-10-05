serval_add_rom(hello SOURCES hello/main.c TITLE "SERVAL HELLO" GAME_CODE "SVHE")
target_link_libraries(hello PRIVATE serval_warnings)

serval_add_rom(bunnymark SOURCES bunnymark/main.c bunnymark/bunnymark.c
               TITLE "BUNNYMARK" GAME_CODE "SVBM")
target_link_libraries(bunnymark PRIVATE serval_warnings)

serval_add_rom(pong SOURCES pong/main.c TITLE "PONG" GAME_CODE "SVPG")
target_link_libraries(pong PRIVATE serval_warnings)

serval_add_rom(asteroids SOURCES asteroids/main.c TITLE "ASTEROIDS" GAME_CODE "SVAS")
target_link_libraries(asteroids PRIVATE serval_warnings)

serval_add_rom(platformer SOURCES platformer/main.c platformer/game.c platformer/level.c
               platformer/player.c platformer/objects.c platformer/art.c platformer/sound.c
               TITLE "SERVAL DASH" GAME_CODE "SVPF")
target_link_libraries(platformer PRIVATE serval_warnings)

# Headless benchmark: the same game run by bench.c instead of main.c.
serval_add_rom(bunnymark_bench SOURCES bunnymark/bench.c bunnymark/bunnymark.c
               TITLE "BUNNYBENCH" GAME_CODE "SVBB")
target_link_libraries(bunnymark_bench PRIVATE serval_warnings)

# Padding and header, no libc.a in the map, no BLX (cmake/ServalRomChecks.cmake).
if(COMMAND serval_add_rom_checks)
    serval_add_rom_checks(hello bunnymark pong asteroids platformer bunnymark_bench)
endif()
