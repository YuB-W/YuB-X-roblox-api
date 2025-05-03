#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>
#include <vector>
#include <mutex>
#include "RBX.hpp"
#include "Scheduler/Scheduler.hpp"
#include <Environment/Environment.hpp>

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

    RBX::Print(0, "[YuB-X] Listening on port " PORT "...");

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
          lua_State* L = Manager->GetLuaState();
          if (L) {
              Execution->Send(L, script);
          }
          else {
              RBX::Print(0, "[YuB-X] ❌ Failed to execute: no valid Lua state");
          }
        closesocket(ClientSocket);
    }

    closesocket(ListenSocket);
    WSACleanup();
}

void StartServer() {
    ServerThread();
}
