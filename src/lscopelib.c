#include <stdio.h>
#include <lua.h>
#include <lauxlib.h>

LUALIB_API void luaL_printstack(lua_State *L)
{
	int i;
	for(i = 1; i <= lua_gettop(L); ++i) {
		printf("%d = ", i);
		switch (lua_type(L, i)) {
			case LUA_TNUMBER:
				printf("%g", lua_tonumber(L, i));
				break;
			case LUA_TSTRING:
				printf("\"%s\"", lua_tostring(L, i));
				break;
			case LUA_TBOOLEAN:
				printf(lua_toboolean(L, i) ? "true" : "false");
				break;
			case LUA_TNIL:
				printf("nil");
				break;
			default:
				printf("%s: %p", luaL_typename(L, i),
				                 lua_topointer(L, i));
				break;
		}
		printf("\n");
	}
	printf("\n");
}

#include <uv.h>


#define CT_HANDLECLS	"uv_handle_t*"
#define CT_SCOPECLS	"uv_loop_t*"
#define ct_toloophandle(L)	(uv_loop_t *)lua_touserdata(L, 1)

#if !defined(ct_assert)
#define ct_assert(X)	((void)(X))
#endif

static uv_handle_t *ctI_addthread (lua_State *L) {
	lua_State* ct = lua_tothread(L, -1);  /* L: uv...ct */
	uv_handle_t *h = (uv_handle_t *)lua_touserdata(ct, -1);  /* ct: ...h */
	lua_pushvalue(L, -1);    /* L: uv...ct,ct */
	lua_xmove(ct, L, 1);     /* L: uv...ct,ct,h | ct: ... */
	lua_pushvalue(L, 1);     /* L: uv...ct,ct,h,uv */
	lua_setuservalue(L, -2); /* L: uv...ct,ct,h */
	lua_getuservalue(L, 1);  /* L: uv...ct,ct,h,tr (tr ~ {[ct]=h}) */
	lua_insert(L, -3);       /* L: uv...ct,tr,ct,h */
	lua_settable(L, -3);     /* L: uv...ct,tr */
	lua_pop(L, 1);           /* L: uv...ct */
	return h;
}

static void ctI_delthread (lua_State *L) { /* L: uv...ct */
	lua_getuservalue(L, 1);                  /* L: uv...ct,tr (tr ~ {[ct]=h}) */
	lua_pushvalue(L, -2);                    /* L: uv...ct,tr,ct */
	lua_pushvalue(L, -1);                    /* L: uv...ct,tr,ct,ct */
	lua_gettable(L, -3);                     /* L: uv...ct,tr,ct,h */
	lua_pushnil(L);                          /* L: uv...ct,tr,ct,h,nil */
	lua_setuservalue(L, -2);                 /* L: uv...ct,tr,ct,h */
	lua_insert(L, -3);                       /* L: uv...ct,h,tr,ct */
	lua_pushnil(L);                          /* L: uv...ct,h,tr,ct,nil */
	lua_settable(L, -3);                     /* L: uv...ct,h,tr */
	lua_pop(L, 1);                           /* L: uv...ct,h */
}

static void ctI_runthread (lua_State *L, lua_State *ct) {
	uv_handle_t *h;
	int status;
	int c = lua_gettop(ct);         /* ct: ...  | L: uv,trap */
	lua_pushthread(ct);             /* ct: ...ct */
	lua_xmove(ct, L, 1);            /* ct: ...  | L: uv,trap,ct */
	ctI_delthread(L);                          /* L: uv,trap,ct,h */
	status = lua_resume(ct, L, c ? c-1 : 0);
	lua_xmove(L, ct, 1);            /* ct: ...h | L: uv,trap,ct  */
	lua_insert(ct, 1);              /* ct: h... */
	h = (uv_handle_t *)lua_touserdata(ct, 1);
	h->data = NULL;  /* mark as closed */
	if (status == LUA_YIELD) {
		lua_settop(ct, 1);            /* ct: h */
	} else {
		lua_pushvalue(L, 2);                     /* L: uv,trap,ct,trap */
		lua_pushlightuserdata(L, ct);            /* L: uv,trap,ct,trap,tid */
		lua_pushvalue(L, 3);                     /* L: uv,trap,ct,trap,tid,ct */
		if (status == LUA_OK) {
			int nres = lua_gettop(ct) - 1;
			if (!lua_checkstack(L, nres + 1)) {
				lua_pop(ct, nres);        /* ct: h */
				lua_pop(L, 1);                       /* L: uv,trap,ct,trap,tid,ct */
				lua_pushboolean(L, 0);               /* L: uv,trap,ct,trap,tid,ct,ok */
				lua_pushliteral(L, "too many results to trap");
			} else {
				lua_pushboolean(L, 1);               /* L: uv,trap,ct,trap,tid,ct,ok */
				lua_xmove(ct, L, nres);   /* ct: h  |   L: uv,trap,ct,trap,tid,ct,ok,... */
			}
		} else {
			lua_pushboolean(L, 0);                 /* L: uv,trap,ct,trap,tid,ct,ok */
			lua_xmove(ct, L, 1);        /* ct: h  |   L: uv,trap,ct,trap,tid,ct,ok,err */
		}
		lua_pcall(L, lua_gettop(L)-4, 0, 0);     /* L: uv,trap,ct,... */
		/* TODO: handle trap function errors! Should make 'scope:resume' return? */
	}
	lua_settop(L, 2);                          /* L: uv,trap */
}

