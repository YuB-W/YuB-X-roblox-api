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
#include <lmem.h>
#include <string>
#include <winioctl.h>
#include <intrin.h>
#include <algorithm>
#include <memory>
#include <cctype>
#include <Environment/Library.hpp>


#undef LoadString
#define oxorany(x) x 
#define RegisterFunction(L, Func, Name) lua_pushcclosure(L, Func, Name, 0); \
lua_setglobal(L, Name);
#define ObfStr(x) (x) 

static std::string exploitName = ObfStr("YuB-X-NG");
static std::string exploitVersion = ObfStr("2.5.6");

int SetClipboard(lua_State* L)
{
	std::size_t length;
	const char* setting = lua_tolstring(L, 1, &length);

	if (!setting)
	{
		lua_pushboolean(L, 0);
		lua_pushstring(L, "Invalid argument to SetClipboard (expected string)");
		return 2;
	}

	if (!OpenClipboard(NULL))
	{
		lua_pushboolean(L, 0);
		lua_pushstring(L, "Failed to open clipboard");
		return 2;
	}

	bool success = false;

	if (EmptyClipboard())
	{
		HGLOBAL hglbCopy = GlobalAlloc(GMEM_MOVEABLE, length + 1);
		if (hglbCopy)
		{
			LPVOID clipboardMemory = GlobalLock(hglbCopy);
			if (clipboardMemory)
			{
				memcpy(clipboardMemory, setting, length + 1);
				GlobalUnlock(hglbCopy);

				if (SetClipboardData(CF_TEXT, hglbCopy))
					success = true;
				else
					GlobalFree(hglbCopy);
			}
			else
			{
				GlobalFree(hglbCopy);
			}
		}
	}

	CloseClipboard();

	lua_pushboolean(L, success);
	if (!success)
		lua_pushstring(L, "Failed to set clipboard data");
	else
		lua_pushnil(L);

	return 2; // (success boolean, error message or nil)
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

int SetFpsCap(lua_State* L)
{
	auto FpsInput = lua_tonumber(L, 1);

	return 0;
}

int IdentifyExecutor(lua_State* L)
{
	lua_pushstring(L, exploitName.c_str());
	lua_pushstring(L, exploitVersion.c_str()); // version as a string
	return 2; // Return two values (name, version)
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


int custom_is_in_roblox_window(lua_State* L) {
	lua_pushboolean(L, GetForegroundWindow() == FindWindowA(0, "Roblox"));
	return 1;
};

int custom_getinstancelist(lua_State* L) {

	lua_pushvalue(L, LUA_REGISTRYINDEX);
	lua_pushlightuserdata(L, (void*)Update::PushInstance);
	lua_gettable(L, -2);
	return 1;
};





namespace SystemFuncs
{
	struct SystemFingerprint
	{
		static constexpr int FingerprintSize = 16;
		unsigned char UniqueFingerprint[16]{};

		std::string ToString()
		{
			std::string result;
			char buffer[3];
			for (int i = 0; i < FingerprintSize; i++)
			{
				sprintf_s(buffer, "%02X", UniqueFingerprint[i]);
				result += buffer;
			}
			return result;
		}

		void InitializeFingerprint()
		{
			for (int i = 0; i < FingerprintSize; ++i)
				UniqueFingerprint[i] = static_cast<unsigned char>(~i & 0xFF);
		}

		void Interleave(unsigned long data)
		{
			auto x = reinterpret_cast<unsigned long*>(UniqueFingerprint);
			x[0] ^= (data + 0x2EF35C3D);
			x[1] ^= (data + 0x6E50D365);
			x[2] ^= (data + 0x73B3E4F9);
			x[3] ^= (data + 0x1A044581);

			auto orig = x[0];
			x[0] ^= x[3];
			x[3] ^= orig * 0x3D05F7D1 + x[0];

			UniqueFingerprint[0] = UniqueFingerprint[15] + UniqueFingerprint[14];
			UniqueFingerprint[14] = UniqueFingerprint[0] + UniqueFingerprint[15];
		}

		static std::string Pad4Byte(const std::wstring& str)
		{
			auto s = str.size() + (4 - str.size() % 4) % 4;
			std::wstring padded = str + std::wstring(s - str.size(), L' ');
			return std::string(padded.begin(), padded.end());
		}

		static std::string GetPhysicalDriveId(DWORD driveIndex)
		{
			std::string drivePath = "\\\\.\\PhysicalDrive" + std::to_string(driveIndex);
			HANDLE hDevice = CreateFileA(drivePath.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
				nullptr, OPEN_EXISTING, 0, nullptr);
			if (hDevice == INVALID_HANDLE_VALUE)
				return "Unknown";

			std::unique_ptr<void, decltype(&CloseHandle)> handleGuard(hDevice, &CloseHandle);

			STORAGE_PROPERTY_QUERY query{};
			query.PropertyId = StorageDeviceProperty;
			query.QueryType = PropertyStandardQuery;

			STORAGE_DESCRIPTOR_HEADER header{};
			DWORD bytesReturned;

			if (!DeviceIoControl(hDevice, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
				&header, sizeof(header), &bytesReturned, nullptr))
				return "Unknown";

			std::unique_ptr<BYTE[]> buffer(new BYTE[header.Size]{});
			if (!DeviceIoControl(hDevice, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
				buffer.get(), header.Size, &bytesReturned, nullptr))
				return "Unknown";

			auto descriptor = reinterpret_cast<STORAGE_DEVICE_DESCRIPTOR*>(buffer.get());
			if (descriptor->SerialNumberOffset == 0)
				return "Unknown";

			const char* serial = reinterpret_cast<const char*>(buffer.get() + descriptor->SerialNumberOffset);
			return std::string(serial);
		}

		static SystemFingerprint* CreateUniqueFingerprint()
		{
			SystemFingerprint* fingerprint = new SystemFingerprint();
			fingerprint->InitializeFingerprint();

			int cpuInfo[4] = { 0 };
			__cpuid(cpuInfo, 0x80000001);
			fingerprint->Interleave(cpuInfo[0] + cpuInfo[1] + (cpuInfo[2] | 0x8000) + cpuInfo[3]);

			DWORD volumeSerial;
			GetVolumeInformation("C:\\", nullptr, 0, &volumeSerial, nullptr, nullptr, nullptr, 0);
			fingerprint->Interleave(volumeSerial);

			char computerName[MAX_COMPUTERNAME_LENGTH + 1] = {};
			DWORD nameLen = MAX_COMPUTERNAME_LENGTH + 1;
			GetComputerNameA(computerName, &nameLen);
			for (DWORD i = 0; i < nameLen; i += 4)
				fingerprint->Interleave(*reinterpret_cast<unsigned long*>(&computerName[i]));

			// Use RegGetValue instead of winreg for wider compatibility
			HKEY hKey;
			if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
			{
				wchar_t buffer[256];
				DWORD size = sizeof(buffer);
				if (RegGetValueW(hKey, nullptr, L"ProcessorNameString", RRF_RT_REG_SZ, nullptr, buffer, &size) == ERROR_SUCCESS)
				{
					std::string processor = Pad4Byte(buffer);
					for (size_t i = 0; i < processor.size(); i += 4)
						fingerprint->Interleave(*reinterpret_cast<const unsigned long*>(&processor[i]));
				}
				RegCloseKey(hKey);
			}

			auto driveId = GetPhysicalDriveId(0);
			for (size_t i = 0; i < driveId.size(); i += 4)
				fingerprint->Interleave(*reinterpret_cast<const unsigned long*>(&driveId[i]));

			return fingerprint;
		}
	};
}

int custom_GETHWID(lua_State* L) {
	SystemFuncs::SystemFingerprint fingerprint;
	std::string HWID = fingerprint.CreateUniqueFingerprint()->ToString();

	lua_pushlstring(L, HWID.data(), HWID.size());
	return 1;
};

int custom_MessageBox(lua_State* L) {
	luaL_checktype(L, 1, LUA_TSTRING);
	luaL_checktype(L, 2, LUA_TSTRING);
	luaL_checktype(L, 3, LUA_TNUMBER);

	LPCSTR message = lua_tostring(L, 1);
	LPCSTR title = lua_tostring(L, 2);
	int id = lua_tonumberx(L, 3, NULL);

	lua_pushnumber(L, MessageBoxA(NULL, message, title, id));
	return 1;
};


void api::Environment::Extra::Register(lua_State* L)
{
	RegisterFunction(L, custom_MessageBox, oxorany("messagebox"));
	RegisterFunction(L, custom_GETHWID, oxorany("gethwid"));
	RegisterFunction(L, custom_getinstancelist, oxorany("getinstances"));
	RegisterFunction(L, custom_getinstancelist, oxorany("get_instances"));
	RegisterFunction(L, custom_is_in_roblox_window, oxorany("isrbxactive"));
	RegisterFunction(L, custom_is_in_roblox_window, oxorany("isgameactive"));
	RegisterFunction(L, SetClipboard, oxorany("setrbxclipboard"));
	RegisterFunction(L, SetClipboard, oxorany("setclipboard"));
	RegisterFunction(L, SetClipboard, oxorany("toclipboard"));
	RegisterFunction(L, MessageBoxFunc, oxorany("messagebox"));
	RegisterFunction(L, SetFpsCap, oxorany("setfpscap"));
	RegisterFunction(L, IdentifyExecutor, oxorany("identifyexecutor"));
	RegisterFunction(L, getexecutorname, oxorany("getexecutorname"));
	RegisterFunction(L, SetIdentity, oxorany("setidentity"));
	RegisterFunction(L, SetIdentity, oxorany("setthreadidentity"));
	RegisterFunction(L, SetIdentity, oxorany("setthreadcontext"));
	RegisterFunction(L, GetIdentity, oxorany("getidentity"));
	RegisterFunction(L, GetIdentity, oxorany("getthreadidentity"));
	RegisterFunction(L, GetIdentity, oxorany("getthreadcontext"));
}
