#include "FileSys.hpp"
#include <Luau/Compiler.h>
#include <cpr/cpr.h>
#include "RBX.hpp"
#include <lualib.h>
#include <unordered_set>
#include <fstream>
#include <filesystem>
#include <exception>

#define oxorany(x) x 
#define EXPLOIT_DEFAULTPATH std::filesystem::path(getenv("localappdata")) / "Cloudy"

std::filesystem::path GetFolderPath(std::string FolderName)
{
	std::filesystem::path Folder = EXPLOIT_DEFAULTPATH / FolderName;

	if (!std::filesystem::exists(EXPLOIT_DEFAULTPATH))
	{
		std::filesystem::create_directory(EXPLOIT_DEFAULTPATH);
	}
	else if (!std::filesystem::exists(Folder))
	{
		std::filesystem::create_directory(Folder);
	}

	return Folder;
}

#define RegisterFunction(L, Func, Name) lua_pushcclosure(L, Func, Name, 0); \
lua_setglobal(L, Name);

std::unordered_set<std::string> BlExt = { oxorany(".wsf", ".exe", ".dll", ".bat", ".cmd", ".scr", ".vbs", ".js",
														 ".ts",  ".wsf", ".msi", ".com", ".lnk", ".ps1", ".py",
														 ".py3", ".pyc", ".pyw", ".scr", ".msi", ".html") };

int WriteFile(lua_State* L)
{
	size_t ContentSize{};
	auto FilePath = std::string(lua_tostring(L, 1));

	auto Contents = std::string(luaL_checklstring(L, 2, &ContentSize));

	for (std::string FileExt : BlExt)
	{
		if (FilePath.find(FileExt) != std::string::npos)
		{
			luaL_error(L, oxorany("This file extension is not allowed!"));
			return 1;
		}
	}

	if (FilePath.find("../") != std::string::npos)
	{
		luaL_error(L, oxorany("Attempt to escape directory"));
		return 1;
	}

	std::ofstream FileStream(GetFolderPath(oxorany("workspace")) / FilePath, std::ios::binary);

	FileStream.write(Contents.c_str(), ContentSize);

	return 0;

}

int ReadFile(lua_State* L)
{
	auto FilePath = std::string(lua_tostring(L, 1));

	for (std::string FileExt : BlExt)
	{
		if (FilePath.find(FileExt) != std::string::npos)
		{
			luaL_error(L, oxorany("This file extension is not allowed!"));
			return 1;
		}
	}

	if (FilePath.find("../") != std::string::npos)
	{
		luaL_error(L, oxorany("Attempt to escape directory"));
		return 1;
	}

	std::ostringstream ContentStream{};

	std::ifstream FileStream(GetFolderPath(oxorany("workspace")) / FilePath);

	std::string FileContents{ std::istreambuf_iterator<char>(FileStream), std::istreambuf_iterator<char>() };
	FileStream.close();

	lua_pushstring(L, FileContents.c_str());
	return 1;

}

int MakeFolder(lua_State* L)
{
	auto FolderPath = std::string(lua_tostring(L, 1));

	if (FolderPath.find("../") != std::string::npos)
	{
		luaL_error(L, oxorany("Attempt to escape directory"));
		return 0;
	}
	try
	{
		std::filesystem::create_directories(GetFolderPath(oxorany("workspace")) / FolderPath);
	}
	catch (std::exception& Ex)
	{
		luaL_error(L, oxorany("Failed to create a directory, %s"), Ex.what());
		return 1;
	};

	return 0;
}

int delfolder(lua_State* L)
{
	auto FolderPath = std::string(lua_tostring(L, 1));

	if (FolderPath.find("../") != std::string::npos)
	{
		luaL_error(L, oxorany("Attempt to escape directory"));
		return 0;
	}

	try
	{
		auto fullPath = GetFolderPath(oxorany("workspace")) / FolderPath;

		if (!std::filesystem::exists(fullPath))
		{
			luaL_error(L, oxorany("Folder does not exist"));
			return 1;
		}

		std::filesystem::remove_all(fullPath);
	}
	catch (std::exception& Ex)
	{
		luaL_error(L, oxorany("Failed to delete directory, %s"), Ex.what());
		return 1;
	}

	return 0;
}

int ExpDeleteFile(lua_State* L)
{
	auto FilePath = std::string(lua_tostring(L, 1));

	if (FilePath.find(oxorany("../")) != std::string::npos)
	{
		luaL_error(L, oxorany("Attempt to escape directory"));
		return 0;
	}
	try
	{
		std::filesystem::remove(GetFolderPath(oxorany("workspace")) / FilePath);
	}
	catch (std::exception& Ex)
	{
		luaL_error(L, oxorany("Failed to delete %s"), Ex.what());
		return 1;
	};

	return 0;
}

