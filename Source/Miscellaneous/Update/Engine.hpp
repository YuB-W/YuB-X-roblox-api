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


    namespace ScriptContext {
        const uintptr_t GlobalState = 312;
        const uintptr_t DecryptState = 136;
    }
    // YUBX::Core Dumper Finished!



    namespace ExtraSpace {
        const uintptr_t Identity = 0x30;
        const uintptr_t Capabilities = 0x48;
    }


    //string "WaitingHybridScriptsJob" sub_1AAA560(a1, "WaitingHybridScriptsJob", 1, 0, v7, 7u); >  sub_35AF010(a1, a2, &v14, a6);
    // sub_35AF010(a1, a2, &v14, a6);
    //*a1 = &unk_4D91A40;
    //*(a1 + 432) = a3;
    //*(a1 + 440) = 0i64;
    //*(a1 + 448) = a4;
    //*(a1 + 456) = 0i64;
    //*(a1 + 464) = 0; JobsStart
    //*(a1 + 472) = *a5;
    //*(a1 + 480) = 0;
    //*(a1 + 456) = sub_35EB150("Jobs", v8, -1, 0, 255);
    //// 
    //*(a1 + 88) = 0i64;
    //*(a1 + 96) = 0;
    //*(a1 + 104) = 0i64;
    //*(a1 + 112) = 0i64;
    //*(a1 + 120) = 0x3FA999999999999Ai64;
    //*(a1 + 128) = 0i64;
    //*(a1 + 136) = 0i64;
    //*(a1 + 144) = 0i64; // JobName
    //*(a1 + 152) = 1;
    //v11 = (a1 + 160);
    //if (dword_5A00FAC > 1)
    //    sub_36D7120(v11);
    //else
    //    sub_36D7030(v11);


    //namespace TaskScheduler { // string "Default job arbiter must always be valid"
    //    const uintptr_t JobsStart = 464;
    //    const uintptr_t JobName = 144;
    //}
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




}
