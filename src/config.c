#include "blaze.h"

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

        lua_pop(config.L, 1);
    }

    return config;
}

void free_config(Config config) {
    lua_close(config.L);
}