int AppendFile(lua_State* L)
{
	size_t ContentSize{};
	auto FilePath = std::string(lua_tostring(L, 1));

	auto Contents = std::string(luaL_checklstring(L, 2, &ContentSize));

	for (std::string FileExt : BlExt)
	{
		if (FilePath.find(FileExt) != std::string::npos)
		{
			luaL_error(L, oxorany("This file extension is not allowed!"));
			return 1;
		}
	}

	if (FilePath.find(oxorany("../")) != std::string::npos)
	{
		luaL_error(L, oxorany("Attempt to escape directory"));
		return 1;
	}

	std::ostringstream ContentStream{};

	std::ifstream FileStream(GetFolderPath("workspace") / FilePath);

	std::string FileContents{ std::istreambuf_iterator<char>(FileStream), std::istreambuf_iterator<char>() };
	FileStream.close();

	std::ofstream FileStream2(GetFolderPath("workspace") / FilePath, std::ios::binary);

	FileContents.append(Contents);

	FileStream2.write(FileContents.c_str(), FileContents.size());

	FileStream2.close();

	return 0;
}

int IsFile(lua_State* L)
{
	auto FilePath = std::string(lua_tostring(L, 1));

	for (std::string FileExt : BlExt)
	{
		if (FilePath.find(FileExt) != std::string::npos)
		{
			luaL_error(L, oxorany("This file extension is not allowed!"));
			return 1;
		}
	}

	if (FilePath.find(oxorany("../")) != std::string::npos)
	{
		luaL_error(L, oxorany("Attempt to escape directory"));
		return 1;
	}

	std::filesystem::path fullPath = GetFolderPath(oxorany("workspace")) / FilePath;
	if (std::filesystem::exists(fullPath) && std::filesystem::is_regular_file(fullPath))
	{
		lua_pushboolean(L, true); 
	}
	else
	{
		lua_pushboolean(L, false);  
	}

	return 1;
}

int IsFolder(lua_State* L)
{
	auto FolderPath = std::string(lua_tostring(L, 1));

	if (FolderPath.find(oxorany("../")) != std::string::npos)
	{
		luaL_error(L, oxorany("Attempt to escape directory"));
		return 1;
	}

	lua_pushboolean(L, std::filesystem::exists(GetFolderPath(oxorany("workspace")) / FolderPath));
	return 1;
}

int loadfile(lua_State* L)
{
	const char* filename = luaL_checkstring(L, 1);
	std::string FilePath(filename);

	for (const std::string& FileExt : BlExt) {
		if (FilePath.find(FileExt) != std::string::npos) {
			lua_pushnil(L);
			lua_pushstring(L, "Blocked file extension");
			return 2;
		}
	}

	if (FilePath.find("../") != std::string::npos || FilePath.find("..\\") != std::string::npos) {
		lua_pushnil(L);
		lua_pushstring(L, "Invalid path");
		return 2;
	}

	auto fullPath = GetFolderPath("workspace") / FilePath;
	std::ifstream file(fullPath, std::ios::binary);
	if (!file.is_open()) {
		lua_pushnil(L);
		lua_pushstring(L, "Could not open file");
		return 2;
	}

	std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	file.close();

	std::string chunkname = "@" + FilePath;

	int result = luau_load(L, chunkname.c_str(), content.c_str(), content.size(), 0);

	if (result != LUA_OK) {
		const char* err = lua_tostring(L, -1);
		lua_pushnil(L);
		lua_pushstring(L, err ? err : "Compilation failed");
		return 2;
	}

	return 1;
}

int delfile(lua_State* L)
{
	auto FilePath = std::string(lua_tostring(L, 1));

	if (FilePath.find("../") != std::string::npos)
	{
		luaL_error(L, oxorany("Attempt to escape directory"));
		return 0;
	}

	try
	{
		auto fullPath = GetFolderPath(oxorany("workspace")) / FilePath;

		if (!std::filesystem::exists(fullPath))
		{
			luaL_error(L, oxorany("File does not exist"));
			return 1;
		}

		std::filesystem::remove(fullPath);
	}
	catch (std::exception& Ex)
	{
		luaL_error(L, oxorany("Failed to delete file, %s"), Ex.what());
		return 1;
	}

	return 0;
}


void api::Environment::FileSystem::Register(lua_State* L)
{
	RegisterFunction(L, WriteFile, oxorany("writefile"));
	RegisterFunction(L, ReadFile, oxorany("readfile"));
	RegisterFunction(L, ExpDeleteFile, oxorany("deletefile"));
	RegisterFunction(L, MakeFolder, oxorany("makefolder"));
	RegisterFunction(L, delfolder, oxorany("delfolder"));
	RegisterFunction(L, delfile, oxorany("delfile"));
	RegisterFunction(L, loadfile, oxorany("loadfile"));
	RegisterFunction(L, AppendFile, oxorany("appendfile"));

	// Is functions

	RegisterFunction(L, IsFolder, oxorany("isfolder"));
	RegisterFunction(L, IsFile, oxorany("isfile"));



}