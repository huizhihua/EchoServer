#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <thread>
#include <mutex>
#include <condition_variable>

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>

#include "./environment/winsockinitializer.hpp"
#include "./TcpServer/tcpserver.hpp"

int main()
{
    WinsockInitializer winsockInitializer;

    TcpServer echo_server;
    echo_server.start("127.0.0.1", 9999, [](const char *msg, unsigned int size) -> std::string
                      { return std::string(msg, size); });

    std::string op;
    while (std::cin >> op)
    {
        if (op == "exit")
            break;
        std::cout << "op: " << op << std::endl;
    }

    return 0;
}