static void ctI_handled (uv_handle_t *h) {
	lua_State *L = (lua_State *)h->loop->data;
	lua_State *ct = (lua_State *)h->data;
	ctI_runthread(L, ct);
}

static void ctI_idlestarted (uv_idle_t *h) {
	uv_idle_stop(h);
	uv_close((uv_handle_t *)h, ctI_handled);
}

static int ctI_scheduleonce (lua_State *L) {
	uv_loop_t *uv = ct_toloophandle(L);
	lua_State* ct = lua_tothread(L, -1);
	uv_idle_t *h = (uv_idle_t *)ctI_addthread(L);
	int err = uv_idle_init(uv, h);
	if (err >= 0) {
		err = uv_idle_start(h, ctI_idlestarted);
		if (err >= 0) {
			h->data = ct;
			return 0;
		}
		ctI_delthread(L);
		lua_xmove(L, ct, 1);
		uv_close((uv_handle_t *)h, NULL);  /* TODO: is this allowed? */
		h->data = NULL;  /* mark as closed */
	}
	return err;
}

static void ctI_checkerrors (lua_State *L, int err) {
	if (err < 0) luaL_error(L, uv_strerror(err));
}

static void ctI_checkyield (lua_State *L) {
	uv_loop_t *uv = ct_toloophandle(L);
	lua_State *uvL;
	uv_handle_t *h;
	luaL_argcheck(L, uv->data, 1, "inactive scope");
	uvL = (lua_State *)uv->data;
	h = (uv_handle_t *)lua_touserdata(uvL, -1);
	luaL_argcheck(L, h->data == L, 1, "wrong scope");
}


static int ctH_gc (lua_State *L) {
	uv_handle_t *h = (uv_handle_t *)lua_touserdata(L, 1);
	if (h->data) {
		uv_close(h, NULL);  /* TODO: is this allowed? */
		h->data = NULL;  /* mark as closed */
		lua_pushnil(L);
		lua_setmetatable(L, 1);
	}
	return 0;
}

static int ctS_gc (lua_State *L) {
	uv_loop_t *uv = ct_toloophandle(L);
	int err = uv_loop_close(uv);
	ct_assert(err >= 0);
	return 0;
}

static int ctS_create (lua_State *L) {
	uv_handle_t *h;
	lua_State *ct;
	int c;
	luaL_checktype(L, 2, LUA_TFUNCTION);
	c = lua_gettop(L)-1;
	ct = lua_newthread(L);
	if (!lua_checkstack(ct, c+1))
		return luaL_error(L, "too many arguments to create");
	lua_insert(L, 2);  /* move thread below function and args */
	lua_xmove(L, ct, c);  /* move function and args to 'ct' */
	h = (uv_handle_t *)lua_newuserdata(ct, sizeof(union uv_any_handle));
	h->data = NULL;  /* mark as closed */
	luaL_setmetatable(L, CT_HANDLECLS);
	ctI_checkerrors(L, ctI_scheduleonce(L));
	lua_pushlightuserdata(L, ct);
	return 1;
}

static int ctS_resume (lua_State *L) {
	uv_loop_t *uv = ct_toloophandle(L);
	int err;
	luaL_argcheck(L, !uv->data, 1, "already resumed");
	lua_settop(L, 2);  /* set trap function as top */
	uv->data = L;
	err = uv_run(uv, UV_RUN_DEFAULT);
	uv->data = NULL;
	ctI_checkerrors(L, err);
	return 0;
}

static int ctS_running (lua_State *L) {
	uv_loop_t *uv = ct_toloophandle(L);
	lua_State *uvL = (lua_State *)uv->data;
	if (uvL && uvL != L) { /* resumed and not inside a trap func */
		uv_handle_t *h = (uv_handle_t *)lua_touserdata(uvL, -1);
		if (h->data == L) {
			lua_pushlightuserdata(L, L);
			return 1;
		}
	}
	return 0;
}

static int ctS_yield (lua_State *L) {
	ctI_checkyield(L);  /* uv,... */
	lua_settop(L, 1);   /* uv */
	lua_pushthread(L);  /* uv,ct */
	ctI_checkerrors(L, ctI_scheduleonce(L));
	return lua_yield(L, 0);
}


static int ct_newscope (lua_State *L) {
	uv_loop_t *uv = (uv_loop_t *)lua_newuserdata(L, sizeof(uv_loop_t));
	ctI_checkerrors(L, uv_loop_init(uv));
	lua_newtable(L);  /* push the thread registration table */
	lua_setuservalue(L, -2);
	luaL_setmetatable(L, CT_SCOPECLS);
	return 1;
}


static const luaL_Reg handlecls[] = {
	{"__gc", ctH_gc},
	{NULL, NULL}
};

static const luaL_Reg scopecls[] = {
	{"__gc", ctS_gc},
	{"create", ctS_create},
	{"resume", ctS_resume},
	{"running", ctS_running},
	{"yield", ctS_yield},
	{NULL, NULL}
};


LUAMOD_API int luaopen_cothread_scope (lua_State *L)
{
	luaL_newmetatable(L, CT_HANDLECLS);
	luaL_setfuncs(L, handlecls, 0);

	luaL_newmetatable(L, CT_SCOPECLS);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	luaL_setfuncs(L, scopecls, 0);

	lua_pushcfunction(L, ct_newscope);
	return 1;
}
