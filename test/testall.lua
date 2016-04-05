local cothread = require "cothread"

do -- cothread.resume
	local f
	local t = coroutine.create(function (a, b)
		f = (a == 1) and (b == 2)
		return coroutine.yield(a, b, a+b)
	end)
	local a, b, c = cothread.resume(t, 1, 2)
	assert(f == true)
	assert(a == 1)
	assert(b == 2)
	assert(c == 3)
	assert(coroutine.status(t) == "suspended")
	f = nil
	local a, b, c = cothread.resume(t, "a", "b", "c")
	assert(f == true)
	assert(a == 1)
	assert(b == 2)
	assert(c == 3)
	assert(coroutine.status(t) == "suspended")
end

print("OK")
