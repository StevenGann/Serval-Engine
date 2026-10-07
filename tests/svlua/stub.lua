-- stub.lua: Serval Engine's script API in plain Lua, so that a script in the
-- Lua subset (docs/lua.md) runs under real Lua 5.4 built with LUA_32BITS, as
-- tools/svlua_difftest.py runs it to compare with the same script compiled
-- and run on the engine's VM (tests/svlua/runner.c). It reproduces what a
-- script can observe of the engine, as docs/vm.md specifies it and the
-- runner drives it:
--
--   - objects, and instances as tables: the engine's properties (x, y, vx,
--     vy, sprite, ..., truncated to their arrays' types as SETP does) and
--     instance fields (unset ones read 0, 0.0, false or none by the field's
--     type), entity handles as the ECS hands them out (a FIFO of free slots,
--     a generation per slot);
--   - behaviours (create, room_start, threads) as coroutines that wait()
--     yields, in a pool of contexts taken lowest first and resumed in pool
--     order; reactions as plain calls, run to completion;
--   - the event queue, drained in FIFO order with the VM's rules (Create
--     before the first Step, Destroy then the halt, events for dead or
--     unattached entities skipped);
--   - the frame: the resume pass, a drain, the Step reactions in slot order,
--     a drain; then the runner's sys_movement and collision pairs; then
--     vm_events' drain;
--   - random_range, the engine's xorshift32 and its scaling, bit for bit;
--   - the engine calls the platform makes, logged as the runner logs them.
--
-- What the VM does that a script in the subset can't observe, or that the
-- runner never meets, is left out: the ops budget (the difftest rejects runs
-- that hit it), Animation End and WAIT_ANIM (no animations run), paths
-- (WAIT_MOVE never waits), music and paths' bindings, the queue's and the
-- context pool's limits.
--
-- Usage: lua stub.lua CONFIG.lua SCRIPT.lua
-- CONFIG.lua returns a table (tools/svlua_difftest.py writes it):
--   constants  {NAME = value}: the C headers' constants the script uses
--   engine     {C_POS = .., C_VEL = .., C_SPR = .., C_MAPBODY = .., MAX_ENT = ..,
--              VM_CONTEXTS = .., SYS = {PSG_PLAY = 0, ...}}
--   globals    {{name, local?}, ...}: the VM's globals, in order (local: a
--              top-level local)
--   arrays     {{name, local?}, ...}: the RAM arrays, in order
--   fields     {{name, type}, ...}: the instance fields, by slot
--   frames, seed, starts {{object, event}}, attaches {{object, x, y}},
--   buttons {{frame, mask, length}}, movement, collides {{object, other}},
--   prints     "all", "last" or {frame = true, ...}
-- and prints, like svlua_runner: "frame F", "call NAME A..." as calls happen
-- (the VM's arguments, TEXT_PRINT's string instead of its index),
-- then "global NAME VALUE", "array NAME V...", "entity HANDLE OBJECT P... F..."
-- after each printed frame (values as Lua has them: integers in decimal,
-- floats in C's %a hexadecimal, booleans; entities as their handles).

local config = dofile(arg[1])
local script_path = arg[2]
local E = config.engine
local MAX_ENT, CONTEXTS = E.MAX_ENT, E.VM_CONTEXTS

-- --- Output -------------------------------------------------------------------

local info = setmetatable({}, { __mode = "k" }) -- an entity's or object's state

local function show(v)
  if type(v) == "number" then
    if math.type(v) == "integer" then return string.format("%d", v) end
    return string.format("%a", v) -- exact, and never mistaken for an integer
  elseif type(v) == "boolean" then
    return tostring(v)
  elseif type(v) == "table" and info[v] and info[v].handle then
    return string.format("%d", info[v].handle)
  end
  error("can't show a " .. type(v))
end

local function call(name, ...)
  local parts = { "call", name }
  for _, v in ipairs({ ... }) do
    parts[#parts + 1] = type(v) == "string" and ('"' .. v .. '"') or show(v)
  end
  io.write(table.concat(parts, " "), "\n")
end

-- --- Entities -----------------------------------------------------------------

-- The engine's properties, in VM_P_* order, and how SETP stores each.
local PROPS = { "x", "y", "vx", "vy", "sprite", "frame", "flags", "angle", "depth", "scale",
                "body_w", "body_h", "tags", "anim_time", "anim_step" }
local function wrap(v, bits, signed)
  v = v & ((1 << bits) - 1)
  if signed and v >= (1 << (bits - 1)) then v = v - (1 << bits) end
  return v
end
local STORE = {
  sprite = function(v) return wrap(v, 16) end, frame = function(v) return wrap(v, 8) end,
  flags = function(v) return wrap(v, 16) end, angle = function(v) return wrap(v, 16) end,
  depth = function(v) return wrap(v, 16, true) end,
  -- spr_scale: 8.8 in an s16; the script's value is in 256ths too.
  scale = function(v) return wrap(math.floor(v * 256 + 0.5), 16, true) / 256 end,
  body_w = function(v) return wrap(v, 8) end, body_h = function(v) return wrap(v, 8) end,
  tags = function(v) return v & 0x7FFF end,
  anim_time = function(v) return wrap(v, 8) end, anim_step = function(v) return wrap(v, 8) end,
}
local FIXED_PROPS = { x = true, y = true, vx = true, vy = true, scale = true }
local IS_PROP = {}
for _, p in ipairs(PROPS) do IS_PROP[p] = true end
local FIELD_TYPE = {}
for _, f in ipairs(config.fields) do FIELD_TYPE[f[1]] = f[2] end

local none -- the entity 0
local function default(name)
  if IS_PROP[name] then return FIXED_PROPS[name] and 0.0 or 0 end
  local ty = FIELD_TYPE[name]
  if ty == "fixed" then return 0.0 elseif ty == "boolean" then return false
  elseif ty == "entity" then return none end
  return 0
end

local Entity = {}
Entity.__index = function(e, name)
  local s = info[e]
  local v
  if IS_PROP[name] then v = s.props[name] elseif s.attached then v = s.fields[name] end
  if v == nil then return default(name) end
  return v
end
Entity.__newindex = function(e, name, v)
  local s = info[e]
  if IS_PROP[name] then
    s.props[name] = STORE[name] and STORE[name](v) or v
  elseif s.attached then
    s.fields[name] = v
  end
end

local function new_entity(handle)
  local e = setmetatable({}, Entity)
  info[e] = { handle = handle, props = {}, fields = {}, alive = false }
  return e
end
none = new_entity(0)

-- The ECS's slots: handed out from a FIFO of free ones, so a destroyed slot
-- comes back last; a slot's generation goes up when it is freed.
local slots, gens, free, free_head, free_count = {}, {}, {}, 0, MAX_ENT
for i = 0, MAX_ENT - 1 do gens[i] = 1; free[i] = i end

local function entity_create(mask)
  if free_count == 0 then return none end
  local slot = free[free_head]
  free_head = (free_head + 1) % MAX_ENT
  free_count = free_count - 1
  local e = new_entity(gens[slot] << 8 | slot)
  local s = info[e]
  s.slot, s.mask, s.alive = slot, mask, true
  s.props.tags = (mask >> 16) & 0x7FFF
  slots[slot] = e
  return e
end

local function entity_destroy(e)
  local s = info[e]
  s.alive = false
  slots[s.slot] = nil
  gens[s.slot] = gens[s.slot] == 255 and 1 or gens[s.slot] + 1
  free[(free_head + free_count) % MAX_ENT] = s.slot
  free_count = free_count + 1
end

-- --- Objects, contexts, events --------------------------------------------------

local objects = {} -- in declaration order: an object's number

local contexts = {} -- [0, CONTEXTS): {co, wait, entity}, nil when free
local queue = {}    -- {entity, other, event}, oldest first

-- Takes the first free context, in pool order; returns its number.
local function start_context(co, e)
  for k = 0, CONTEXTS - 1 do
    if not contexts[k] then
      contexts[k] = { co = co, wait = 0, entity = e }
      if e ~= none then info[e].context = k end
      return k
    end
  end
  error("no free context")
end

local function halt(k)
  local c = contexts[k]
  if c.entity ~= none then info[c.entity].context = nil end
  contexts[k] = nil
end

-- Runs a behaviour until it waits or ends: WAIT n (n >= 1) yields n.
local function resume(k)
  local c = contexts[k]
  local ok, waited = coroutine.resume(c.co)
  if not ok then error(debug.traceback(c.co, waited), 0) end
  if coroutine.status(c.co) == "dead" then
    halt(k)
  else
    c.wait = waited
  end
end

local function behaviour(handler, e)
  return coroutine.create(function() handler(e) end)
end

local function enqueue(e, other, event)
  queue[#queue + 1] = { e, other, event }
end

local function attach(e, object)
  local s = info[e]
  s.attached, s.object, s.fields = true, object, {}
  s.create_pending = true
  enqueue(e, none, "create")
end

local function unbind(e)
  local s = info[e]
  if s.context then halt(s.context) end
  s.attached, s.fields = false, {}
end

local BEHAVIOURS = { create = true, room_start = true }

local function dispatch(e, other, event)
  local s = info[e]
  if event == "destroy" then
    if not s.alive then return end
    if s.attached then
      local handler = s.object.destroy
      if handler then handler(e) end -- a reaction: other is none
      unbind(e)
    end
    entity_destroy(e)
    return
  end
  if not s.alive or not s.attached then return end
  if event == "create" then s.create_pending = false end
  local handler = s.object[event]
  if not handler then return end
  if not BEHAVIOURS[event] then
    handler(e, other)
    return
  end
  if s.context then halt(s.context) end -- one behaviour per instance
  resume(start_context(behaviour(handler, e), e))
end

local function drain()
  while #queue > 0 do
    local q = table.remove(queue, 1)
    dispatch(q[1], q[2], q[3])
  end
end

-- --- Buttons and random numbers ------------------------------------------------

local held, held_before = 0, 0

-- The engine's random_range (src/core/random.c): xorshift32, scaled by the
-- high half of a 32 x 32-bit product, here from 16-bit pieces (Lua's
-- integers are 32 bits).
local state = config.seed or 0x2545F491
if state == 0 then state = 0x2545F491 end
local function random_u32()
  state = state ~ (state << 13)
  state = state ~ (state >> 17)
  state = state ~ (state << 5)
  return state
end
local function mul_high(a, b) -- the high 32 bits of a * b, both unsigned
  local a_lo, a_hi, b_lo, b_hi = a & 0xFFFF, (a >> 16) & 0xFFFF, b & 0xFFFF, (b >> 16) & 0xFFFF
  local t = a_lo * b_lo
  t = a_hi * b_lo + ((t >> 16) & 0xFFFF)
  local w1, w2 = t & 0xFFFF, (t >> 16) & 0xFFFF
  t = a_lo * b_hi + w1
  return a_hi * b_hi + w2 + ((t >> 16) & 0xFFFF)
end

-- --- The script's world ----------------------------------------------------------

local env = {}
for name, value in pairs(config.constants) do env[name] = value end
env.math = math
env.none = none

function env.C_GAME(n) return 1 << (16 + n) end

function env.object(t)
  local o = {}
  info[o] = { mask = t.components or 0, sprite = t.sprite or 0, index = #objects }
  objects[#objects + 1] = o
  return o
end

function env.array(n)
  local a = {}
  for i = 1, n do a[i] = 0 end
  return a
end

function env.spawn(object, x, y)
  local o = info[object]
  local e = entity_create(o.mask)
  if e == none then return none end
  e.x, e.y = x, y
  if o.mask & E.C_SPR ~= 0 then e.sprite = o.sprite end
  attach(e, object)
  return e
end

function env.kill(e) enqueue(e, none, "destroy") end

function env.instances(object)
  return function(_, previous)
    local from = previous and info[previous].slot + 1 or 0
    for slot = from, MAX_ENT - 1 do
      local e = slots[slot]
      if e and info[e].attached and info[e].object == object then return e end
    end
    return nil
  end, nil, nil
end

function env.wait(n)
  if n > 0 then coroutine.yield(math.min(n, 65535)) end
end
function env.wait_move() end -- no paths run: nothing to wait for
function env.wait_anim() end -- no animations run: the VM warns and goes on

function env.text_print(col, row, text) call("TEXT_PRINT", col, row, text) end
function env.text_print_number(col, row, n, width)
  call("TEXT_PRINT_NUMBER", col, row, n, width or 0)
end
function env.psg_play(id) call("PSG_PLAY", id) end
function env.psg_music_play(song) call("PSG_MUSIC_PLAY", song) end
function env.psg_music_stop() call("PSG_MUSIC_STOP") end
function env.psg_music_pause() call("PSG_MUSIC_PAUSE") end
function env.psg_music_resume() call("PSG_MUSIC_RESUME") end
function env.screen_set_brightness(level) call("SCREEN_SET_BRIGHTNESS", level) end
function env.camera_set(x, y) end
function env.path_start(e, path, flags) end
function env.path_stop(e) end

function env.random_range(lo, hi)
  if hi <= lo then return lo end
  local span = hi - lo + 1 -- wraps; 0 means the whole 32-bit range
  if span == 0 then return random_u32() end
  return lo + mul_high(random_u32(), span)
end

function env.button_down(mask) return held & mask ~= 0 end
function env.button_pressed(mask) return held & ~held_before & mask ~= 0 end

local chunk = assert(loadfile(script_path, "t", env))
chunk()

-- --- Reading the state -----------------------------------------------------------

-- Top-level locals are globals in the VM; in Lua they live on as upvalues
-- of the functions that use them (in the subset every function is
-- top-level), found here by name.
local wanted, upvalue = {}, {}
for _, list in ipairs({ config.globals, config.arrays }) do
  for _, g in ipairs(list) do
    if g[2] then wanted[g[1]] = true end
  end
end
local seen, source = {}, "@" .. script_path
local function search(f)
  if seen[f] or debug.getinfo(f, "S").source ~= source then return end
  seen[f] = true
  for i = 1, math.huge do
    local name, value = debug.getupvalue(f, i)
    if not name then break end
    if wanted[name] and not upvalue[name] then upvalue[name] = { f, i } end
    if type(value) == "function" then search(value) end
  end
end
for _, v in pairs(env) do if type(v) == "function" then search(v) end end
for _, o in ipairs(objects) do
  for _, v in pairs(o) do if type(v) == "function" then search(v) end end
end
for name in pairs(wanted) do
  if not upvalue[name] then
    error("top-level local " .. name .. " is used by no function: the stub can't read it")
  end
end

local function global(name)
  local where = upvalue[name]
  if where then
    local _, value = debug.getupvalue(where[1], where[2])
    return value
  end
  return env[name]
end

local function print_state()
  for _, g in ipairs(config.globals) do
    io.write("global ", g[1], " ", show(global(g[1])), "\n")
  end
  for _, a in ipairs(config.arrays) do
    local parts = { "array", a[1] }
    for _, v in ipairs(global(a[1])) do parts[#parts + 1] = show(v) end
    io.write(table.concat(parts, " "), "\n")
  end
  for slot = 0, MAX_ENT - 1 do
    local e = slots[slot]
    if e and info[e].attached then
      local parts = { "entity", show(e), tostring(info[info[e].object].index) }
      for _, p in ipairs(PROPS) do parts[#parts + 1] = show(e[p]) end
      for _, f in ipairs(config.fields) do parts[#parts + 1] = show(e[f[1]]) end
      io.write(table.concat(parts, " "), "\n")
    end
  end
end

-- --- The frames --------------------------------------------------------------------

for _, s in ipairs(config.starts) do
  local o = objects[s[1] + 1]
  start_context(behaviour(o[s[2]], none), none)
end
for _, a in ipairs(config.attaches) do
  local o = objects[a[1] + 1]
  local e = entity_create(info[o].mask)
  e.x, e.y = a[2] + 0.0, a[3] + 0.0
  if info[o].mask & E.C_SPR ~= 0 then e.sprite = info[o].sprite end
  attach(e, o)
end

local function overlap(a, b)
  return a.x < b.x + b.body_w and b.x < a.x + a.body_w and
         a.y < b.y + b.body_h and b.y < a.y + a.body_h
end

for f = 1, config.frames do
  frame = f
  io.write("frame ", f, "\n")
  held_before, held = held, 0
  for _, b in ipairs(config.buttons) do
    if f >= b[1] and f - b[1] < b[3] then held = held | b[2] end
  end
  -- vm_step: the resume pass, a drain, the Step reactions, a drain.
  for k = 0, CONTEXTS - 1 do
    local c = contexts[k]
    if c then
      if c.wait == 0 then -- ready: started by vm_start
        resume(k)
      else
        c.wait = c.wait - 1
        if c.wait == 0 then resume(k) end
      end
    end
  end
  drain()
  for slot = 0, MAX_ENT - 1 do
    local e = slots[slot]
    local s = e and info[e]
    if s and s.attached and not s.create_pending and s.object.step then s.object.step(e) end
  end
  drain()
  if config.movement then
    for slot = 0, MAX_ENT - 1 do
      local e = slots[slot]
      local m = e and info[e].mask
      if e and m & (E.C_POS | E.C_VEL | E.C_MAPBODY) == E.C_POS | E.C_VEL then
        e.x, e.y = e.x + e.vx, e.y + e.vy
      end
    end
  end
  for _, pair in ipairs(config.collides) do
    local object, other = objects[pair[1] + 1], objects[pair[2] + 1]
    for a = 0, MAX_ENT - 1 do
      local ea = slots[a]
      if ea and info[ea].attached and info[ea].object == object then
        for b = 0, MAX_ENT - 1 do
          local eb = slots[b]
          if a ~= b and eb and info[eb].attached and info[eb].object == other
              and overlap(ea, eb) then
            enqueue(ea, eb, "collision")
          end
        end
      end
    end
  end
  drain() -- vm_events
  if config.prints == "all" or (config.prints == "last" and f == config.frames)
      or (type(config.prints) == "table" and config.prints[f]) then
    print_state()
  end
end
