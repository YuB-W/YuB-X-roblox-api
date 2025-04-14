#include "Closure.hpp"
#include "RBX.hpp"
#include "../../Dependencies/Luau/VM/src/lgc.h"
#include "../../Dependencies/Luau/VM/src/lapi.h"
#include <lualib.h>
#include "../Dependencies/Base64/Base64.h"
#include <Luau/Compiler.h>
#include "../../../Dependencies/Luau/VM/src/lfunc.h"
#include <lua.h>

std::map<Closure*, int> ClMap{};
#define xorstr_(str) (str)
#undef LoadString 
#define RegisterFunction(L, Func, Name) lua_pushcclosure(L, Func, Name, 0); lua_setglobal(L, Name);
#define oxorany(x) x  // Define this properly based on its intended functionality
#define ObfStr(x) (x) 

static TValue* index2addr(lua_State* L, int idx) {
    return (idx > 0) ? &L->base[idx - 1] : &L->top[idx];
}

int IsCClosure(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_pushboolean(L, lua_iscfunction(L, 1));
    return 1;
}

int IsLClosure(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_pushboolean(L, lua_isLfunction(L, 1));
    return 1;
}

int CloneFunction(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_clonefunction(L, 1);
    return 1;
}

static uintptr_t MaxCapabilities = 0x200000000000003FLL | 0x3FFFFFF00LL;


int ExecuteCaptured(lua_State* L)
{
    const char* code = lua_tostring(L, lua_upvalueindex(1)); 
    if (!code)
    {
        luaL_error(L, "No code found in upvalue.");
        return 0;
    }

    if (Execution && Manager)
    {
        Execution->Send(Manager->GetLuaState(), code); 
    }
    else
    {
        luaL_error(L, "Invalid Execution or Manager state.");
    }
    return 0;
}


int Loadstring(lua_State* L)
{
    const char* code = luaL_checkstring(L, 1);

    if (!code)
    {
        luaL_error(L, "Invalid code string.");
        return 0;
    }

    lua_pushstring(L, code);

    lua_pushcclosure(L, ExecuteCaptured, "ExecuteCapturedClosure", 1); 

    return 1;
}


int CheckCaller(lua_State* L) {
    lua_pushboolean(L, (RBX::GetThreadIdentity((uintptr_t)L) == 8));
    return 1;
}

int Blank(lua_State* L) {
    return 0;
}

int NewCCHandler(lua_State* L) {
    CallInfo* Ci = L->ci;
    Closure* CurrentClosure = clvalue(Ci->func);
    auto Args = lua_gettop(L);
    auto SavedCl = ClMap[CurrentClosure];
    lua_getref(L, SavedCl);
    lua_insert(L, 1);
    int Result = lua_pcall(L, Args, LUA_MULTRET, 0);
    if (Result != 0) {
        auto Error = lua_tostring(L, -1);
        if (Error == "attempt to yield across metamethod/C-call boundary") {
            return lua_yield(L, 0);
        }
        luaL_error(L, Error);
    }
    return lua_gettop(L);
}

int NewCClosure(lua_State* L) {
    Closure* Cl = clvalue(index2addr(L, 1));
    if (Cl->isC) {
        lua_pushvalue(L, 1);
        return 1;
    }
    int Ref = lua_ref(L, 1);
    lua_pushcclosure(L, NewCCHandler, "", 0);
    Closure* Cl2 = clvalue(index2addr(L, -1));
    ClMap[Cl2] = Ref;
    return 1;
}

std::unordered_map<Closure*, __int64> CClosures;

int NewCClosureStub(lua_State* L)
{
    const auto Nargs = lua_gettop(L);
    int LClosure = CClosures.find(clvalue(L->ci->func))->second;
    if (!LClosure)
        return 0;

    lua_getref(L, LClosure);
    lua_insert(L, 1);

    const char* Error;
    const auto Res = lua_pcall(L, Nargs, LUA_MULTRET, 0);

    if (Res && Res != LUA_YIELD && (Error = lua_tostring(L, -1), !strcmp(Error, xorstr_("attempt to yield across metamethod/C-call boundary"))))
        return lua_yield(L, 0);

    if (Res != LUA_OK)
    {
        luaL_error(L, "%s", lua_tostring(L, -1));
        return 0;
    }
    return lua_gettop(L);
}


