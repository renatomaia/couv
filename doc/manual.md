Manual
======

### `scope = cothread.scope ()` {#cothread.scope}

Creates a new scope.

### `tid = scope:create (f, ...)` {#scope:create}

Create a cothread in the scope to run function `f` with args `...`.

### `tid = scope:running ()` {#scope:running}

If it is called from the cothread currently running in this scope, it returns the cothread's ID as light userdata. Otherwise it returns `nil`.

### `scope:resume (trap)` {#scope:resume}

Resumes all the cothread in this scope. Whenever a cothread ends, function `trap`is called (using the thread that called this operarion) with the following parameters regarding the ended cothread:
- cothread's ID;
- cothread's dead thread;
- boolean indicating whether the cothread ended successfully (didn't raise an error);
- all the returned values (in case of success), or an error value (in caise of an error).

### `scope:yield ()` {#scope:yield}

Suspends the execution of the current thread so other threads can execute.

