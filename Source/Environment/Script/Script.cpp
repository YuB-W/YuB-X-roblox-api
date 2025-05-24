#include "Script.hpp"
#include <Luau/Compiler.h>
#include <cpr/cpr.h>
#include "RBX.hpp"
#include "../../Dependencies/Luau/VM/src/lgc.h"
#include "../../Dependencies/Luau/VM/src/lapi.h"
#include <lualib.h>
#include "../../../Dependencies/Luau/VM/src/lmem.h"
#include <lua.h>

#define RegisterFunction(L, Func, Name) lua_pushcclosure(L, Func, Name, 0); \
lua_setglobal(L, Name);

#define RegisterMember(L, Func, Name) lua_pushcclosure(L, Func, Name, 0); \
lua_setfield(L, -2, Name);

typedef int (*lua_CFunction)(lua_State* L);
lua_CFunction OldIndex;
lua_CFunction OldNamecall;

inline TValue* index2addr(lua_State* L, int idx) {
    return L->base + (idx - 1);
}

int GetGenv(lua_State* L)
{
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    return 1;
}

int GetRenv(lua_State* L)
{
    lua_pushvalue(reinterpret_cast<lua_State*>(RBX::DecryptLuaState(reinterpret_cast<uintptr_t>(L))), LUA_GLOBALSINDEX);
    lua_xmove(reinterpret_cast<lua_State*>(RBX::DecryptLuaState(reinterpret_cast<uintptr_t>(L))), L, 1);
    return 1;
}

int HttpGet(lua_State* L)
{
    auto Url = lua_tostring(L, 1);
    auto Result = cpr::Get(cpr::Url{ Url });
    lua_pushstring(L, Result.text.c_str());
    return 1;
}

int GetObjects(lua_State* L)
{
    lua_getglobal(L, "game");
    lua_getfield(L, -1, "GetService");
    lua_pushvalue(L, -2);
    lua_pushstring(L, "InsertService");
    lua_pcall(L, 2, 1, 0);

    lua_getfield(L, -1, "LoadLocalAsset");
    lua_pushvalue(L, -2);
    lua_pushstring(L, lua_tostring(L, 1));
    lua_pcall(L, 2, 1, 0);

    lua_newtable(L);
    lua_pushvalue(L, -2);
    lua_rawseti(L, -2, 1);
    return 1;
}

int CompareInstances(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TUSERDATA);
    luaL_checktype(L, 2, LUA_TUSERDATA);

    auto Inst1 = reinterpret_cast<std::uintptr_t*>(lua_touserdata(L, 1));
    auto Inst2 = reinterpret_cast<std::uintptr_t*>(lua_touserdata(L, 2));

    if (!Inst1 || !Inst2)
    {
        luaL_error(L, "Invalid userdata provided.");
        return 0;
    }

    lua_pushboolean(L, (*Inst1 == *Inst2));
    return 1;
}


typedef struct
{
    lua_State* pLua;
    bool accessTables;
    int itemsFound;
} GCOContext;

int GetGc(lua_State* L)
{
    auto AccessTables = lua_toboolean(L, 1);
    lua_newtable(L);

    GCOContext Gcc{ L, AccessTables, 0 };
    auto OldGcState = L->global->gcstate;
    L->global->gcstate = -1;

    luaM_visitgco(L, &Gcc, [](void* context, lua_Page* page, GCObject* gco) {
        auto Ctx = reinterpret_cast<GCOContext*>(context);
        lua_State* GL = Ctx->pLua;

        if (isdead(GL->global, gco))
            return false;

        if (gco->gch.tt == LUA_TFUNCTION || gco->gch.tt == LUA_TUSERDATA || (gco->gch.tt == LUA_TTABLE && Ctx->accessTables))
        {
            GL->top->value.gc = gco;
            GL->top->tt = gco->gch.tt;
            incr_top(GL);
            lua_rawseti(GL, -2, ++Ctx->itemsFound);
        }

        return false;
        });

    L->global->gcstate = OldGcState;
    return 1;
}

int GetNameCallMethod(lua_State* L)
{
    if (L->namecall == nullptr || L->namecall->data == nullptr)
    {
        luaL_error(L, "namecall or its data is nil.");
        return 0;
    }

    lua_pushstring(L, L->namecall->data);  
    return 1;
}


int NewIndex(lua_State* L)
{
    if (RBX::GetThreadIdentity(reinterpret_cast<uintptr_t>(L)) == 8)
    {
        auto Method = std::string(lua_tostring(L, 2));

        if (Method == "HttpGet" || Method == "HttpGetAsync")
        {
            lua_pushcclosure(L, HttpGet, nullptr, 0);
            return 1;
        }
        else if (Method == "GetObjects")
        {
            lua_pushcclosure(L, GetObjects, nullptr, 0);
            return 1;
        }
    }
    return OldIndex(L);
}

int NewNamecall(lua_State* L)
{
    if (RBX::GetThreadIdentity(reinterpret_cast<uintptr_t>(L)) == 8)
    {
        auto NM = std::string(L->namecall->data);

        if (NM == "HttpGet" || NM == "HttpGetAsync")
        {
            lua_pushcclosure(L, HttpGet, nullptr, 0);
            lua_pushstring(L, lua_tostring(L, 2));
            lua_pcall(L, 1, 1, 0);
            return 1;
        }
        else if (NM == "GetObjects")
        {
            lua_pushcclosure(L, GetObjects, nullptr, 0);
            lua_pushstring(L, lua_tostring(L, 2));
            lua_pcall(L, 1, 1, 0);
            return 1;
        }
    }
    return OldNamecall(L);
}



void api::Environment::Script::Register(lua_State* L)
{
    RegisterFunction(L, GetGenv, "getgenv");
    RegisterFunction(L, GetNameCallMethod, "getnamecallmethod");
    RegisterFunction(L, CompareInstances, "compareinstances");
    RegisterFunction(L, GetGc, "getgc");

    lua_pop(L, lua_gettop(L));

    lua_getglobal(L, "game");
    lua_getmetatable(L, -1);

    lua_rawgetfield(L, -1, "__namecall");

  
    //auto Cl = clvalue(index2addr(L, -1));
    //OldNamecall = Cl->c.f;
    //Cl->c.f = NewNamecall;
    //lua_pop(L, 1);
   

  
    //lua_rawgetfield(L, -1, "__index");
    //auto Cl2 = clvalue(index2addr(L, -1));
    //OldIndex = Cl2->c.f;
    //Cl2->c.f = NewIndex;
    //lua_pop(L, 1);

    //lua_pop(L, 2);
    
}
