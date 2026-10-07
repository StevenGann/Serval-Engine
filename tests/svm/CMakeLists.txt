# The hand-written fireflies listing: the example's logic until it was
# written in Lua (examples/fireflies/fireflies.lua), kept as the assembler's
# largest fixture. tools/svm_test.py assembles it and checks the round trip;
# this assembles it through serval_add_script(), so a listing (.svm), as a
# game may still write one, keeps being built.
add_library(serval_svm_listing_check OBJECT)
serval_add_script(serval_svm_listing_check fireflies.svm
                  HEADERS ../../examples/fireflies/game.h serval/ecs.h serval/core.h
                          serval/sprites.h serval/path.h serval/screen.h)
target_link_libraries(serval_svm_listing_check PRIVATE serval_warnings)
