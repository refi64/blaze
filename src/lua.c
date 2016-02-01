#include "blaze.h"

lua_State* setup_lua() {
    lua_State* L = luaL_newstate();
    if (L == NULL) return L;
    luaL_openlibs(L);

    lua_newtable(L);
    #define F(f,t,...) do {\
        lua_pushstring(L, #f);\
        lua_push##t(L, __VA_ARGS__);\
        lua_settable(L, -3);\
    } while (0)
    F(compiler, string, "cc");
    lua_setglobal(L, "config");
    return L;
}

void run_lua_file(lua_State* L, const char* path) {
    if (luaL_dofile(L, path)) {
        fprintf(stderr, "error running %s: %s\n", path, lua_tostring(L, -1));
        lua_pop(L, -1);
    }
}
