/* Updated for version-e00a4ca39fb04359 */

#pragma once

#include <Windows.h>
#include <iostream>
#include <vector>
#include "lobject.h"

#define REBASE(x) x + (uintptr_t)GetModuleHandle(nullptr)
#define RBX_NORMAL 0LL
#define RBX_INFO 1LL
#define RBX_WARN 2LL
#define RBX_ERROR 3LL

struct SignalT;
struct lua_State;
struct Proto;

struct SignalConnectionT {
    char padding[16];
    int thread_idx;
    int func_idx; 
};

struct SignalDataT {
    uint64_t padding1;
    SignalT* root; 
    uint64_t padding2[12];
    SignalConnectionT* connection_data; 
};

struct SignalT {
    uint64_t padding1[2];
    SignalT* next; 
    uint64_t padding2;
    uint64_t state;
    uint64_t padding3;
    SignalDataT* signal_data; //0x30
};


namespace Update {

    // dumped
    const uintptr_t LuaH_DummyNode = REBASE(0x468D6C8);
    const uintptr_t LuaO_NilObject = REBASE(0x468DCA8);
    const uintptr_t Luau_Execute = REBASE(0x275D280);

    const uintptr_t Print = REBASE(0x15469F0);
    const uintptr_t GetProperty = REBASE(0xA3DDB0);
    const uintptr_t GetGlobalState = REBASE(0xDA0660);
    const uintptr_t DecryptLuaState = REBASE(0xB25660);
    const uintptr_t PushInstance = REBASE(0xE7CBC0);
    const uintptr_t LuaVM__Load = REBASE(0xB28790);
    const uintptr_t Task__Defer = REBASE(0xFC9CA0); //nigga

    const uintptr_t FireMouseClick = REBASE(0x1C4E4E0);
    const uintptr_t FireRightMouseClick = REBASE(0x1C4E680);
    const uintptr_t FireMouseHoverEnter = REBASE(0x1C4FA80);
    const uintptr_t FireMouseHoverLeave = REBASE(0x1C4FC20);
    const uintptr_t FireTouchInterest = REBASE(0x141B380);
    const uintptr_t FireProximityPrompt = REBASE(0x1D236A0);

    const uintptr_t RequestCode = REBASE(0x8EBD60);
    const uintptr_t GetIdentityStruct = REBASE(0x37F57D0);
    const uintptr_t IdentityPtr = REBASE(0x6304418);
    const uintptr_t LuaD_throw = REBASE(0x27226D0);
    const uintptr_t RawScheduler = REBASE(0x67AB9E8);
    const uintptr_t KTable = REBASE(0x62D04B0);
    const uintptr_t BitMap = (uintptr_t)GetModuleHandle("RobloxPlayerBeta.dll") + 0x2855A8;

    const uintptr_t GlobalState = 0x140;
    const uintptr_t EncryptedState = 0x88;

    namespace DataModel {
        const uintptr_t FakeDataModel = REBASE(0x66EA5E8);
        const uintptr_t FakeDataModelToDataModel = 0x1B8;
        const uintptr_t PlaceId = 0x1A0;
        const uintptr_t ScriptContext = 0x3B0;
    }

    namespace Instance {
        const uintptr_t ClassDescriptor = 0x18;
        const uintptr_t PropertyDescriptor = 0x3B8;
        const uintptr_t ClassName = 0x8;
        const uintptr_t Name = 0x78;
        const uintptr_t Children = 0x80;
    }

    namespace Scripts {
        const uintptr_t LocalScriptEmbedded = 0x1B0;
        const uintptr_t ModuleScriptEmbedded = 0x158;

        const uintptr_t weak_thread_node = 0x188;
        const uintptr_t weak_thread_ref = 0x8;
        const uintptr_t weak_thread_ref_live = 0x20;
        const uintptr_t weak_thread_ref_live_thread = 0x8;
    }

    namespace ExtraSpace {
        const uintptr_t Identity = 0x30;
        const uintptr_t Capabilities = 0x48;
    }

    namespace TaskScheduler {
        const uintptr_t FpsCap = 0x1B0;
        const uintptr_t JobsStart = 0x1D0;
        const uintptr_t jobEnd = 0x1D8;
        const uintptr_t JobName = 0x18;
    }
}

namespace RBX {

    using TPrint = void(__fastcall*)(int, const char*, ...);
    inline auto Print = (TPrint)Update::Print;

    using TLuaVM__Load = int(__fastcall*)(lua_State*, void*, const char*, int);
    inline auto LuaVM__Load = (TLuaVM__Load)Update::LuaVM__Load;

    using TTask__Defer = int(__fastcall*)(lua_State*);
    inline auto Task__Defer = (TTask__Defer)Update::Task__Defer;

    using TGetGlobalState = uintptr_t(__fastcall*)(uintptr_t, uintptr_t*, uintptr_t*);
    inline auto GetGlobalState = (TGetGlobalState)Update::GetGlobalState;

