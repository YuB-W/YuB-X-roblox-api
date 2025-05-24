#include "Cache.hpp"
#include "RBX.hpp"
#include "../../Dependencies/Luau/VM//src/lgc.h"
#include "../../Dependencies/Luau/VM//src/lapi.h"
#include <lualib.h>
#include "../../../Dependencies/Base64/Base64.h"
#include <lua.h>

#define RegisterFunction(L, Func, Name) lua_pushcclosure(L, Func, Name, 0); \
lua_setglobal(L, Name);

#define RegisterMember(L, Func, Name) lua_pushcclosure(L, Func, Name, 0); \
lua_setfield(L, -2, Name);

int Invalidate(lua_State* L)
{

	auto Ud = lua_touserdata(L, 1);

	auto RawUd = *reinterpret_cast<uintptr_t**>(Ud);

	lua_pushlightuserdata(L, reinterpret_cast<void*>(Update::PushInstance));
	lua_rawget(L, LUA_REGISTRYINDEX);

	lua_pushlightuserdata(L, RawUd);
	lua_pushnil(L);
	lua_rawset(L, -3);

	return 0;
}

int cloneref(lua_State* L)
{
    auto Ud = lua_touserdata(L, 1);
    auto RawUd = *static_cast<void**>(Ud);

    lua_pushlightuserdata(L, reinterpret_cast<void*>(Update::PushInstance)); // Roblox uses the pushinstance address for the globals list

    lua_rawget(L, LUA_REGISTRYINDEX);
    lua_pushlightuserdata(L, RawUd); // Let's check if the value exists in the registry
    lua_rawget(L, -2); // Grab the reg

    lua_pushlightuserdata(L, RawUd);
    lua_pushnil(L); // If the value exists, we need to set the ref to nil
    lua_rawset(L, -4);

    reinterpret_cast<void(__fastcall*)(lua_State*, void*)>(Update::PushInstance)(L, Ud);
    // Now once we are done, we need to restore the instance
    lua_pushlightuserdata(L, RawUd);
    lua_pushvalue(L, -3);
    lua_rawset(L, -5);

    return 1;
}

void api::Environment::Cache::Register(lua_State* L)
{
	lua_newtable(L);
	RegisterMember(L, Invalidate, "invalidate");
	lua_setglobal(L, "cache");

	RegisterFunction(L, cloneref, "cloneref");
}