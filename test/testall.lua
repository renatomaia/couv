local scope = require "cothread.scope"

local function asserterror(msg, f, ...)
  local ok, err = pcall(f, ...)
  assert(ok == false)
  assert(string.find(err, msg, nil, true) ~= nil)
end

do print "scope:{create,resume,running}"
  local s = scope()
  asserterror("function expected", s.create, s)
  asserterror("function expected", s.create, s, "function")
  local tok, terr
  tok = s:create(function (...)
    asserterror("already resumed", s.resume, s)
    assert(s:running() == tok)
    assert(select("#", ...) == 4)
    assert(select(1, ...) == "testing")
    assert(select(2, ...) == 1)
    assert(select(3, ...) == 2)
    assert(select(4, ...) == 3)
    return ...
  end, "testing", 1, 2, 3)
  collectgarbage("collect")
  terr = s:create(function (errmsg)
    asserterror("already resumed", s.resume, s)
    assert(s:running() == terr)
    error(errmsg)
  end, "oops!")
  collectgarbage("collect")
  local c = 0
  s:resume(function (tid, ct, ok, ...)
    asserterror("already resumed", s.resume, s)
    collectgarbage("collect")
    assert(s:running() == nil)
    --assert(coroutine.status(ct) == "dead")
    if tid == tok then
      assert(ok == true)
      assert(select("#", ...) == 4)
      assert(select(1, ...) == "testing")
      assert(select(2, ...) == 1)
      assert(select(3, ...) == 2)
      assert(select(4, ...) == 3)
    else
      assert(tid == terr)
      assert(ok == false)
      assert(select("#", ...) == 1)
      assert(string.find(..., "oops!") ~= nil)
    end
    c = c+1
  end)
  collectgarbage("collect")
  assert(c == 2)
  assert(s:running() == nil)
end

print("OK")
