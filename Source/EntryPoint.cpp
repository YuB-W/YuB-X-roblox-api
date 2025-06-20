#include "Includes.hpp"
#include "Scheduler.hpp"
#include "Execution.hpp"
#include "Environment.hpp"
#include "overlay/overlay.hpp"

#pragma comment(lib, "ws2_32.lib")

#define PORT "5454"

bool recvAll(SOCKET sock, char* buffer, int totalBytes) {
    int received = 0;
    while (received < totalBytes) {
        int result = recv(sock, buffer + received, totalBytes - received, 0);
        if (result <= 0) return false;
        received += result;
    }
    return true;
}

void ServerThread() {
    WSADATA wsaData;
    struct addrinfo* result = nullptr, hints = {};
    SOCKET ListenSocket = INVALID_SOCKET, ClientSocket = INVALID_SOCKET;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return;

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(nullptr, PORT, &hints, &result) != 0) {
        WSACleanup();
        return;
    }

    ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (ListenSocket == INVALID_SOCKET) {
        freeaddrinfo(result);
        WSACleanup();
        return;
    }

    if (bind(ListenSocket, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
        closesocket(ListenSocket);
        freeaddrinfo(result);
        WSACleanup();
        return;
    }

    freeaddrinfo(result);

    if (listen(ListenSocket, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(ListenSocket);
        WSACleanup();
        return;
    }

    while (true) {
        ClientSocket = accept(ListenSocket, nullptr, nullptr);
        if (ClientSocket == INVALID_SOCKET) continue;

        uint32_t scriptSizeNet = 0;
        if (!recvAll(ClientSocket, reinterpret_cast<char*>(&scriptSizeNet), 4)) {
            closesocket(ClientSocket);
            continue;
        }

        uint32_t scriptSize = ntohl(scriptSizeNet);
        if (scriptSize == 0 || scriptSize > 10 * 1024 * 1024) {
            closesocket(ClientSocket);
            continue;
        }

        std::vector<char> buffer(scriptSize + 1, 0);
        if (!recvAll(ClientSocket, buffer.data(), scriptSize)) {
            closesocket(ClientSocket);
            continue;
        }

        std::string receivedScript(buffer.data(), scriptSize);

        Execution->Execute(receivedScript);
        closesocket(ClientSocket);
    }

    closesocket(ListenSocket);
    WSACleanup();
}

void disable_flags()
{
    *reinterpret_cast<BYTE*>(Offsets::InternalFastFlags::EnableLoadModule) = TRUE;
    *reinterpret_cast<BYTE*>(Offsets::InternalFastFlags::DebugCheckRenderThreading) = FALSE;
    *reinterpret_cast<BYTE*>(Offsets::InternalFastFlags::RenderDebugCheckThreading2) = FALSE;
    *reinterpret_cast<BYTE*>(Offsets::InternalFastFlags::DisableCorescriptLoadstring) = FALSE;
    *reinterpret_cast<BYTE*>(Offsets::InternalFastFlags::LuaStepIntervalMsOverrideEnabled) = FALSE;
    *reinterpret_cast<BYTE*>(Offsets::InternalFastFlags::LockViolationInstanceCrash) = FALSE;
}

void Start()
{
    disable_flags();

	auto LuaState = Scheduler->GetLuaState();

	LuaState->userdata->Identity = 8;
	LuaState->userdata->Capabilities = MaxCaps;

	Environment->Initialize(LuaState);

    luaL_sandboxthread(LuaState);

    Execution->Execute("print('YubxInternal Loaded!')");
    std::thread(gui::overlay::render).detach();
	while (true) { Sleep(10000); };
}

BOOL APIENTRY DllMain(HMODULE Module, DWORD Reason, LPVOID Reserved) {
    switch (Reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(Module);
        std::thread(InitializeExploitation);
        break;
    }
    return TRUE;
}
