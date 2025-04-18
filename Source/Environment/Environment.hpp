#pragma once

#include <Windows.h>
#include <memory>
#include "lstate.h"
#include <lualib.h>
#include <lstate.h>
#include <lstring.h>
#include <lfunc.h>
#include <lmem.h>


#include "Scheduler/Scheduler.hpp"
#include "Execution/Execution.hpp"

class CEnvironment {
public:
	void Initialize(lua_State* L);
};

inline auto Environment = std::make_unique<CEnvironment>();