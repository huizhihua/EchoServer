#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>

#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <string>
#include <functional>

#include "base.hpp"
#include "../utils/utils.hpp"

class TcpServer
{
public:
    TcpServer();
    ~TcpServer();

    bool start(const char *ip, unsigned short port);
    void stop();

private:
    bool init_listen_socket(const char *ip, unsigned short port);
    bool init_iocp();
    bool init_lpfn_acceptex();
    bool init_worker_threads(unsigned int thread_count = std::thread::hardware_concurrency());
    void init_accept_contexts(unsigned int accept_count);
    void worker_loop();

    void cleanup_listen_socket();
    void stop_iocp();
    void stop_worker_threads();
    void cleanup_iocp();

    bool post_accept();
    bool post_receive(ConnectionContext *connection_context);
    bool post_send(ConnectionContext *connection_context, const char *data, size_t length);

    void handle_accept(IO_Context *io_context);
    void handle_receive(IO_Context *io_context, DWORD bytes_transferred);
    void handle_send(IO_Context *io_context, DWORD bytes_transferred);

    void close_connection(ConnectionContext *connection_context);

private:
    static constexpr size_t BUFFER_SIZE = 4096;

    std::atomic<bool> _running{false};

    std::string _ip{};
    unsigned short _port{0};
    SOCKET _listen_socket;

    HANDLE _iocp{nullptr};
    LPFN_ACCEPTEX _lpfn_acceptex{nullptr};

    unsigned int _threads_count{};
    std::vector<std::thread> _worker_threads{};

    std::function<std::string(const char *)> _func = [](const char *msg)
    { return msg; };
};