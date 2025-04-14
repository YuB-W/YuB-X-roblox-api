#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>
#include "RBX.hpp"
#include "Scheduler/Scheduler.hpp"
#include <Environment/Environment.hpp>


#pragma comment(lib, "ws2_32.lib")

#define PORT "5454"
#define BUFFER_SIZE 8192

void ServerThread() {
    WSADATA wsaData;
    struct addrinfo* result = NULL, hints = {};
    SOCKET ListenSocket = INVALID_SOCKET, ClientSocket = INVALID_SOCKET;
    char recvbuf[BUFFER_SIZE];

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed" << std::endl;
        return;
    }

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, PORT, &hints, &result) != 0) {
        std::cerr << "getaddrinfo failed" << std::endl;
        WSACleanup();
        return;
    }

    ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (ListenSocket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed" << std::endl;
        freeaddrinfo(result);
        WSACleanup();
        return;
    }

    if (bind(ListenSocket, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
        std::cerr << "Bind failed" << std::endl;
        closesocket(ListenSocket);
        freeaddrinfo(result);
        WSACleanup();
        return;
    }

    freeaddrinfo(result);

    if (listen(ListenSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed" << std::endl;
        closesocket(ListenSocket);
        WSACleanup();
        return;
    }

    while (true) {
        ClientSocket = accept(ListenSocket, NULL, NULL);
        if (ClientSocket == INVALID_SOCKET) {
            std::cerr << "Accept failed" << std::endl;
            closesocket(ListenSocket);
            WSACleanup();
            return;
        }

        int bytesReceived = recv(ClientSocket, recvbuf, BUFFER_SIZE - 1, 0);
        if (bytesReceived > 0) {
            recvbuf[bytesReceived] = '\0';
            std::string receivedScript(recvbuf);
            //RBX::Print(0, receivedScript.c_str());
            Execution->Send(Manager->GetLuaState(), receivedScript);
        }
        closesocket(ClientSocket);
    }

    closesocket(ListenSocket);
    WSACleanup();
}

void StartServer() {
    //RBX::Print(0, "Byfron Who?");
    CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ServerThread, NULL, 0, NULL);
}

