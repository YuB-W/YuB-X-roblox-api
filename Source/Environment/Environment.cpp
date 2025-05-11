#include "Environment.hpp"

// UNC INCLUDES
#include "Closure/Closure.hpp"
#include "Crypt/Crypt.hpp"
#include "Script/Script.hpp"
#include "FileSystem/FileSys.hpp"
#include "MetaTable/Table.hpp"
#include "Extra/Extra.hpp"
#include "Input/Input.hpp"
#include "Cache/Cache.hpp"
#include "Instance/Instance.hpp"
#include "Http/Http.hpp"
#include <WinUser.h>
#include "ResourceManager.hpp"

#include "Library.hpp"




void CEnvironment::Initialize(lua_State* L) {

    
    //api::Environment::Debug::RegisterFunctions(L);

    api::Environment::Script::Register(L); // getgenv, **getrenv**, getnamecallmethod, compareinstances, getgc §HttpGet§
    api::Environment::FileSystem::Register(L); // writefile, readfile, deletefile, makefolder, appendfile, isfolder, isfile
    api::Environment::MetaTable::Register(L); // getrawmetatable, setrawmetatable, setreadonly, isreadonly
    api::Environment::Extra::Register(L); // setclipboard, messagebox, **setfpscap**, identifyexecutor, "openprocess", setidentity, setthreadidentity, getidentity, getthreadidentity
    api::Environment::Crypt::Register(L); // base64encode, base64decode, base64_encode, base64_decode
    api::Environment::Cl::Register(L); // iscclosure, islclosure, clonefunction, loadstring, newcclosure, checkcaller, hookfunction
    api::Environment::Input::Register(L); // mouse1click, mouse2click, mouse1press, mouse2press, mouse1release, mouse2release, mousemoveabs, mousemoverel, keypress, keyrelease, isrbxactive
    api::Environment::Cache::Register(L); // DISABLED: no pushinstance offset required.
    api::Environment::Instance::Register(L); // DISABLED: no pushinstance offset required.
    api::Environment::Http::Register(L); // httpGet

    const auto rL = lua_newthread(L);
    lua_ref(L, -1);
    lua_pop(L, 1);

    lua_newtable(L);
    lua_setglobal(L, xorstr_("_G"));
    lua_getglobal(L, xorstr_("_G"));
    lua_setglobal(L, xorstr_("shared"));

    lua_pushvalue(L, LUA_GLOBALSINDEX);
    lua_setglobal(L, xorstr_("_ENV"));

    const auto nL = lua_newthread(L);
    lua_ref(L, -1);
    lua_pop(L, 1);

    luaL_sandboxthread(nL);

    lua_newtable(nL);
    lua_setglobal(nL, "_G");

    lua_newtable(nL);
    lua_setglobal(nL, "shared");

}
