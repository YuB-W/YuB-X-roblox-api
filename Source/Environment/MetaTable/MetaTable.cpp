#include "Table.hpp"
#include <lualib.h>
#include "../../Dependencies/Luau/VM/src/lstate.h"

#define RegisterFunction(L, Func, Name) lua_pushcclosure(L, Func, Name, 0); \
lua_setglobal(L, Name);


int GetRawMetaTable(lua_State* L)
{
	lua_getmetatable(L, 1);
	return 1; // Return the table
}

int SetRawMetaTable(lua_State* L)
{
	lua_setmetatable(L, 1);
	return 0; // Return the table
}

int SetReadOnly(lua_State* L)
{
	lua_setreadonly(L, 1, lua_toboolean(L, 2));
	return 0;
}

int IsReadOnly(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TTABLE);

	lua_pushboolean(L, lua_getreadonly(L, 1));
	return 1;
}

void api::Environment::MetaTable::Register(lua_State* L)
{
	RegisterFunction(L, GetRawMetaTable, "getrawmetatable");
	RegisterFunction(L, SetRawMetaTable, "setrawmetatable");
	RegisterFunction(L, SetReadOnly, "setreadonly");
	RegisterFunction(L, IsReadOnly, "isreadonly");


}