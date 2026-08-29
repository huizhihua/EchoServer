#pragma once

#include <string>
#include <stdexcept>

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>

class WinsockInitializer
{
public:
    WinsockInitializer();
    ~WinsockInitializer();

private:
    WSADATA _wsaData;
};