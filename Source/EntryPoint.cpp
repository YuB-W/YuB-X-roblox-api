#define _CRT_SECURE_NO_WARNINGS

#include <Environment/Environment.hpp>
#include "Scheduler/Scheduler.hpp"
#include <Update/Engine.hpp>

#include <ThreadPool.hpp>
#include <inttypes.h>  
#include "NamedPipe.h"
#include <RBX.hpp>

#include <Windows.h>
#include <fstream>
#include <sstream>
#include <string>

#include <windows.h>
#include <userenv.h>


std::atomic<uintptr_t> lastState{ 0 };
std::atomic<uintptr_t> lastPlaceId{ 0 };
std::atomic<bool> teleportMonitoringActive{ true };


uintptr_t GlobalState() {
    auto ScriptContext = RBX::Scheduler->GetScriptContext();
    uintptr_t GlobalState = ScriptContext + Update::ScriptContext::GlobalState;
    return GlobalState;
}


std::string namecallhookscript = R"(

if not game:IsLoaded() then game.Loaded:Wait() end

local BlacklistedFunctions = {
    "OpenVideosFolder",
    "OpenScreenshotsFolder",
    "GetRobuxBalance",
    "PerformPurchase",
    "PromptBundlePurchase",
    "PromptNativePurchase",
    "PromptProductPurchase",
    "PromptPurchase",
    "PromptGamePassPurchase",
    "PromptRobloxPurchase",
    "PromptThirdPartyPurchase",
    "Publish",
    "GetMessageId",
    "OpenBrowserWindow",
    "OpenNativeOverlay",
    "RequestInternal",
    "ExecuteJavaScript",
    "EmitHybridEvent",
    "AddCoreScriptLocal",
    "HttpRequestAsync",
    "ReportAbuse",
    "SaveScriptProfilingData",
    "OpenUrl",
    "DeleteCapture",
    "DeleteCapturesAsync"
}

local Metatable = getrawmetatable(game)
local OldMetatable = Metatable.__namecall

setreadonly(Metatable, false)
Metatable.__namecall = function(Self, ...)
    local Method = getnamecallmethod()
   
    if table.find(BlacklistedFunctions, Method) then
        
        return nil
    end

    if Method == "HttpGet" or Method == "HttpGetAsync" then
            return httpget(...)
    elseif Method == "GetObjects" then 
            return GetObjects(...)
    end

    return OldMetatable(Self, ...)
end

local OldIndex = Metatable.__index

setreadonly(Metatable, false)
Metatable.__index = function(Self, i)
    if table.find(BlacklistedFunctions, i) then
        return nil
    end

    if Self == game then
        if i == "HttpGet" or i == "HttpGetAsync" then 
            return httpget
        elseif i == "GetObjects" then 
            return GetObjects
        end
    end
    return OldIndex(Self, i)
end


function HookMetaMethod(object, metamethodName, hookFunction)
    local metatable = getmetatable(object)
    if not metatable then
        return error("The object does not have a metatable.")
    end

    local originalMethod = metatable[metamethodName]

    metatable[metamethodName] = function(Self, ...)
        return hookFunction(Self, originalMethod, ...)
    end
end

print("Init SetUp successfully!")
)";



void monitor_teleport() {
    try {
        uintptr_t DataModel = RBX::Scheduler->GetDataModel();
        uintptr_t currentPlaceId = *(uintptr_t*)(DataModel + Update::DataModel::PlaceId);

        lastPlaceId.store(currentPlaceId);
        lastState.store(GlobalState());

        bool teleportedAway = false;

        while (teleportMonitoringActive.load()) {
            DataModel = RBX::Scheduler->GetDataModel();
            currentPlaceId = *(uintptr_t*)(DataModel + Update::DataModel::PlaceId);
            
            if (currentPlaceId == 0 || GlobalState == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }

            if (currentPlaceId != lastPlaceId.load() || GlobalState() != lastState.load()) {
                teleportedAway = true;
                RBX::Print(0, "Teleport detected. placeId: %" PRIuPTR, currentPlaceId);
                lastPlaceId.store(currentPlaceId);
                lastState.store(GlobalState());
            }
            else if (teleportedAway && currentPlaceId == lastPlaceId.load() && GlobalState() == lastState.load()) {
                teleportedAway = false;
                RBX::Print(0, "Teleport detected. placeId: %" PRIuPTR, currentPlaceId);

                std::this_thread::sleep_for(std::chrono::milliseconds(3000));

                auto ScriptContext = RBX::Scheduler->GetScriptContext();
                auto GlobalState = ScriptContext + Update::ScriptContext::GlobalState;

                uintptr_t StateIndex[] = { 0 };
                uintptr_t ActorIndex[] = { 0, 0 };

                lua_State* L = RBX::DecryptState(
                    RBX::GetGlobalStateForInstance(GlobalState, StateIndex, ActorIndex) +
                    Update::ScriptContext::DecryptState
                );

                lua_State* ExploitThread = Execution->NewThread(L);
                luaL_sandboxthread(ExploitThread);
                Manager->SetLuaState(ExploitThread);

                RBX::Scheduler->HookJob("Heartbeat");
                Environment->Initialize(Manager->GetLuaState());

                Execution->Send(Manager->GetLuaState(), namecallhookscript);

                RBX::Print(0, "YuB-X: Reinitialized after teleport!");
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        }
    }
    catch (const std::exception& ex) {
        RBX::Print(1, "Error in teleport monitor: %s\n", ex.what());
    }
}

void InitializeExploitation() {
    uintptr_t DataModel = RBX::Scheduler->GetDataModel();
    uintptr_t placeId = *(uintptr_t*)(DataModel + Update::DataModel::PlaceId);

    if (placeId) {

        RBX::Scheduler->Initialize();

        uintptr_t StateIndex[] = { 0 };
        uintptr_t ActorIndex[] = { 0, 0 };

        lua_State* L = RBX::DecryptState(
            RBX::GetGlobalStateForInstance(GlobalState(), StateIndex, ActorIndex) +
            Update::ScriptContext::DecryptState
        );

        lua_State* ExploitThread = Execution->NewThread(L);
        luaL_sandboxthread(ExploitThread);
        Manager->SetLuaState(ExploitThread);

        RBX::Scheduler->HookJob("Heartbeat");
        Environment->Initialize(Manager->GetLuaState());
        Execution->Send(Manager->GetLuaState(), namecallhookscript);

        lastPlaceId.store(placeId);
        lastState.store(GlobalState());
    }

    StartServer();
    std::thread(monitor_teleport).detach();

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}


void check_update()
{
    RBX::Print(0, "[!] Print Address Works");
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
}

BOOL APIENTRY DllMain(HMODULE Module, DWORD Reason, LPVOID Reserved) {
    switch (Reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(Module);
        ThreadPool->Run(check_update);
        break;
    case DLL_PROCESS_DETACH:
        teleportMonitoringActive.store(false); 
        break;
    }
    return TRUE;
}