    using TDecryptLuaState = lua_State * (__fastcall*)(uintptr_t);
    inline auto DecryptLuaState = (TDecryptLuaState)Update::DecryptLuaState;

    using TPushInstance = void(__fastcall*)(lua_State* state, void* instance);
    inline auto PushInstance = (TPushInstance)Update::PushInstance;

    using TLuaD_throw = void(__fastcall*)(lua_State*, int);
    inline auto LuaD_throw = (TLuaD_throw)Update::LuaD_throw;

    using TGetProperty = uintptr_t * (__thiscall*)(uintptr_t, uintptr_t*);
    inline auto GetProperty = (TGetProperty)Update::GetProperty;

    using TFireMouseClick = void(__fastcall*)(__int64 a1, float a2, __int64 a3);
    inline auto FireMouseClick = (TFireMouseClick)Update::FireMouseClick;

    using TFireRightMouseClick = void(__fastcall*)(__int64 a1, float a2, __int64 a3);
    inline auto FireRightMouseClick = (TFireRightMouseClick)Update::FireRightMouseClick;

    using TFireMouseHoverEnter = void(__fastcall*)(__int64 a1, __int64 a2);
    inline auto FireMouseHoverEnter = (TFireMouseHoverEnter)Update::FireMouseHoverEnter;

    using TFireMouseHoverLeave = void(__fastcall*)(__int64 a1, __int64 a2);
    inline auto FireMouseHoverLeave = (TFireMouseHoverLeave)Update::FireMouseHoverLeave;

    using TFireTouchInterest = void(__fastcall*)(uintptr_t, uintptr_t, uintptr_t, bool, bool);
    inline auto FireTouchInterest = (TFireTouchInterest)Update::FireTouchInterest;

    using TFireProxmityPrompt = std::uintptr_t(__fastcall*)(std::uintptr_t prompt);
    inline auto FireProximityPrompt = (TFireProxmityPrompt)Update::FireProximityPrompt;

    using TRequestCode = uintptr_t(__fastcall*)(uintptr_t protected_string_ref, uintptr_t script);
    inline auto RequestCode = (TRequestCode)Update::RequestCode;

    using TGetIdentityStruct = uintptr_t(__fastcall*)(uintptr_t);
    inline auto GetIdentityStruct = (TGetIdentityStruct)Update::GetIdentityStruct;

    inline std::string RequestBytecode(uintptr_t scriptPtr) {
        uintptr_t code[0x4];
        std::memset(code, 0, sizeof(code));

        RequestCode((std::uintptr_t)code, scriptPtr);

        std::uintptr_t bytecodePtr = code[1];

        if (!bytecodePtr) { return "Failed to get bytecode"; }

        std::uintptr_t str = bytecodePtr + 0x10;
        std::uintptr_t data;

        if (*reinterpret_cast<std::size_t*>(str + 0x18) > 0xf) {
            data = *reinterpret_cast<std::uintptr_t*>(str);
        }
        else {
            data = str;
        }

        std::string BOOOHOOOOOOOO;
        std::size_t len = *reinterpret_cast<std::size_t*>(str + 0x10);
        BOOOHOOOOOOOO.reserve(len);

        for (unsigned i = 0; i < len; i++) {
            BOOOHOOOOOOOO += *reinterpret_cast<char*>(data + i);
        }

        if (BOOOHOOOOOOOO.size() <= 8) { "Failed to get bytecode"; }

        return BOOOHOOOOOOOO;
    }


    inline __int64 GetThreadIdentity(uintptr_t L) {
        uintptr_t Userdata = *(uintptr_t*)(L + 0x78);
        return *(__int64*)(Userdata + Update::ExtraSpace::Identity);
    }

    inline void SetThreadIdentity(uintptr_t L, uintptr_t Identity) {
        uintptr_t Userdata = *(uintptr_t*)(L + 0x78); // 78 
        *(__int64*)(Userdata + Update::ExtraSpace::Identity) = Identity;
    }

    inline void SetThreadCapabilities(uintptr_t L, uintptr_t Capabilities) {
        uintptr_t Userdata = *reinterpret_cast<uintptr_t*>(L + 0x78); // L->userdata
        *reinterpret_cast<uint64_t*>(Userdata + Update::ExtraSpace::Capabilities) = Capabilities;
    }

    //                                                             120 > 0x78
    //if (a2)                                                      ___
    //    Userdata = *(a2 + 120); t:0000000000F3E7A3 mov rcx, [rdx+78h]
    //else
    //    Userdata = 0i64;
    //v7 = sub_335B950((Userdata + 48));

    inline void SetProto(Proto* prototype, uintptr_t* caps) {
        if (!prototype) return;

        prototype->userdata = caps;
        for (int i = 0; i < prototype->sizep; ++i) {
            SetProto(prototype->p[i], caps);
        }
    }
}

// YUBX::Core Dumper Finished!
