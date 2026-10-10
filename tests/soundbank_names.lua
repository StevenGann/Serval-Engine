-- soundbank_names.lua: a script naming a sound bank's modules and samples,
-- the MOD_* and SFX_* of the header serval_add_soundbank() generates (the
-- jukebox example's, built for the API-only check in tests/CMakeLists.txt),
-- as it names any header's constants.
Jukebox = object {}

tunes = { MOD_THEME, MOD_CALM }
effects = { SFX_COIN, SFX_LASER, SFX_BOOM, SFX_ENGINE }
tune_count = MSL_NSONGS

function Jukebox:create()
  tune_count = tune_count + tunes[1] + effects[1]
end