int HookFunction(lua_State* L)
{
    luaL_stackcheck(L, 2, 2, luaL_checktype(L, 1, LUA_TFUNCTION););
    luaL_checktype(L, 2, LUA_TFUNCTION);

    auto Function = lua_toclosure(L, 1);
    auto Hook = lua_toclosure(L, 2);

    if (Function->isC)
    {
        const auto HookRef = lua_ref(L, 2);

        if (!Hook->isC)
        {
            lua_pushcclosure(L, NewCClosureStub, 0, 0);
            CClosures[&luaA_toobject(L, -1)->value.gc->cl] = HookRef;
            Hook = lua_toclosure(L, -1);
            lua_ref(L, -1);
            lua_pop(L, 1);
        }

        lua_CFunction Func1 = Hook->c.f;
        lua_clonefunction(L, 1);
        Function->c.f = [](lua_State* L) -> int { return 0; };

        for (auto i = 0; i < Hook->nupvalues; i++)
        {
            auto OldTValue = &Function->c.upvals[i];
            auto HookTValue = &Hook->c.upvals[i];

            OldTValue->value = HookTValue->value;
            OldTValue->tt = HookTValue->tt;
        }

        auto ClosureRef = CClosures.find(Function)->second;
        if (ClosureRef != 0)
        {
            CClosures[Function] = HookRef;
            CClosures[clvalue(luaA_toobject(L, -1))] = ClosureRef;
        }

        Function->nupvalues = Hook->nupvalues;
        Function->c.f = Func1;

        return 1;
    }
    else
    {
        if (Hook->isC)
        {
            lua_newtable(L);
            lua_newtable(L);

            lua_pushvalue(L, LUA_GLOBALSINDEX);
            lua_setfield(L, -2, xorstr_("__index"));
            lua_setreadonly(L, -1, true);

            lua_setmetatable(L, -2);

            lua_pushvalue(L, 2);
            lua_setfield(L, -2, xorstr_("cFuncCall"));

            static auto Encoder = CBytecodeEncoder();
            const auto Bytecode = Luau::compile(xorstr_("return cFuncCall(...)"), {}, {}, &Encoder);
            luau_load(L, "", Bytecode.c_str(), Bytecode.size(), -1);
            Hook = lua_toclosure(L, -1);
        }

        Proto* nProto = Hook->l.p;
        lua_clonefunction(L, 1);

        Function->env = Hook->env;

        Function->stacksize = Hook->stacksize;
        Function->preload = Hook->preload;

        for (auto i = 0; i < Hook->nupvalues; ++i)
            setobj2n(L, &Function->l.uprefs[i], &Hook->l.uprefs[i]);

        Function->nupvalues = Hook->nupvalues;
        Function->l.p = nProto;

        return 1;
    }
    return 0;
}

int HookMetaMethod(lua_State* L)
{
    // Argument check: (object, metamethodName, hookFunction)
    luaL_checkany(L, 1); // Accept Instance/userdata/table
    luaL_checktype(L, 2, LUA_TSTRING); // "__index", "__namecall", etc.
    luaL_checktype(L, 3, LUA_TFUNCTION); // Function to hook

    const char* methodName = lua_tostring(L, 2);

    // Get or create metatable of the object
    if (!lua_getmetatable(L, 1)) {
        // If no metatable, create one
        lua_newtable(L);
        lua_setmetatable(L, 1);
        lua_getmetatable(L, 1); // Push it again
    }

    int metaIndex = lua_gettop(L);

    // Confirm the metatable is a table
    if (!lua_istable(L, metaIndex)) {
        luaL_error(L, "Failed to get a valid metatable.");
        return 0;
    }

    // Save original function (optional: store in registry or upvalue for restoration)
    lua_pushstring(L, methodName);   // Key
    lua_rawget(L, metaIndex);        // meta[methodName]
    int originalFuncType = lua_type(L, -1);

    if (originalFuncType != LUA_TFUNCTION && originalFuncType != LUA_TNIL) {
        lua_pop(L, 1); // Not a valid hookable function
        luaL_error(L, "Cannot hook metamethod: original is not a function.");
        return 0;
    }

    lua_pop(L, 1); // Pop original method

    // Set new hook function
    lua_pushstring(L, methodName);
    lua_pushvalue(L, 3); // The hook function
    lua_rawset(L, metaIndex); // metatable[methodName] = hook

    lua_pop(L, 1); // Pop metatable

    lua_pushboolean(L, 1); // Return true to Lua
    return 1;
}

int debug_setstack(lua_State* L) {
    luaL_checktype(L, 1, LUA_TNUMBER);
    luaL_checktype(L, 2, LUA_TNUMBER);
    luaL_checkany(L, 3);

    int originalArgsCount = lua_gettop(L);

    lua_getglobal(L, ObfStr("getfenv"));
    lua_call(L, 0, 1);

    lua_getglobal(L, "debug");
    lua_setfield(L, -2, "debug");

    lua_pop(L, lua_gettop(L) - originalArgsCount);

    std::int32_t indice = lua_tonumber(L, 1);
    std::int32_t idx = lua_tonumber(L, 2);
    auto value = index2addr(L, 3);

    luaL_argcheck(L, indice > 0, 1, ObfStr("invalid level passed to setstack"));

    std::int32_t level = indice;
    CallInfo* ci = nullptr;
    Closure* f = nullptr;

    if (unsigned(level) < unsigned(L->ci - L->base_ci)) {
        ci = L->ci - level;
        const auto args = cast_int(ci->top - ci->base);

        if (args > (idx - 1)) {
            auto reg = ci->base + (idx - 1);
            luaA_pushobject(L, value);

            const auto val = index2addr(L, -1);
            if (val->tt != reg->tt)
                luaL_argerror(L, 3, ObfStr("type does not match register at specified index"));

            setobj(L, reg, val);
        }
        else
            luaL_error(L, ObfStr("invalid index passed to setstack"));
    }
    else
        luaL_error(L, ObfStr("invalid level passed to setstack"));

    return 0;
};


void api::Environment::Cl::Register(lua_State* L) {
    RegisterFunction(L, debug_setstack, oxorany("debug_setstack"));
    RegisterFunction(L, IsCClosure, oxorany("iscclosure"));
    RegisterFunction(L, IsLClosure, oxorany("islclosure"));
    RegisterFunction(L, CloneFunction, oxorany("clonefunction"));
    RegisterFunction(L, Loadstring, oxorany("loadstring"));
    RegisterFunction(L, NewCClosure, oxorany("newcclosure"));
    RegisterFunction(L, CheckCaller, oxorany("checkcaller"));
    RegisterFunction(L, HookFunction, oxorany("hookfunction"));
    RegisterFunction(L, HookFunction, oxorany("replaceclosure"));
    RegisterFunction(L, HookMetaMethod, oxorany("hookmetamethod"));
}