#include "Table.hpp"
#include <lualib.h>
#include "../../Dependencies/Luau/VM/src/lstate.h"

#define RegisterFunction(L, Func, Name) lua_pushcclosure(L, Func, Name, 0); \
lua_setglobal(L, Name);

int custom_getrawmetatable(lua_State* L) {
	luaL_checkany(L, 1);

	if (!lua_getmetatable(L, 1))
		lua_pushnil(L);

	return 1;
}
int custom_setrawmetatable(lua_State* L) {
	luaL_checkany(L, 1);
	luaL_checktype(L, 2, LUA_TTABLE);

	lua_setmetatable(L, 1);
	lua_pushvalue(L, 1);

	return 1;
}


auto setreadonly(lua_State* rl) -> int {
	luaL_checktype(rl, 2, LUA_TBOOLEAN);
	lua_setreadonly(rl, 1, lua_toboolean(rl, 2));
	return 0;
}


int custom_isreadonly(lua_State* L) {
	luaL_checktype(L, 1, LUA_TTABLE);

	lua_pushboolean(L, hvalue(luaA_toobject(L, 1))->readonly);
	return 1;
};


void api::Environment::MetaTable::Register(lua_State* L)
{

	RegisterFunction(L, custom_getrawmetatable, "getrawmetatable");
	RegisterFunction(L, custom_setrawmetatable, "setrawmetatable");


	RegisterFunction(L, custom_setrawmetatable, "setrawmetatable");
	RegisterFunction(L, setreadonly, "setreadonly");
	RegisterFunction(L, custom_isreadonly, "isreadonly");


}
