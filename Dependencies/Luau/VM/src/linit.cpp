// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
// This code is based on Lua 5.x implementation licensed under MIT License; see lua_LICENSE.txt for details
#include "lualib.h"

#include <stdlib.h>
#include <ldo.h>
#include <lgc.h>

static const luaL_Reg lualibs[] = {
    {"", luaopen_base},
    {LUA_COLIBNAME, luaopen_coroutine},
    {LUA_TABLIBNAME, luaopen_table},
    {LUA_OSLIBNAME, luaopen_os},
    {LUA_STRLIBNAME, luaopen_string},
    {LUA_MATHLIBNAME, luaopen_math},
    {LUA_DBLIBNAME, luaopen_debug},
    {LUA_UTF8LIBNAME, luaopen_utf8},
    {LUA_BITLIBNAME, luaopen_bit32},
    {LUA_BUFFERLIBNAME, luaopen_buffer},
    {NULL, NULL},
};

void luaL_openlibs(lua_State* L)
{
    const luaL_Reg* lib = lualibs;
    for (; lib->func; lib++)
    {
        lua_pushcfunction(L, lib->func, NULL);
        lua_pushstring(L, lib->name);
        lua_call(L, 1, 0);
    }
}

void luaL_sandbox(lua_State* L)
{
    // set all libraries to read-only
    lua_pushnil(L);
    while (lua_next(L, LUA_GLOBALSINDEX) != 0)
    {
        if (lua_istable(L, -1))
            lua_setreadonly(L, -1, true);

        lua_pop(L, 1);
    }

    // set all builtin metatables to read-only
    lua_pushliteral(L, "");
    if (lua_getmetatable(L, -1))
    {
        lua_setreadonly(L, -1, true);
        lua_pop(L, 2);
    }
    else
    {
        lua_pop(L, 1);
    }

    // set globals to readonly and activate safeenv since the env is immutable
    lua_setreadonly(L, LUA_GLOBALSINDEX, true);
    lua_setsafeenv(L, LUA_GLOBALSINDEX, true);
}

#define LUA_WHITE0 0x01
#define LUA_WHITE1 0x02
#define LUA_BLACK  0x04

#define LUA_WHITEBITS (LUA_WHITE0 | LUA_WHITE1)


#define iswhite(x)  (((x)->gch.marked) & LUA_WHITEBITS)
#define isblack(x)  (((x)->gch.marked) & LUA_BLACK)
#define isgray(x)   (!isblack(x) && !iswhite(x))

#define luaC_barrier(L, p, v) \
    { \
        if (iscollectable(v) && isblack(obj2gco(p)) && iswhite(obj2gco(v))) \
            luaC_barrierf(L, obj2gco(p), obj2gco(v)); \
    }


void luaL_sandboxthread(lua_State* L)
{
    lua_newtable(L);

    lua_newtable(L);

    sethvalue(L, L->top, L->gt);
    incr_top(L);

    lua_setfield(L, -2, "__index"); 


    lua_setreadonly(L, -1, true);

    lua_setmetatable(L, -2);

    Table* newenv = hvalue(L->top - 1);
    L->gt = newenv;

    luaC_barrier(L, L, newenv);

    L->gt->safeenv = 1;

    lua_pop(L, 1);
}

static void* l_alloc(void* ud, void* ptr, size_t osize, size_t nsize)
{
    (void)ud;
    (void)osize;
    if (nsize == 0)
    {
        free(ptr);
        return NULL;
    }
    else
        return realloc(ptr, nsize);
}

lua_State* luaL_newstate(void)
{
    return lua_newstate(l_alloc, NULL);
}
