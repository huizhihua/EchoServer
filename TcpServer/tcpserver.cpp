#include <iostream>

#include "tcpserver.hpp"

TcpServer::TcpServer()
{
}

TcpServer::~TcpServer()
{
    if (_running)
    {
        stop();
    }
}

bool TcpServer::start(const char *ip, unsigned short port)
{
    if (_running)
    {
        print("stopping...");
        stop();
    }

    auto result = init_listen_socket(ip, port);
    if (!result)
    {
        print("init_listen_socket fail");
        return false;
    }

    result = init_iocp();
    if (!result)
    {
        print("init_iocp fail");
        return false;
    }

    result = init_lpfn_acceptex();
    if (!result)
    {
        print("init_lpfn_acceptex fail");
        return false;
    }

    result = init_worker_threads();
    if (!result)
    {
        print("init_worker_threads fail");
        return false;
    }

    init_accept_contexts(4);

    _running.store(true);
    print("server start listen ip:" + std::string(ip) + " port:" + std::to_string(port));
    return true;
}

void TcpServer::stop()
{
    if (!_running.exchange(false))
        return;

    cleanup_listen_socket();
    stop_iocp();
    stop_worker_threads();
    cleanup_iocp();
}

bool TcpServer::init_listen_socket(const char *ip, unsigned short port)
{
    if (ip == nullptr || port < 0 || port > 65535)
        return false;

    _ip = ip;
    _port = port;
    _listen_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (_listen_socket == INVALID_SOCKET)
    {
        print("listen socket create fail");
        return false;
    }

    SOCKADDR_IN server_address_in{};
    server_address_in.sin_family = AF_INET;
    server_address_in.sin_addr.s_addr = inet_addr(ip);
    server_address_in.sin_port = htons(port);
    int result = bind(_listen_socket,
                      reinterpret_cast<SOCKADDR *>(&server_address_in), sizeof(server_address_in));
    if (result == SOCKET_ERROR)
    {
        print("listen socket bind fail");
        closesocket(_listen_socket);
        _ip = "";
        _port = 0;
        _listen_socket = INVALID_SOCKET;
        return false;
    }

    result = listen(_listen_socket, SOMAXCONN);
    if (result == SOCKET_ERROR)
    {
        print("listen socket listen fail");
        closesocket(_listen_socket);
        _ip = "";
        _port = 0;
        _listen_socket = INVALID_SOCKET;
        return false;
    }
    return true;
}

bool TcpServer::init_iocp()
{
    _iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (!_iocp)
    {
        print("create iocp fail");
        return false;
    }

    auto result = CreateIoCompletionPort(reinterpret_cast<HANDLE>(_listen_socket), _iocp, 0, 0);
    if (!result)
    {
        print("bind listen socket to iocp fail");
        CloseHandle(_iocp);
        return false;
    }
    return true;
}

bool TcpServer::init_lpfn_acceptex()
{
    GUID id = WSAID_ACCEPTEX;
    DWORD bytes_received = 0;
    int result = WSAIoctl(_listen_socket, SIO_GET_EXTENSION_FUNCTION_POINTER, &id, sizeof(id),
                          &_lpfn_acceptex, sizeof(_lpfn_acceptex), &bytes_received, nullptr, nullptr);
    if (result == SOCKET_ERROR)
    {
        print("get acceptex fail");
        _lpfn_acceptex = nullptr;
        return false;
    }

    return true;
}

bool TcpServer::init_worker_threads(unsigned int thread_count)
{
    _threads_count = std::max(thread_count, std::thread::hardware_concurrency());
    for (unsigned int i = 0; i < _threads_count; ++i)
    {
        _worker_threads.emplace_back(&TcpServer::worker_loop, this);
    }
    return true;
}

void TcpServer::init_accept_contexts(unsigned int accept_count)
{
    for (auto i = 0; i < accept_count; ++i)
    {
        post_accept();
    }
}

void TcpServer::worker_loop()
{
    while (true)
    {
        DWORD bytes_transferred = 0;
        ULONG_PTR completion_key = 0;
        OVERLAPPED *overlapped = nullptr;
        BOOL result = GetQueuedCompletionStatus(_iocp, &bytes_transferred, &completion_key,
                                                &overlapped, INFINITE);
        if (overlapped == nullptr)
            break;

        if (!result)
        {
            IO_Context *io = reinterpret_cast<IO_Context *>(overlapped);
            if (io->_operation == IO_OPERATION::ACCEPT)
            {
                closesocket(io->_socket);
            }
            else if (io->_connection_context != nullptr)
            {
                close_connection(io->_connection_context);
            }

            delete io;
            continue;
        }

        IO_Context *io = reinterpret_cast<IO_Context *>(overlapped);
        switch (io->_operation)
        {
        case IO_OPERATION::ACCEPT:

            handle_accept(io);

            break;
        case IO_OPERATION::RECEIVE:

            handle_receive(io, bytes_transferred);

            break;
        case IO_OPERATION::SEND:

            handle_send(io, bytes_transferred);

            break;
        }
    }
}

