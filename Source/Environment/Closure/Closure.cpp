#include "Closure.hpp"
#include "RBX.hpp"
#include "../../Dependencies/Luau/VM/src/lgc.h"
#include "../../Dependencies/Luau/VM/src/lapi.h"
#include <lualib.h>
#include "../Dependencies/Base64/Base64.h"
#include <Luau/Compiler.h>
#include "../../../Dependencies/Luau/VM/src/lfunc.h"
#include <lua.h>
#include <Environment/Library.hpp>
#include <unordered_set>
#include <Environment/Environment.hpp>

std::map<Closure*, int> ClMap{};
std::unordered_map<Closure*, __int64> CClosures;
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


int ExecuteCaptured(lua_State* L)
{
    const char* code = lua_tostring(L, lua_upvalueindex(1));
    if (!code)
    {
        luaL_error(L, ObfStr("No code found in upvalue."));
        return 0;
    }

    if (Execution && Manager)
    {
        Execution->Send(Manager->GetLuaState(), code);
    }
    else
    {
        luaL_error(L, ObfStr("Invalid Execution or Manager state."));
    }
    return 0;
}

inline std::string random_string(std::size_t length) {
    auto randchar = []() -> char {
        const char charset[] =
            "0123456789"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz";
        const std::size_t max_index = (sizeof(charset) - 1);
        return charset[rand() % max_index];
        };

    std::string str(length, 0);
    std::generate_n(str.begin(), length, randchar);
    return str;
};

bool IsLuauBytecode(const char* data, size_t len)
{
    if (len >= 3 &&
        static_cast<unsigned char>(data[0]) == 0x1B &&
        data[1] == 'L' &&
        data[2] == 'u')
        return true;

    if (len >= 1 && static_cast<unsigned char>(data[0]) == 0x1B)
        return true;

    return false;
}

int loadstring(lua_State* L) {
    luaL_checktype(L, 1, LUA_TSTRING);

    size_t len = 0;
    const char* src = luaL_checklstring(L, 1, &len);
    const char* chunkname = (lua_gettop(L) >= 2 && lua_type(L, 2) == LUA_TSTRING)
        ? lua_tostring(L, 2)
        : "@loadstring";

    // Block Luau bytecode
    if (IsLuauBytecode(src, len)) {

        lua_pushnil(L);
        lua_pushstring(L, "Luau bytecode is not loadable");
        return 2;
    }

    std::string source(src, len);
    std::string bytecode;

    try {
        bytecode = Execution->CompileScript(source);
    }
    catch (const std::exception& e) {
        lua_pushnil(L);
        lua_pushstring(L, e.what());
        return 2;
    }
    catch (...) {
        lua_pushnil(L);
        lua_pushstring(L, "Unknown compilation error");
        return 2;
    }

    int result = RBX::LuaVM__Load(L, &bytecode, chunkname, 0);
    if (result == LUA_OK) {
        Closure* cl = (Closure*)lua_topointer(L, -1);
        if (cl && cl->l.p) {
            static uintptr_t MaxCaps = 0xFFFFFFFFFFFFFFFF;
            RBX::SetProto(cl->l.p, &MaxCaps);
        }
        return 1;
    }

    const char* err = lua_tostring(L, -1);
    lua_pop(L, 1);

    lua_pushnil(L);
    lua_pushstring(L, err ? err : "Unknown error during load");
    return 2;
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
        if (Error == ObfStr("attempt to yield across metamethod/C-call boundary")) {
            return lua_yield(L, 0);
        }
        luaL_error(L, Error);
    }
    return lua_gettop(L);
}



#define lua_preparepush(L,n)                lua_rawcheckstack((L),(n))
#define lua_preparepushcollectable(L,n)  \
        { lua_preparepush(L,n); luaC_threadbarrier(L); }


static std::vector<Closure*>               g_wrappers;         
static std::map<void*, lua_CFunction>      g_cfuncMap;         
static std::map<void*, void*>              g_wrapper2Real;     


static int trampoline(lua_State* L)
{
    void* key = (void*)clvalue(index2addr(L, lua_upvalueindex(1)));
    auto it = g_cfuncMap.find(key);
    if (it != g_cfuncMap.end())
        return it->second(L);
    return 0;
}


static void push_newcc(lua_State* L,
    lua_CFunction cfn,
    int nups /*=0*/)
{
    
    lua_preparepushcollectable(L, 1);

    
    lua_pushlightuserdata(L, nullptr);           
    lua_pushcclosurek(L, trampoline, nullptr,   
        1 + nups, nullptr);


    Closure* wrapper = clvalue(index2addr(L, -1));
    g_cfuncMap[(void*)wrapper] = cfn;
}


