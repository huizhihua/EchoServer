#include <winsock2.h>
#include <windows.h>

#include <atomic>

enum class IO_OPERATION
{
    ACCEPT,
    SEND,
    RECEIVE,
};

class ConnectionContext
{
public:
    ConnectionContext(SOCKET socket) : _socket(socket)
    {
    }

    SOCKET _socket = INVALID_SOCKET;
    std::mutex _mutex;
    std::atomic<bool> _is_connected{true};
};

constexpr size_t BUFFER_SIZE = 4096;
class IO_Context
{
public:
    IO_Context(IO_OPERATION operation, ConnectionContext *connection_context, SOCKET socket)
        : _operation(operation), _connection_context(connection_context), _socket(socket)
    {
        _wsa_buffer.buf = _buffer;
        _wsa_buffer.len = BUFFER_SIZE;
    }
    IO_Context(IO_OPERATION operation, ConnectionContext *connection_context, SOCKET socket,
               const char *data, size_t length)
        : _operation(operation), _connection_context(connection_context), _socket(socket)
    {
        _wsa_buffer.buf = _buffer;
        _wsa_buffer.len = length;
        memcpy(_buffer, data, length);
    }

    OVERLAPPED _overlapped{};
    IO_OPERATION _operation;
    ConnectionContext *_connection_context = nullptr;
    SOCKET _socket = INVALID_SOCKET;
    WSABUF _wsa_buffer{};
    char _buffer[BUFFER_SIZE]{};
    char _address_buffer[(sizeof(sockaddr_in) + 16) * 2]{};
};