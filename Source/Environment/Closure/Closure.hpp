#pragma once
#include <lua.h>
#include "Scheduler/Scheduler.hpp"



namespace api 
{
	namespace Environment
	{
		namespace Cl
		{
			void Register(lua_State* L);
		}
	}

}