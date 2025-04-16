/* Updated for version-c2c9efad42eb44e5 */

#pragma once

#include <Windows.h>
#include <iostream>
#include <vector>

#define REBASE(x) x + (uintptr_t)GetModuleHandle(nullptr)

struct lua_State;
struct Proto;

namespace Update {

    const uintptr_t Print = REBASE(0x1631640); // Current identity is %d
    const uintptr_t RawScheduler = REBASE(0x6310638); // ClusterPacketCacheTaskQueue
    const uintptr_t GetGlobalStateForInstance = REBASE(0xEF0540);// Script Start
    const uintptr_t DecryptState = REBASE(0xC92180); // Script Start
    const uintptr_t LuaVM__Load = REBASE(0xC94DA0); // oldResult, moduleRef = ...
    const uintptr_t Task__Defer = REBASE(0x10D02D0); // Maximum re-entrancy depth (%i) 



    namespace ScriptContext { // Script Start > GetGlobalStateForInstance < DecryptState >
        const uintptr_t GlobalState = 0x138;
        const uintptr_t DecryptState = 0x88;
    }
    // YUBX::Core Dumper Finished!


    namespace ExtraSpace {
        const uintptr_t Identity = 0x30;
        const uintptr_t Capabilities = 0x48;
    }

    namespace TaskScheduler {
        const uintptr_t JobsStart = 0x1D0;
        const uintptr_t JobName = 0x18;
    }
    namespace DataModel {
        const uintptr_t FakeDataModelPointer = REBASE(0x6257EB8);
        const uintptr_t FakeDataModelToDataModel = 0x1B8;
        const uintptr_t PlaceId = 0x180;
        const uintptr_t ScriptContext = 0x3C0;

        namespace Information {
            const uintptr_t PushInstance = REBASE(0xFBF950);
            const uintptr_t Children = 0x80;
        }
    }
}

namespace RBX {

    using _Print = int(__fastcall*)(int, const char*, ...);
    inline auto Print = (_Print)Update::Print;

    using TGetGlobalStateForInstance = uintptr_t(__fastcall*)(uintptr_t, uintptr_t*, uintptr_t*);
    inline auto GetGlobalStateForInstance = (TGetGlobalStateForInstance)Update::GetGlobalStateForInstance;

    using TDecryptState = lua_State * (__fastcall*)(uintptr_t);
    inline auto DecryptState = (TDecryptState)Update::DecryptState;

    using TLuaVM__Load = int(__fastcall*)(lua_State*, void*, const char*, int);
    inline auto LuaVM__Load = (TLuaVM__Load)Update::LuaVM__Load;

    using TTask__Defer = int(__fastcall*)(lua_State*);
    inline auto Task__Defer = (TTask__Defer)Update::Task__Defer;

    inline __int64 GetThreadIdentity(uintptr_t L) {
        uintptr_t Userdata = *(uintptr_t*)(L + 0x78);
        return *(__int64*)(Userdata + Update::ExtraSpace::Identity);
    }

    inline void SetThreadIdentity(uintptr_t L, uintptr_t Identity) {
        uintptr_t Userdata = *(uintptr_t*)(L + 0x78);
        *(__int64*)(Userdata + Update::ExtraSpace::Identity) = Identity;
    }

    inline void SetThreadCapabilities(uintptr_t L, uintptr_t Capabilities) {
        uintptr_t Userdata = *(uintptr_t*)(L + 0x78);
        *(__int64*)(Userdata + Update::ExtraSpace::Capabilities) = Capabilities;
    }

    inline void SetProto(Proto* prototype, uintptr_t* caps) {
        if (!prototype) return;

        prototype->userdata = caps;
        for (int i = 0; i < prototype->sizep; ++i) {
            SetProto(prototype->p[i], caps);
        }
    }


