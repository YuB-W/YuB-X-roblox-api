#pragma once
#include <lua.h>
#include "Scheduler/Scheduler.hpp"

namespace api
{
	namespace Environment
	{
		namespace Crypt
		{
			void Register(lua_State* L);
		}
	}
}