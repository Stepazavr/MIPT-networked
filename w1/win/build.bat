@echo off

if not exist bin\ (
    mkdir bin
)

g++ server.cpp socket_tools.cpp DuelManager.cpp MessageManager.cpp -std=c++17 -o bin/server.exe -lws2_32
g++ client.cpp socket_tools.cpp -std=c++17 -o bin/client.exe -lws2_32