void TcpServer::cleanup_listen_socket()
{
    if (_listen_socket == INVALID_SOCKET)
        return;

    closesocket(_listen_socket);
    _listen_socket = INVALID_SOCKET;
    _ip = "";
    _port = 0;
}

void TcpServer::stop_iocp()
{
    for (unsigned int i = 0; i < _threads_count; ++i)
    {
        PostQueuedCompletionStatus(_iocp, 0, 0, nullptr);
    }
}

void TcpServer::stop_worker_threads()
{
    for (auto &worker : _worker_threads)
    {
        if (worker.joinable())
            worker.join();
    }
    _threads_count = 0;
    _worker_threads.clear();
}

void TcpServer::cleanup_iocp()
{
    CloseHandle(_iocp);
    _iocp = nullptr;
    _lpfn_acceptex = nullptr;
}

bool TcpServer::post_accept()
{
    SOCKET client_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (client_socket == INVALID_SOCKET)
        return false;

    IO_Context *io = new IO_Context(IO_OPERATION::ACCEPT, nullptr, client_socket);

    DWORD bytesReceived = 0;
    BOOL result = _lpfn_acceptex(_listen_socket, client_socket, io->_address_buffer, 0,
                                 sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16, &bytesReceived,
                                 &io->_overlapped);
    if (!result && WSAGetLastError() != ERROR_IO_PENDING)
    {
        closesocket(client_socket);
        delete io;
        return false;
    }

    return true;
}

bool TcpServer::post_receive(ConnectionContext *connection_context)
{
    if (connection_context == nullptr)
        return false;

    if (!connection_context->_is_connected)
        return false;

    IO_Context *io = new IO_Context(IO_OPERATION::RECEIVE, connection_context, connection_context->_socket);

    DWORD flags = 0;
    DWORD bytes_received = 0;
    int result = WSARecv(connection_context->_socket, &io->_wsa_buffer, 1, &bytes_received, &flags,
                         &io->_overlapped, nullptr);

    if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
    {
        delete io;
        return false;
    }

    return true;
}

bool TcpServer::post_send(ConnectionContext *connection_context, const char *data, size_t length)
{
    if (connection_context == nullptr)
        return false;

    if (!connection_context->_is_connected)
        return false;

    if (length == 0)
        return true;

    IO_Context *io = new IO_Context(IO_OPERATION::SEND, connection_context, connection_context->_socket,
                                    data, length);

    DWORD bytes_send = 0;
    int result = WSASend(connection_context->_socket, &io->_wsa_buffer, 1, &bytes_send, 0,
                         &io->_overlapped, nullptr);

    if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
    {
        delete io;
        return false;
    }

    return true;
}

void TcpServer::handle_accept(IO_Context *io_context)
{
    print("handle accept");
    SOCKET client_socket = io_context->_socket;
    setsockopt(client_socket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
               reinterpret_cast<const char *>(&_listen_socket), sizeof(_listen_socket));

    HANDLE result = CreateIoCompletionPort(reinterpret_cast<HANDLE>(client_socket), _iocp, 0, 0);
    if (!result)
    {
        closesocket(client_socket);
        delete io_context;
        post_accept();
        return;
    }

    ConnectionContext *connection = new ConnectionContext(client_socket);

    bool is_post = post_receive(connection);
    if (!is_post)
    {
        close_connection(connection);
        delete connection;
    }

    delete io_context;
    post_accept();
}

void TcpServer::handle_receive(IO_Context *io_context, DWORD bytes_transferred)
{
    ConnectionContext *connection = io_context->_connection_context;
    if (bytes_transferred == 0)
    {
        close_connection(connection);
        delete io_context;
        delete connection;
        return;
    }

    auto received = std::string(io_context->_buffer, bytes_transferred);
    print("received: " + received);

    post_send(connection, io_context->_buffer, bytes_transferred);

    delete io_context;
    post_receive(connection);
}

void TcpServer::handle_send(IO_Context *io_context, DWORD bytes_transferred)
{
    auto send = std::string(io_context->_buffer, bytes_transferred);
    print("send: " + send);
    delete io_context;
}

void TcpServer::close_connection(ConnectionContext *connection_context)
{
    if (connection_context == nullptr)
        return;

    std::lock_guard<std::mutex> lock(connection_context->_mutex);

    // 如果已经关闭，就不用重复关闭
    if (!connection_context->_is_connected.exchange(false))
        return;

    if (connection_context->_socket == INVALID_SOCKET)
        return;

    closesocket(connection_context->_socket);
    connection_context->_socket = INVALID_SOCKET;
}
