#include "Crypt.hpp"
#include "RBX.hpp"
#include "../../Dependencies/Luau/VM//src/lgc.h"
#include "../../Dependencies/Luau/VM//src/lapi.h"
#include <lualib.h>
#include <Base64/Base64.h>



#define oxorany(x) x 
#define RegisterFunction(L, Func, Name) lua_pushcclosure(L, Func, Name, 0); \
lua_setglobal(L, Name);

#define RegisterMember(L, Func, Name) lua_pushcclosure(L, Func, Name, 0); \
lua_setfield(L, -2, Name);

int B64Encode(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TSTRING);

	auto Str = lua_tostring(L, 1);

	lua_pushstring(L, base64_encode(Str).c_str());
	return 1;
}

int B64Decode(lua_State* L)
{
	luaL_checktype(L, 1, LUA_TSTRING);

	auto Str = lua_tostring(L, 1);

	lua_pushstring(L, base64_decode(Str).c_str());
	return 1;
}

void api::Environment::Crypt::Register(lua_State* L)
{
	lua_newtable(L);
	RegisterMember(L, B64Encode, oxorany("base64encode"));
	RegisterMember(L, B64Decode, oxorany("base64decode"));
	lua_setglobal(L, oxorany("crypt"));

	RegisterFunction(L, B64Encode, "base64_encode");
	RegisterFunction(L, B64Decode, "base64_decode");

}