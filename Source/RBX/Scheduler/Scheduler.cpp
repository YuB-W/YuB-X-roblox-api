#include "Scheduler.hpp"

using namespace RBX;

using JobOriginalVF = uintptr_t(__fastcall*)(uintptr_t, uintptr_t, uintptr_t);
static JobOriginalVF OriginalVF = {};
static std::vector<std::string> ScriptQueue;

static TValue* index2addr(lua_State* L, int idx) {
    return (idx > 0) ? &L->base[idx - 1] : &L->top[idx];
}

void CScheduler::UpdateJobs() {
    Jobs.clear();
    uintptr_t JobsStart = *(uintptr_t*)(Address + Update::TaskScheduler::JobsStart);
    uintptr_t JobsEnd = *(uintptr_t*)(Address + Update::TaskScheduler::JobsStart + sizeof(void*));

    for (auto i = JobsStart; i < JobsEnd; i += 0x10) {
        uintptr_t Job = *(uintptr_t*)i;
        if (!Job) continue;

        std::string* JobName = reinterpret_cast<std::string*>(Job + Update::TaskScheduler::JobName);
        if (JobName && JobName->length() > 0)
            Jobs.push_back(Job);
    }
}

uintptr_t CScheduler::GetJobByName(const std::string& Name) {
    for (auto Job : Jobs) {
        if (*(std::string*)(Job + Update::TaskScheduler::JobName) == Name)
            return Job;
    }
    return 0;
}

std::vector<uintptr_t> CScheduler::GetAllJobsByName(const std::string& Name) {
    std::vector<uintptr_t> matches;
    for (auto Job : Jobs) {
        std::string* JobName = reinterpret_cast<std::string*>(Job + Update::TaskScheduler::JobName);
        if (JobName && *JobName == Name)
            matches.push_back(Job);
    }
    return matches;
}

uintptr_t CScheduler::GetScriptContext() {
    uintptr_t dataModel = GetDataModel();
    if (!dataModel) {
        RBX::Print(0, "❌ DataModel is null");
        return 0;
    }

    uintptr_t children = *(uintptr_t*)(dataModel + 0x80); 
    uintptr_t start = *(uintptr_t*)(children + 0x0);
    uintptr_t end = *(uintptr_t*)(children + 0x8);

    for (uintptr_t ptr = start; ptr < end; ptr += sizeof(uintptr_t)) {
        uintptr_t instance = *(uintptr_t*)ptr;
        if (!instance) continue;

        uintptr_t classDesc = *(uintptr_t*)(instance + 0x18);
        if (!classDesc) continue;

        const char* name = *(const char**)(classDesc + 0x8);
        if (!name) continue;

        if (strcmp(name, "ScriptContext") == 0) {
            return instance;
        }
    }

    RBX::Print(0, "❌ ScriptContext not found in Children iteration");
    return 0;
}


uintptr_t CScheduler::GetDataModel() {
    uintptr_t fakeDM = *(uintptr_t*)Update::DataModel::FakeDataModel;
    return *(uintptr_t*)(fakeDM + Update::DataModel::FakeDataModelToDataModel);
}

uintptr_t CScheduler::Cycle(uintptr_t A1, uintptr_t A2, uintptr_t A3) {
    lua_State* L = Manager->GetLuaState();
    if (!L) return OriginalVF(A1, A2, A3);

    if (!ScriptQueue.empty()) {
        std::string Script = ScriptQueue.front();
        ScriptQueue.erase(ScriptQueue.begin());
        if (!Script.empty()) Execution->Send(L, Script);
    }

    return OriginalVF(A1, A2, A3);
}

void CScheduler::HookJob(const std::string& Name) {
    uintptr_t Job = GetJobByName(Name);
    if (!Job) return;

    void** VTable = new void* [25]();
    memcpy(VTable, *(void**)Job, sizeof(uintptr_t) * 25);

    OriginalVF = (JobOriginalVF)VTable[2];
    VTable[2] = Cycle;
    *(void**)Job = VTable;
}

void CScheduler::ScheduleScript(const std::string& Script) {
    ScriptQueue.push_back(Script);
}

void CScheduler::BlacklistBadJobs() {
    auto jobs = GetAllJobsByName("WaitingHybridScriptsJob");
    if (jobs.empty()) {
        return;
    }

    for (auto Job : jobs) {
        uintptr_t global = GetScriptContext() + Update::GlobalState;

        uintptr_t StateIndex[1] = { 0 };
        uintptr_t ActorIndex[2] = { 0, 0 };

        uintptr_t state = RBX::GetGlobalState(global, StateIndex, ActorIndex);
        uintptr_t decryptPtr = state + Update::EncryptedState;

        lua_State* L = RBX::DecryptLuaState(decryptPtr);

        if (!L || L->tt != LUA_TTHREAD) continue;
        lua_State* ExploitThread = Execution->NewThread(L);

        lua_pushvalue(L, LUA_REGISTRYINDEX);
        Table* registry = hvalue(index2addr(L, -1));
        int regSize = registry ? registry->sizearray : -1;

        if (registry && regSize == 4) {
            BlacklistedJob = Job;
            return;
        }
    }
}

void CScheduler::Initialize() {
    Address = *(uintptr_t*)Update::RawScheduler;
    UpdateJobs();
    BlacklistBadJobs();
}
