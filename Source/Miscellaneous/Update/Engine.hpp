/* Updated for version-ff05edc617954c5b */

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
    int thread_idx; // 0x10
    int func_idx; //0x14
};

struct SignalDataT {
    uint64_t padding1;
    SignalT* root; //0x8
    uint64_t padding2[12];
    SignalConnectionT* connection_data; //0x70
};

struct SignalT {
    uint64_t padding1[2];
    SignalT* next; //0x10
    uint64_t padding2;
    uint64_t state;
    uint64_t padding3;
    SignalDataT* signal_data; //0x30
};


namespace Update {
    //
    const uintptr_t Print =                     REBASE(0x16C1A30); // Current identity is %d updated

    // functions
    const uintptr_t PushInstance =              REBASE(0x1001240);
    const uintptr_t PushInstance2 =             REBASE(0x1001290); //0xFD8160
    const uintptr_t FireClickDetector =         REBASE(0x1DC0D30);

    //core 
    const uintptr_t RawScheduler =              REBASE(0x69CD068); // ClusterPacketCacheTaskQueue
    const uintptr_t GetGlobalStateForInstance = REBASE(0xF2A5F0);  // Script Start 0xF08A50
    const uintptr_t DecryptState =              REBASE(0xCBDE70); // Script Start
    const uintptr_t LuaVM__Load =               REBASE(0xCC0A60); // oldResult, moduleRef = ...
    const uintptr_t Task__Defer =               REBASE(0x115C9C0); // Maximum re-entrancy depth (%i) 


    // updated

    namespace Bytecode
    {
        const uintptr_t RequestCode = REBASE(0xA82310);
        const uintptr_t Bytecode = 0x10;
        const uintptr_t BytecodeSize = 0x20;

        const uintptr_t ModuleScriptByteCode = 0x158;
        const uintptr_t ModuleScriptBytecodePointer = 0x10;
        const uintptr_t ModuleScriptHash = 0x180;

        const uintptr_t LocalScriptByteCode = 0x1B0;
        const uintptr_t LocalScriptBytecodePointer = 0x10;
        const uintptr_t LocalScriptHash = 0x1C0; // not used
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
    const uintptr_t DebugCheckRenderThreading                 = REBASE(0x6122318);  // ✅
    const uintptr_t RenderDebugCheckThreading2                = REBASE(0x61521C8);  // ✅
    const uintptr_t EnableLoadModule                          = REBASE(0x60EBDA0);  // ✅
    const uintptr_t DisableCorescriptLoadstring               = REBASE(0x60EBD80);  // ✅
    const uintptr_t CrashOnDataModelValidationFailure         = REBASE(0x610A940);  // ✅
    const uintptr_t RuntimeCrashOnHang                        = REBASE(0x613EBA0);  // ✅
    const uintptr_t LockViolationScriptCrash                  = REBASE(0x60EB9F0);  // ✅
    const uintptr_t CapsMissUnassignedTelemetry               = REBASE(0x6139A20);  // ✅
    const uintptr_t LockViolationInstanceCrash                = REBASE(0x60FBEF8);  // ✅
    const uintptr_t NewScriptContextStrictFacetAccessChecks   = REBASE(0x60EB000);  // ✅

    const uintptr_t LuaStepIntervalMsOverrideEnabled = REBASE(0x60EF7D0);  // ✅

}

    namespace LuaUserData {
        inline constexpr std::uint64_t ScriptContext = 0x3B0;
        inline constexpr std::uint64_t ScriptInstance = 0x50;
        inline constexpr std::uint64_t StartScriptReentrancy = 0x58;
    }

    namespace ScriptContext {
        const uintptr_t GlobalState = 0x140;
        const uintptr_t DecryptState = 0x88;
    }

    namespace ExtraSpace {
        const uintptr_t Identity = 0x30;
        const uintptr_t Capabilities = 0x48; // 48 // 78
    }

    namespace TaskScheduler {
        const uintptr_t JobsStart = 0x1D0;
        const uintptr_t jobEnd = 0x1D8;
        const uintptr_t JobName = 0x18;
    }

    namespace DataModel {
        const uintptr_t FakeDataModelPointer = REBASE(0x690D168);
        const uintptr_t FakeDataModelToDataModel = 0x1B8;
        const uintptr_t PlaceId = 0x1A0;
        const uintptr_t ScriptContext = 0x3B0;

        namespace Information {
            const uintptr_t PushInstance = REBASE(0x1001240); // sub_FC65C0 0xFD8110
            const uintptr_t Children = 0x80;
            const uintptr_t ChildrenEnd = 0x8;
            const uintptr_t Parent = 0x50;
            const uintptr_t Name = 0x78;
            const uintptr_t ClassDescriptor = 0x18;
            const uintptr_t ClassName = 0x8;

            // ✅ New: Fast path from DataModel → Wrapper → ScriptContext
            const uintptr_t WrapperFromDataModel = 0x18;   // *(DataModel + 0x18)
            const uintptr_t InstanceFromWrapper = 0x08;   // *(Wrapper + 0x08)
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

    using RequestCodeT = std::uintptr_t(__fastcall*)(std::uintptr_t protected_string_ref, std::uintptr_t script);
    inline auto RequestCode = (RequestCodeT)Update::Bytecode::RequestCode;

    using PushInstanceT1 = uintptr_t(__fastcall*)(lua_State*, void* instance);
    inline auto Push__Instance = (PushInstanceT1)Update::PushInstance;

    using PushInstanceT = uintptr_t(__fastcall*)(lua_State*, void* instance);
    inline auto Push__Instance2 = (PushInstanceT)Update::PushInstance2;

    using ClickT = void(__fastcall*)(__int64 a1, float a2, __int64 a3);
    inline auto Click = (ClickT)Update::FireClickDetector;

    //void __fastcall sub_1D04280(__int64 a1, float a2, __int64 a3)
    //{


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
