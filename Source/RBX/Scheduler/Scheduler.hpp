#pragma once

#include <iostream>
#include <vector>

#include <Execution/Execution.hpp>
#include <Update/Engine.hpp>
#include <Manager.hpp>

namespace RBX {
    class CScheduler {
    private:
        uintptr_t Address = 0;
        std::vector<uintptr_t> Jobs;
        uintptr_t BlacklistedJob = 0; 

    public:
        void UpdateJobs();
        uintptr_t GetJobByName(const std::string& Name);
        std::vector<uintptr_t> GetAllJobsByName(const std::string& Name); 
        uintptr_t GetScriptContext();
        uintptr_t GetDataModel();

        static uintptr_t Cycle(uintptr_t A1, uintptr_t A2, uintptr_t A3);

        void HookJob(const std::string& Name);
        void ScheduleScript(const std::string& Script);
        void Initialize();

        void BlacklistBadJobs();
        uintptr_t GetBlacklistedJob() const { return BlacklistedJob; } 
    };

    inline auto Scheduler = std::make_unique<CScheduler>();
}
