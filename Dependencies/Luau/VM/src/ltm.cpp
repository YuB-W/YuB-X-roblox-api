// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
// This code is based on Lua 5.x implementation licensed under MIT License; see lua_LICENSE.txt for details
#include "ltm.h"

#include "lstate.h"
#include "lstring.h"
#include "ludata.h"
#include "ltable.h"
#include "lgc.h"

#include <string.h>

// clang-format off
const char* const luaT_typenames[] = {
    // ORDER TYPE
    "nil",
    "boolean",

    LUAU_SHUFFLE3(LUAU_COMMA_SEP,
    "userdata",
    "number",
    "vector"),


    "string",

    LUAU_SHUFFLE5(LUAU_COMMA_SEP,
    "table",
    "function",
    "userdata",
    "thread",
    "buffer"),
};
const char* const luaT_eventname[] = {
    // ORDER TM
    LUAU_SHUFFLE7(LUAU_COMMA_SEP,
    "__index",    // a1
    "__newindex", // a2
    "__mode",     // a3
    "__namecall", // a4
    "__call",     // a5
    "__iter",     // a6
    "__len"),     // a7

    "__eq",

    LUAU_SHUFFLE8(LUAU_COMMA_SEP,
    "__add", // a1
    "__sub", // a2
    "__mul", // a3
    "__div", // a4
    "__idiv",// a5
    "__mod", // a6
    "__pow", // a7
    "__unm"),// a8

    LUAU_SHUFFLE5(LUAU_COMMA_SEP,
    "__lt",
    "__le",
    "__concat",
    "__type",
    "__metatable"),
};
// clang-format on

static_assert(sizeof(luaT_typenames) / sizeof(luaT_typenames[0]) == LUA_T_COUNT, "luaT_typenames size mismatch");
static_assert(sizeof(luaT_eventname) / sizeof(luaT_eventname[0]) == TM_N, "luaT_eventname size mismatch");
static_assert(TM_EQ < 8, "fasttm optimization stores a bitfield with metamethods in a byte");

void luaT_init(lua_State* L)
{
    int i;
    for (i = 0; i < LUA_T_COUNT; i++)
    {
        L->global->ttname[i] = luaS_new(L, luaT_typenames[i]);
        luaS_fix(L->global->ttname[i]); // never collect these names
    }
    for (i = 0; i < TM_N; i++)
    {
        L->global->tmname[i] = luaS_new(L, luaT_eventname[i]);
        luaS_fix(L->global->tmname[i]); // never collect these names
    }
}

/*
** function to be used with macro "fasttm": optimized for absence of
** tag methods.
*/
const TValue* luaT_gettm(LuaTable* events, TMS event, TString* ename)
{
    const TValue* tm = luaH_getstr(events, ename);
    LUAU_ASSERT(event <= TM_EQ);
    if (ttisnil(tm))
    {                                              // no tag method?
        events->tmcache |= cast_byte(1u << event); // cache this fact
        return NULL;
    }
    else
        return tm;
}

const TValue* luaT_gettmbyobj(lua_State* L, const TValue* o, TMS event)
{
    /*
      NB: Tag-methods were replaced by meta-methods in Lua 5.0, but the
      old names are still around (this function, for example).
    */
    LuaTable* mt;
    switch (ttype(o))
    {
    case LUA_TTABLE:
        mt = hvalue(o)->metatable;
        break;
    case LUA_TUSERDATA:
        mt = uvalue(o)->metatable;
        break;
    default:
        mt = L->global->mt[ttype(o)];
    }
    return (mt ? luaH_getstr(mt, L->global->tmname[event]) : luaO_nilobject);
}

const TString* luaT_objtypenamestr(lua_State* L, const TValue* o)
{
    // Userdata created by the environment can have a custom type name set in the individual metatable
    // If there is no custom name, 'userdata' is returned
    if (ttisuserdata(o) && uvalue(o)->tag != UTAG_PROXY && uvalue(o)->metatable)
    {
        const TValue* type = luaH_getstr(uvalue(o)->metatable, L->global->tmname[TM_TYPE]);

        if (ttisstring(type))
            return tsvalue(type);

        return L->global->ttname[ttype(o)];
    }

    // Tagged lightuserdata can be named using lua_setlightuserdataname
    if (ttislightuserdata(o))
    {
        int tag = lightuserdatatag(o);

        if (unsigned(tag) < LUA_LUTAG_LIMIT)
        {
            if (const TString* name = L->global->lightuserdataname[tag])
                return name;
        }
    }

    // For all types except userdata and table, a global metatable can be set with a global name override
    if (LuaTable* mt = L->global->mt[ttype(o)])
    {
        const TValue* type = luaH_getstr(mt, L->global->tmname[TM_TYPE]);

        if (ttisstring(type))
            return tsvalue(type);
    }

    return L->global->ttname[ttype(o)];
}

const char* luaT_objtypename(lua_State* L, const TValue* o)
{
    return getstr(luaT_objtypenamestr(L, o));
}
