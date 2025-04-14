#include "Extra.hpp"

#include <Luau/Compiler.h>
#include <cpr/cpr.h>
#include "RBX.hpp"
#include "../../Dependencies/Luau/VM//src/lgc.h"
#include "../../Dependencies/Luau/VM//src/lapi.h"


#include <iostream>
#include <Windows.h>
#include <string>
#include <thread>
#include <vector>
#include <mutex>
#include <fstream>
#include <sstream> 
#include <filesystem>
#include <wininet.h>


#undef LoadString
#define oxorany(x) x 
#define RegisterFunction(L, Func, Name) lua_pushcclosure(L, Func, Name, 0); \
lua_setglobal(L, Name);

int SetClipboard(lua_State* L)
{
	std::size_t Length;
	const char* Setting = lua_tolstring(L, 1, &Length);
	if (OpenClipboard(NULL))
	{
		if (EmptyClipboard())
		{
			HGLOBAL hglbCopy = GlobalAlloc(GMEM_FIXED, Length + 1);
			if (hglbCopy == NULL)
			{
				CloseClipboard();
			}
			memcpy(hglbCopy, Setting, Length + 1);
			if (!SetClipboardData(CF_TEXT, hglbCopy))
			{
				CloseClipboard();
				GlobalFree(hglbCopy);
			}
			CloseClipboard();
		}
		else
		{

		}
	}
	else
	{
	}
	return 0;
}

int GetClipboard(lua_State* L)
{
	std::size_t Length;
	const char* Setting = lua_tolstring(L, 1, &Length);
	if (OpenClipboard(NULL))
	{
		HANDLE Got = GetClipboardData(CF_TEXT);
		if (Got == nullptr)
		{
			CloseClipboard();
		}

		char* pszText = static_cast<char*>(GlobalLock(Got));
		if (pszText == nullptr)
		{
			CloseClipboard();
		}
		std::string text(pszText);
		GlobalUnlock(Got);
		CloseClipboard();
		lua_pushstring(L, text.c_str());
		return 1;
	}
	else
	{
	}
}



int MessageBoxFunc(lua_State* L)
{
	auto Contents = lua_tostring(L, 1);
	auto Title = lua_tostring(L, 2);
	auto Flags = lua_tonumber(L, 3);

	auto Result = MessageBoxA(NULL, Contents, Title, Flags);

	lua_pushnumber(L, Result);
	return 1;
}

int OpenProcessFunc(lua_State* L)
{
	auto Process = lua_tostring(L, 1);

	// Convert Process to a wide string
	std::wstring wProcess(Process, Process + strlen(Process));

	// Use ShellExecute to open the process
	HINSTANCE hInst = ShellExecuteW(NULL, L"open", wProcess.c_str(), NULL, NULL, SW_SHOW);

	if ((int)hInst <= 32)
	{
		lua_pushboolean(L, false); // Failed to open the process
		return 1;
	}

	lua_pushboolean(L, true); // Successfully opened the process
	return 1;
}

int SetFpsCap(lua_State* L)
{
	auto FpsInput = lua_tonumber(L, 1);

	return 0;
}

int IdentifyExecutor(lua_State* L)
{
	lua_pushstring(L, oxorany("YuB-X-NG"));
	lua_pushnumber(L, oxorany(1));
	lua_pushstring(L, oxorany("YuB-X-NG"));
	//RBX::Print(2,"YUBX::API");
	return 1;
}

int getexecutorname(lua_State* L)
{
	lua_pushstring(L, oxorany("YuB-X"));
	lua_pushnumber(L, oxorany(1));
	lua_pushstring(L, oxorany("YuB-X"));
	//RBX::Print(2,"YUBX::API");
	return 1;
}


int GetIdentity(lua_State* L)
{
	lua_pushnumber(L, RBX::GetThreadIdentity(reinterpret_cast<uintptr_t>(L)));
	return 1;
}


int SetIdentity(lua_State* L)
{
	auto Identity = lua_tonumber(L, 1);
	RBX::SetThreadIdentity(reinterpret_cast<uintptr_t>(L), Identity);
	return 0;
}


void api::Environment::Extra::Register(lua_State* L)
{
	RegisterFunction(L, GetClipboard, oxorany("GetClipboard"));
	RegisterFunction(L, SetClipboard, oxorany("setclipboard"));
	RegisterFunction(L, SetClipboard, oxorany("toclipboard"));
	RegisterFunction(L, MessageBoxFunc, oxorany("messagebox"));
	RegisterFunction(L, SetFpsCap, oxorany("setfpscap"));
	RegisterFunction(L, IdentifyExecutor, oxorany("identifyexecutor"));
	RegisterFunction(L, getexecutorname, oxorany("getexecutorname"));
	/*RegisterFunction(L, OpenProcessFunc, oxorany("openprocess"));*/


	RegisterFunction(L, SetIdentity, oxorany("setidentity"));
	RegisterFunction(L, SetIdentity, oxorany("setthreadidentity"));
	RegisterFunction(L, SetIdentity, oxorany("setthreadcontext"));
	RegisterFunction(L, GetIdentity, oxorany("getidentity"));
	RegisterFunction(L, GetIdentity, oxorany("getthreadidentity"));
	RegisterFunction(L, GetIdentity, oxorany("getthreadcontext"));
}