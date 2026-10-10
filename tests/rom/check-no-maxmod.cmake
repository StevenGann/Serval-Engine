# Fails if a ROM's link map shows any of Maxmod or the engine's mixer:
#
#   cmake -DMAP=<rom>.map -P check-no-maxmod.cmake
#
# For tests/rom/sampled_audio_calls_main.c, which calls music_*() and sfx_*()
# without registering a sound bank: only audio_bank_set() may bring Maxmod in
# (src/gba/maxmod.c), so games that never register a bank (scripted games
# reach every music_*() and sfx_*() call) keep their IWRAM.
file(READ "${MAP}" map)
string(REGEX MATCH "Discarded input sections" discarded "${map}")
string(FIND "${map}" "Discarded input sections" start)
string(SUBSTRING "${map}" 0 ${start} archive_members)
foreach(object IN ITEMS maxmod.c.obj mas.c.obj mas_arm.c.obj effect.c.obj main_gba.c.obj
                        mixer.c.obj mixer_asm.s.obj)
    string(FIND "${archive_members}" "(${object})" at)
    if(NOT at EQUAL -1)
        message(FATAL_ERROR "${MAP}: links ${object}, a part of Maxmod, without a sound bank")
    endif()
endforeach()
