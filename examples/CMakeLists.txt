serval_add_rom(hello SOURCES hello/main.c TITLE "SERVAL HELLO" GAME_CODE "SVHE")
target_link_libraries(hello PRIVATE serval_warnings)
