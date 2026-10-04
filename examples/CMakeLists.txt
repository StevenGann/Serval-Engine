serval_add_rom(hello SOURCES hello/main.c TITLE "SERVAL HELLO" GAME_CODE "SVHE")
target_link_libraries(hello PRIVATE serval_warnings)

serval_add_rom(bunnymark SOURCES bunnymark/main.c TITLE "BUNNYMARK" GAME_CODE "SVBM")
target_link_libraries(bunnymark PRIVATE serval_warnings)

# Headless benchmark build of bunnymark, run by tools/bench.sh.
serval_add_rom(bunnymark_bench SOURCES bunnymark/main.c TITLE "BUNNYBENCH" GAME_CODE "SVBB")
target_compile_definitions(bunnymark_bench PRIVATE BUNNYMARK_BENCH)
target_link_libraries(bunnymark_bench PRIVATE serval_warnings)
