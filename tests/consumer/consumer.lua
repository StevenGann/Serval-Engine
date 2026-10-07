-- consumer.lua: a script for the consumer game, built by serval_add_script()
-- with the release archive's tools/svlua.py and tools/svm.py: one object
-- whose Create handler adds 2 to a global that starts at 40 (an initial
-- value the blob carries), which main.c reads back.
Thing = object {}

answer = 40

function Thing:create()
  answer = answer + 2
end
