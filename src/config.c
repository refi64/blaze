/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "blaze.h"

static lua_State* setup_lua() {
    lua_State* L = luaL_newstate();
    if (L == NULL) return L;
    luaL_openlibs(L);

    lua_newtable(L);
    #define F(f,s) do {\
        lua_pushstring(L, #f);\
        lua_pushstring(L, s);\
        lua_settable(L, -3);\
    } while (0)
    F(compiler, "cc");
    F(cflags, "");
    F(kind, "unix");
    F(lightbuild, "lightbuild");
    #undef F
    lua_setglobal(L, "config");
    return L;
}

static void run_lua_file(lua_State* L, const char* path) {
    if (luaL_dofile(L, path)) {
        fprintf(stderr, "error running %s: %s\n", path, lua_tostring(L, -1));
        lua_pop(L, -1);
    }
}

static String* get_main_config() {
    /* String* res; */
    /* const char* home = getenv("HOME"); */
    /* if (!home) return NULL; */
    /* res = string_new(home); */
    /* string_mergec(res, '/'); */
    /* string_merges(res, ".blaze.lua"); */
    /* return res; */

    return string_new("/home/ryan/blaze/config.lua");
}

Config load_config() {
    Config config;
    String* config_script;

    config.L = setup_lua();
    config_script = get_main_config();

    if (config_script) {
        if (exists(config_script->str)) run_lua_file(config.L, config_script->str);
        string_free(config_script);
    }

    lua_getglobal(config.L, "config");
    luaL_checktype(config.L, -1, LUA_TTABLE);

    lua_pushnil(config.L);
    while (lua_next(config.L, -2)) {
        luaL_checktype(config.L, -2, LUA_TSTRING);
        luaL_checktype(config.L, -1, LUA_TSTRING);
        /* printf("%s - %s\n", lua_tostring(config.L, -2), */
                            /* lua_tostring(config.L, -1)); */

        #define F(n,x) else if (strcmp(lua_tostring(config.L, -2), #n) == 0)\
                         config.n##x = lua_tostring(config.L, -1);
        // This should ALWAYS be false.
        if (!config.L) fatal("XXX");
        F(compiler,)
        F(cflags,)
        F(kind,_string)
        F(lightbuild,)
        #undef F

        lua_pop(config.L, 1);
    }

    #define K(k) else if (strcmp(config.kind_string, #k) == 0) config.kind = C##k;
    // Again, always false.
    if (!config.L) fatal("XXX");
    K(unix)
    K(clang)
    else {
        fprintf(stderr, "unknown compiler kind %s\n", config.kind_string);
        config.kind = Cunix;
    }

    return config;
}

void free_config(Config config) {
    lua_close(config.L);
}
