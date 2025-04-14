#include "Instance.hpp"
#include "RBX.hpp"
#include "../../Dependencies/Luau/VM//src/lgc.h"
#include "../../Dependencies/Luau/VM//src/lapi.h"
#include <lualib.h>
#include "../../../Dependencies/Base64/Base64.h"
#include "../../../Dependencies/Luau/VM/src/lmem.h"

#define RegisterFunction(L, Func, Name) lua_pushcclosure(L, Func, Name, 0); \
lua_setglobal(L, Name);

#define RegisterMember(L, Func, Name) lua_pushcclosure(L, Func, Name, 0); \
lua_setfield(L, -2, Name);

int GetInstances(lua_State* L)
{
	lua_pushlightuserdata(L, reinterpret_cast<void*>(Update::DataModel::Information::PushInstance)); // Roblox uses the pushinstance address for the globals list

	lua_rawget(L, LUA_REGISTRYINDEX);

	lua_newtable(L);

	lua_pushnil(L);

	auto Index = 0;

	while (!lua_next(L, -3))
	{
		if (!lua_isnil(L, -2))
		{
			lua_getglobal(L, "typeof");
			lua_pushvalue(L, -2);
			lua_pcall(L, 1, 1, 0);

			auto TypeName = lua_tostring(L, -1);

			lua_pop(L, 1);

			if (TypeName == "Instance")
			{
				lua_rawseti(L, -3, ++Index);
			}
			else
			{
				lua_pop(L, 1);
			}
		}

	}

	lua_pushvalue(L, -2);

	return 3;
}

void api::Environment::Instance::Register(lua_State* L)
{
	RegisterFunction(L, GetInstances, "getinstances");
}