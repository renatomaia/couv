local scheduler = require "coroutine.scheduler"

local function asserterror(msg, f, ...)
  local ok, err = pcall(f, ...)
  assert(ok == false)
  assert(string.find(err, msg, nil, true) ~= nil, err)
end

do print "scheduler.pause"
  asserterror("string expected", scheduler.run, coroutine.running())
  asserterror("unable to yield", scheduler.pause)

  assert(scheduler.run() == false)
  local weak = setmetatable({}, {__mode = "v"})

  local stage1 = 0
  local ok, a,b,c,d = coroutine.resume(coroutine.create(function (...)
    weak.coro1 = coroutine.running()
    scheduler.pause(...)
    asserterror("already running", scheduler.run)
    stage1 = 1
    scheduler.pause()
    stage1 = 2
    coroutine.yield()
    stage1 = 3
  end), "testing", 1, 2, 3)
  assert(ok == true)
  assert(a == "testing")
  assert(b == 1)
  assert(c == 2)
  assert(d == 3)
  assert(stage1 == 0)
  assert(type(weak.coro1) == "thread")

  local stage2 = 0
  ok, a,b,c,d = coroutine.resume(coroutine.create(function (errmsg)
    weak.coro2 = coroutine.running()
    scheduler.pause()
    asserterror("already running", scheduler.run)
    stage2 = 1
    error(errmsg)
  end, "oops!"))
  assert(ok == true)
  assert(a == nil)
  assert(b == nil)
  assert(c == nil)
  assert(d == nil)
  assert(stage2 == 0)
  assert(type(weak.coro2) == "thread")

  collectgarbage("collect")
  assert(type(weak.coro1) == "thread")
  assert(type(weak.coro2) == "thread")
  assert(scheduler.run() == false)
  assert(stage1 == 2)
  assert(stage2 == 1)

  collectgarbage("collect")
  assert(weak.coro1 == nil)
  assert(weak.coro2 == nil)
  assert(scheduler.run() == false)
end

print("OK")
