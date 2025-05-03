/* Updated for version-a9a5d0b275a34ffb */

#pragma once

#include <Windows.h>
#include <iostream>
#include <vector>
#include "lobject.h" // or whatever file defines `struct Proto`

#define REBASE(x) x + (uintptr_t)GetModuleHandle(nullptr)

struct lua_State;
struct Proto;


namespace Update {
    const uintptr_t Print = REBASE(0x164A850); // Current identity is %d updated

    const uintptr_t RawScheduler = REBASE(0x63BE258); // ClusterPacketCacheTaskQueue
    const uintptr_t GetGlobalStateForInstance = REBASE(0xF08A50); // Script Start
    const uintptr_t DecryptState = REBASE(0xCB0870); // Script Start
    const uintptr_t LuaVM__Load = REBASE(0xCB3460); // oldResult, moduleRef = ...
    const uintptr_t Task__Defer = REBASE(0x10E5B80); // Maximum re-entrancy depth (%i) 


    /* CUSTOM FUNCTIONS */
    const uintptr_t KTable = REBASE(0x5EBC120); // Xref "Trying to call method on object of type: `%s` with incorrect arguments." first xref and below the string in that xref is the ktable (qword_??????)
    const uintptr_t Touch_Offset = REBASE(0x11D63A0);
    const uintptr_t GetProperty_Offset = REBASE(0xA13A40);

    namespace Bytecode // updated
    {
        static __int64 localscript = 0x1B0;
        static __int64 modulescript = 0x158;
    }

    namespace Instance { // updated
        inline constexpr std::uint64_t Name = 0x78;
        inline constexpr std::uint64_t Parent = 0x50;
        inline constexpr std::uint64_t PropDiscriptor = 0x3B0;
        inline constexpr std::uint64_t ClassName = 0x8;
        inline constexpr std::uint64_t ClassDiscriptor = 0x18;
        inline constexpr std::uint64_t Primitive = 0x170;
    }

    namespace InternalFastFlags {
        const uintptr_t EnableLoadModule = REBASE(0x5AC18D8);
        const uintptr_t DisableCorescriptLoadstring = REBASE(0x5AC18B8);
        const uintptr_t DebugCheckRenderTh = 0x5AFCC30; // (BYTE) Xref "DebugCheckRenderThreading" updated
        const uintptr_t NewScriptContextStrictFacetAccess = REBASE(0x5AC0968);

    }

    //namespace InternalFastFlags { // updated
    //    uintptr_t EnableLoadModule = 0x5AC18D8; // (BYTE) Xref "EnableLoadModule" // updated
    //    uintptr_t DisableCorescriptLoadstring = 0x5AC18B8; // (BYTE) Xref "DisableCorescriptLoadstring"
    //    uintptr_t RenderDebugCheckThreading2 = 0x53D4518; // (BYTE) Xref "RenderDebugCheckThreading2" // not found
    //    uintptr_t DebugCheckRenderTh = 0x5AFCC30; // (BYTE) Xref "DebugCheckRenderThreading" updated
    //    uintptr_t NewScriptContextStrictFacetAccess = 0x5AC0968; // (BYTE) Xref "NewScriptContextStrictFacetAccessChecks"
    //}

    namespace LuaUserData {
        inline constexpr std::uint64_t ScriptContext = 0x3C0;
        inline constexpr std::uint64_t ScriptInstance = 0x50;
        inline constexpr std::uint64_t StartScriptReentrancy = 0x58;
    }

    namespace ScriptContext {
        const uintptr_t GlobalState = 0x140;
        const uintptr_t DecryptState = 0x88;
    }

    namespace ExtraSpace {
        const uintptr_t Identity = 0x30;
        const uintptr_t Capabilities = 0x48;
    }

    namespace TaskScheduler {
        const uintptr_t JobsStart = 0x1D0;
        const uintptr_t jobEnd = 0x1D8;
        const uintptr_t JobName = 0x18;
    }

    namespace DataModel {
        const uintptr_t FakeDataModelPointer = REBASE(0x63047A8);
        const uintptr_t FakeDataModelToDataModel = 0x1B8;
        const uintptr_t PlaceId = 0x1A0;
        const uintptr_t ScriptContext = 0x3C0;

        namespace Information {
            const uintptr_t PushInstance = REBASE(0xFD8110); // sub_FC65C0 0xFD8110
            const uintptr_t Children = 0x80;

            /*     void __fastcall sub_FC65C0(__int64 a1, __int64 a2)
                 {
                     if (sub_EE28F0())
                     {
                         sub_FC6610(a1, a2);
                     }
                     else
                     {
                         *(*(a1 + 24) + 12i64) = 0;
                         *(a1 + 24) += 16i64;
                     }
                 }*/
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



// YUBX::Core Dumper Finished!
