#include "Input.hpp"
#include <Luau/Compiler.h>
#include <cpr/cpr.h>
#include "RBX.hpp"
#include "../../Dependencies/Luau/VM//src/lgc.h"
#include "../../Dependencies/Luau/VM//src/lapi.h"
#include <lualib.h>
#include "../../../Dependencies/Luau/VM/src/lmem.h"

#define RegisterFunction(L, Func, Name) lua_pushcclosure(L, Func, Name, 0); \
lua_setglobal(L, Name);

#define RegisterMember(L, Func, Name) lua_pushcclosure(L, Func, Name, 0); \
lua_setfield(L, -2, Name);

int Mouse1Click(lua_State* L)
{
	mouse_event(MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
	return 0;
}

int Mouse1Press(lua_State* L)
{
	mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
	return 0;
}

int Mouse1Release(lua_State* L)
{
	mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
	return 0;
}

int Mouse2Click(lua_State* L)
{
	mouse_event(MOUSEEVENTF_RIGHTDOWN | MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);
	return 0;
}

int Mouse2Press(lua_State* L)
{
	mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);
	return 0;
}

int Mouse2Release(lua_State* L)
{
	mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);
	return 0;
}


int MouseMoveAbs(lua_State* L)
{
	float X = lua_tonumber(L, 1);
	float Y = lua_tonumber(L, 2);

	int width = GetSystemMetrics(SM_CXSCREEN) - 1;
	int height = GetSystemMetrics(SM_CYSCREEN) - 1;

	RECT CRect;
	GetClientRect(GetForegroundWindow(), &CRect);

	POINT Point{ CRect.left, CRect.top };
	ClientToScreen(GetForegroundWindow(), &Point);

	X = (X + (DWORD)Point.x) * (65535 / width);
	Y = (Y + (DWORD)Point.y) * (65535 / height);

	mouse_event(MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE, X, Y, 0, 0);

	return 0;
}

int MouseMoveRel(lua_State* L)
{
	SetCursorPos(lua_tonumber(L, 1), lua_tonumber(L, 2));
	return 0;
}

int MouseScroll(lua_State* L)
{
	mouse_event(MOUSEEVENTF_WHEEL, 0, 0, 0, 0);
	return 0;
}

int KeyPress(lua_State* L)
{
	UINT Key = lua_tonumber(L, 1);
	keybd_event(0, (BYTE)MapVirtualKeyA(Key, MAPVK_VK_TO_VSC), KEYEVENTF_SCANCODE, 0);
	return 0;
}

int KeyRelease(lua_State* L)
{
	UINT Key = lua_tonumber(L, 1);
	keybd_event(0, (BYTE)MapVirtualKeyA(Key, MAPVK_VK_TO_VSC), KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP, 0);
	return 0;
}

int IsRbxActive(lua_State* L)
{
	lua_pushboolean(L, (GetForegroundWindow() == FindWindowA(NULL, "Roblox")));
	return 1;
}

void api::Environment::Input::Register(lua_State* L)
{
	RegisterFunction(L, Mouse1Click, "mouse1click");
	RegisterFunction(L, Mouse2Click, "mouse2click");

	RegisterFunction(L, Mouse1Press, "mouse1press");
	RegisterFunction(L, Mouse2Press, "mouse2press");

	RegisterFunction(L, Mouse1Release, "mouse1release");
	RegisterFunction(L, Mouse2Release, "mouse2release");

	RegisterFunction(L, MouseMoveAbs, "mousemoveabs");
	RegisterFunction(L, MouseMoveRel, "mousemoverel");

	RegisterFunction(L, KeyPress, "keypress");
	RegisterFunction(L, KeyRelease, "keyrelease");

	RegisterFunction(L, IsRbxActive, "isrbxactive");
	RegisterFunction(L, IsRbxActive, "isgameactive");
}