# The script benchmark (docs/development.md#script-benchmark), run headless
# by tools/bench.sh like bunnymark_bench: scriptmark.lua's busy scripted scene,
# and the fireflies example timed the same way, both measured by vmbench.c.
# Benchmarks, not examples: vmbench.c reads the engine's cycle counter by its
# timer registers, which games may read (docs/core-api.md#timers) but
# examples, using only Serval API, don't. GBA builds only.
serval_add_rom(scriptmark_bench SOURCES scriptmark/bench.c scriptmark/vmbench.c
               TITLE "SCRIPTMARK" GAME_CODE "SVSM")
target_link_libraries(scriptmark_bench PRIVATE serval_warnings)
serval_add_script(scriptmark_bench scriptmark/scriptmark.lua
                  HEADERS scriptmark/game.h serval/ecs.h serval/core.h)

set(fireflies "${PROJECT_SOURCE_DIR}/examples/fireflies")
serval_add_rom(fireflies_bench SOURCES scriptmark/fireflies_bench.c scriptmark/vmbench.c
               "${fireflies}/art.c" "${fireflies}/sound.c" TITLE "FIREFLYBENCH" GAME_CODE "SVFB")
target_link_libraries(fireflies_bench PRIVATE serval_warnings)
serval_add_script(fireflies_bench "${fireflies}/fireflies.lua"
                  HEADERS "${fireflies}/game.h" serval/ecs.h serval/core.h serval/sprites.h
                          serval/path.h serval/screen.h)

serval_add_rom_checks(scriptmark_bench fireflies_bench)
