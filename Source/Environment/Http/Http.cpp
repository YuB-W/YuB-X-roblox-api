#include "http.hpp"
#include <curl/curl.h>
#include "RBX.hpp"
#include "../../Dependencies/Luau/VM//src/lgc.h"
#include "../../Dependencies/Luau/VM//src/lapi.h"
#include <lualib.h>
#include <Base64/Base64.h>

size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output)
{
    size_t totalSize = size * nmemb;
    output->append((char*)contents, totalSize);
    return totalSize;
}
#define oxorany(x) x 
#define RegisterFunction(L, Func, Name) lua_pushcclosure(L, Func, Name, 0); \

#define RegisterMember(L, Func, Name) lua_pushcclosure(L, Func, Name, 0); \
lua_setfield(L, -2, Name);
int httpGet(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TSTRING);
    std::string url = lua_tostring(L, 1);

    CURL* curl = curl_easy_init();
    if (!curl)
    {
        lua_pushnil(L);
        lua_pushstring(L, "Failed to initialize CURL");
        return 2;
    }

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
    {
        lua_pushnil(L);
        lua_pushstring(L, curl_easy_strerror(res));
        return 2;
    }

    lua_pushstring(L, response.c_str());
    return 1;
}

void api::Environment::Http::Register(lua_State* L)
{
    lua_newtable(L);
    RegisterMember(L, httpGet, "get");
    lua_setglobal(L, "http");

    RegisterFunction(L, httpGet, "httpget");
    lua_setglobal(L, "httpget");
}
