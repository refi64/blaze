#include "blaze.h"

static lua_State* setup_lua() {
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
        printf("%s - %s\n", lua_tostring(config.L, -2),
                            lua_tostring(config.L, -1));

        #define F(n) else if (strcmp(lua_tostring(config.L, -2), #n) == 0)\
                         config.n = lua_tostring(config.L, -1);
        // This should ALWAYS be false.
        if (!config.L) fatal("XXX");
        F(compiler)
        #undef F

        lua_pop(config.L, 1);
    }

    return config;
}

void free_config(Config config) {
    lua_close(config.L);
}