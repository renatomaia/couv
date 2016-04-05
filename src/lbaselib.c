#include <lua.h>
#include <lauxlib.h>


#define ct_toloophandle(L)	(uv_loop_t *)lua_touserdata(L, lua_upvalueindex(1))
#define ct_toloopthread(L)	lua_tothread(L, lua_upvalueindex(2))


static int ct_calltrap (lua_State *L, int status) {
	/* TODO: call the trap function using 'L', if any is registered */
}

static void ctI_idleclosed (uv_handle_t* h) {
	lua_State *uvL = (lua_State *)h->data;
	lua_State *L = (lua_State *)lua_touserdata(uvL, -1);
	lua_pop(co, 1);  /* pop the userdata 'L' */

	/* TODO: discard 'co' from the place it is saved, so it become collectable */

	if (L == uvL) {
		uv_stop(uv);
	} else {
		int status = lua_resume(L, uvL, narg);
		if (status != LUA_YIELD) ct_calltrap(L, status);
	}
}

static void ctI_idlestarted (uv_idle_t* h) {
	lua_State *uvL = (lua_State *)h->loop->data;
	lua_State *L = (lua_State *)h->data;
	lua_pushlightuserdata(L, uvL);
	uv_idle_stop(h);
	uv_close(h, ctI_idleclosed);
}

static void ctI_scheduleonce (lua_State *L, lua_State* co, uv_loop_t *uv) {
	uv_idle_t *h = (uv_idle_t *)lua_newuserdata(co, sizeof(uv_idle_t));
	h->data = co;
	uv_idle_init(uv, h);
	uv_idle_start(h, ctI_idlestarted);
	/* TODO: save thread 'co' in 'L' so it is not collected */
}

static int ctI_yield (lua_State *L) {
	uv_loop_t *uv = ct_toloophandle(L);
	lua_State *uvL = ct_toloopthread(L);
	if (!uv->data) {
		int status = lua_resume(uvL, L, 0);
		lua_assert(status == LUA_YIELD);
		return 0;
	}
	return lua_yield(L, 0);
}

static int ctI_initialize (lua_State *L) {
	return lua_yieldk(L, 0, 0, ctI_runloop);
}

static lua_State *ctI_newthread (lua_State *L, int nargs) {
	lua_State *co = lua_newthread(L);
	lua_xmove(L, co, nargs+1);  /* move function and args from L to co */
	return co;
}

static int ctI_closelib (lua_State *L) {
	uv_loop_t *uv = (uv_loop_t *)lua_touserdata(L, 1);
	uv_loop_close(uv);
	return 0;
}


static int ct_create (lua_State *L) {
	uv_loop_t *uv = ct_toloophandle(L);
	lua_State *co;
	luaL_checktype(L, 1, LUA_TFUNCTION);
	co = lua_newthread(L);
	lua_insert(L, 1);                   /* move thread to bottom */
	lua_xmove(L, co, lua_gettop(L)-1);  /* move function and args from L to co */
	ctI_scheduleonce(L, co, uv);
	lua_pushlightuserdata(L, co);
	return 1;
}

static int ct_yield (lua_State *L) {
	uv_loop_t *uv = ct_toloophandle(L);
	ctI_scheduleonce(L, L, uv);
	return ctI_yield(L);
}


static const luaL_Reg lib[] = {
	{"create", ct_create},
	{"yield", ct_yield},
	{NULL, NULL}
};

static void ctI_runloop (lua_State *L) {
	uv_loop_t *uv = ct_toloophandle(L);
	uv->data = L;
	lua_pushboolean(L, uv_run(uv, UV_RUN_DEFAULT));
	lua_insert(L, 1);
	uv->data = NULL;
	return lua_gettop(L);
}

static int ct_newscope (lua_State *L) {
	lua_State *ct;
	uv_loop_t *uv;
	int nargs;
	int pending;

	luaL_checktype(L, 1, LUA_TFUNCTION);
	nargs = lua_gettop(L);        /* args + the library created ahead */

	uv = (uv_loop_t *)luaL_newsentinel(t, sizeof(uv_loop_t), ctI_closelib);
	uv_loop_init(uv);
	lua_newtable(L);              /* push the thread registration table */
	lua_setuservalue(L, -2);

	luaL_newlibtable(L, lib);     /* push new library */
	lua_pushvalue(L, -2);         /* push a copy of 'uv' to be the upvalue */
	luaL_setfuncs(L, lib, 1);     /* fill library functions */
	lua_insert(L, 2);             /* place the library below the args */
	lua_insert(L, 1);             /* place the 'uv' below the main function */

	ct = ctI_newthread(L, nargs); /* pops function, library and args */
	ctI_scheduleonce(L, ct, uv);
	lua_pushcclosure(L, ctI_runloop, 1);  /* push 'resume' function */

	return 1;
}


LUAMOD_API int luaopen_cothread_scope (lua_State *L)
{
	lua_State *t;
	uv_loop_t *uv;
  int status;

	luaL_newlibtable(L, lib);

	t = lua_newthread(L);
	uv = (uv_loop_t *)luaL_newsentinel(t, sizeof(uv_loop_t), ctI_closelib);
	uv_loop_init(uv);
	lua_pushcclosure(t, ctI_initialize, 1);
	status = lua_resume(t, L, 0);
	lua_assert(status == LUA_YIELD);

	luaL_setfuncs(L, lib, 1);
	return 1;
}
