# Fails if consumer_unused(), which nothing calls, made it into the ROM:
# game sources were compiled without -ffunction-sections.
file(READ "${MAP}" map)
string(REGEX MATCH "\n[ \t]+0x[0-9a-f]+[ \t]+consumer_unused\n" kept "${map}")
if(kept)
    message(FATAL_ERROR "consumer_unused() is in the ROM: game code lacks -ffunction-sections")
endif()
string(FIND "${map}" ".text.consumer_unused" discarded)
if(discarded EQUAL -1)
    message(FATAL_ERROR "consumer_unused() was not compiled into its own section "
                        "(.text.consumer_unused missing from the map)")
endif()
