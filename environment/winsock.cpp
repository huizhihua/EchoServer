#include "winsock.hpp"

WinsockInitializer::WinsockInitializer()
{
    int result = WSAStartup(MAKEWORD(2, 2), &_wsaData);
    if (result != 0)
    {
        throw std::runtime_error("WSAStartup failed with error: " + std::to_string(result));
    }
}

WinsockInitializer::~WinsockInitializer()
{
    WSACleanup();
}