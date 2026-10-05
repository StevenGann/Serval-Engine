serval_add_rom(hello SOURCES hello/main.c TITLE "SERVAL HELLO" GAME_CODE "SVHE")
target_link_libraries(hello PRIVATE serval_warnings)

serval_add_rom(bunnymark SOURCES bunnymark/main.c bunnymark/bunnymark.c
               TITLE "BUNNYMARK" GAME_CODE "SVBM")
target_link_libraries(bunnymark PRIVATE serval_warnings)

serval_add_rom(pong SOURCES pong/main.c TITLE "PONG" GAME_CODE "SVPG")
target_link_libraries(pong PRIVATE serval_warnings)

# Headless benchmark: the same game run by bench.c instead of main.c.
serval_add_rom(bunnymark_bench SOURCES bunnymark/bench.c bunnymark/bunnymark.c
               TITLE "BUNNYBENCH" GAME_CODE "SVBB")
target_link_libraries(bunnymark_bench PRIVATE serval_warnings)