static int newcclosure_handler(lua_State* L)
{
    void* key = (void*)clvalue(L->ci->func);
    auto  it = g_wrapper2Real.find(key);
    if (it == g_wrapper2Real.end() || it->second == nullptr)
        return 0;                                   

    TValue* top = L->top;
    top->value.p = it->second;                      
    top->tt = LUA_TFUNCTION;
    L->top = top + 1;

    lua_insert(L, 1);                               

    int nargs = lua_gettop(L);
    int res = lua_pcall(L, nargs, LUA_MULTRET, 0);
    if (res == LUA_OK && (L->status == LUA_YIELD || L->status == LUA_BREAK))
        return -1;

    return lua_gettop(L);                           
}


static int newcclosure_cont(lua_State* L, int status)
{
    if (status == LUA_OK) return lua_gettop(L);
    luaL_error(L, lua_tostring(L, -1));
    return 0;
}


int newcclosure(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TFUNCTION);
    if (lua_iscfunction(L, 1))
        (void)luaL_error(L, "L closure expected");
    return 0;

    TValue* tv = (TValue*)index2addr(L, 1);
    void* realClosurePtr = (void*)&tv->value.gc->cl;

    /* store the real Lua closure somewhere stable */
    lua_ref(L, 1);                                  /* pops from stack */

    /* create our wrapper on top */
    push_newcc(L, newcclosure_handler, 0);

    /* record mapping: wrapper → realPtr */
    Closure* wrapper = clvalue(index2addr(L, -1));
    g_wrappers.push_back(wrapper);
    g_wrapper2Real[(void*)wrapper] = realClosurePtr;

    /* set wrapper ptr as trampoline upvalue (#1) so we can find ourselves */
    lua_pushvalue(L, -1);                /* wrapper closure */
    lua_setupvalue(L, -2, 1);            /* set upvalue[1] = wrapper */

    return 1;                            /* wrapper left on stack */
}


int NewCClosureStub(lua_State* L)
{
    const int nargs = lua_gettop(L);

    Closure* closure = clvalue(L->ci->func);
    auto it = CClosures.find(closure);
    if (it == CClosures.end())
        return 0;

    int lclosureRef = it->second;
    lua_getref(L, lclosureRef);
    lua_insert(L, 1);

    const int status = lua_pcall(L, nargs, LUA_MULTRET, 0);
    if (status == LUA_YIELD)
        return lua_yield(L, 0);

    if (status != LUA_OK)
    {
        const char* err = lua_tostring(L, -1);
        if (err && strcmp(err, ObfStr("attempt to yield across metamethod/C-call boundary")) == 0)
            return lua_yield(L, 0);
        return 0;
    }

    return lua_gettop(L);
}

namespace Metatable {

#define lua_preparepush(L, pushCount) lua_rawcheckstack(L, pushCount)
#define lua_preparepushcollectable(L, pushCount) { lua_preparepush(L, pushCount); luaC_threadbarrier(L); }


    [[noreturn]] int luaL_errorLd(lua_State* L, const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        luaL_where(L, 1);
        lua_pushvfstring(L, fmt, args);
        va_end(args);
        lua_concat(L, 2);
        lua_error(L); // never returns
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
    int hookmetamethod(lua_State* state) {
        if (!lua_isuserdata(state, 1) && !lua_istable(state, 1)) {
            luaL_error(state, "Expected userdata or table as first argument.");
            return 0;
        }

        const char* metamethod = luaL_checkstring(state, 2);

        if (!lua_getmetatable(state, 1)) {
            luaL_error(state, "No metatable detected!");
            return 0;
        }

        lua_pushstring(state, metamethod);
        lua_rawget(state, -2);

        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            luaL_error(state, "Invalid meta method");
            return 0;
        }

        if (!lua_isfunction(state, 3)) {
            luaL_error(state, "Need function at 3rd argument!");
            return 0;
        }

        lua_getglobal(state, "hookfunction");
        if (!lua_isfunction(state, -1)) {
            luaL_error(state, "hookfunction is not available in global environment.");
            return 0;
        }

        lua_pushvalue(state, -2);
        lua_pushvalue(state, 3);

        if (lua_pcall(state, 2, 1, 0) != LUA_OK) {
            luaL_error(state, "%s", lua_tostring(state, -1));
            return 0;
        }

        return 1;
    }



}



void api::Environment::Cl::Register(lua_State* L) {
    RegisterFunction(L, loadstring, oxorany("loadstring")); // rewrite needed
    RegisterFunction(L, loadstring, oxorany("load")); // rewrite needed
    RegisterFunction(L, Metatable::HookFunction, oxorany("hookfunction"));
    RegisterFunction(L, Metatable::HookFunction, oxorany("replaceclosure"));
    RegisterFunction(L, Metatable::HookFunction, oxorany("hookfunc"));
    // in test
    RegisterFunction(L, IsCClosure, oxorany("iscclosure"));
    RegisterFunction(L, IsLClosure, oxorany("islclosure"));
    RegisterFunction(L, CloneFunction, oxorany("clonefunction"));
    RegisterFunction(L, newcclosure, oxorany("newcclosure"));
    RegisterFunction(L, CheckCaller, oxorany("checkcaller"));
}
