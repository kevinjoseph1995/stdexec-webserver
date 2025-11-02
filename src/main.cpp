#include <cstdint>
#include <exception>
#include <expected>
#include <print>

#include <thread>

#include <netinet/in.h>

#include <exec/repeat_effect_until.hpp>
#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>

#include "client.hpp"
#include "error.hpp"
#include "file_descriptor.hpp"
#include "server.hpp"
#include "uring_context.hpp"

std::expected<uint32_t, Error> parse_port(int argc, char *argv[])
{
    if (argc < 2)
    {
        return std::unexpected(Error("Port number not provided"));
    }
    try
    {
        int port = std::stoi(argv[1]);
        if (port < 1 || port > 65535)
        {
            return std::unexpected(Error("Port number must be between 1 and 65535"));
        }
        return static_cast<uint16_t>(port);
    }
    catch (std::exception const &ex)
    {
        auto message = std::string("Invalid port number: ") + ex.what();
        return std::unexpected(Error(message));
    }
}

int main(int argc, char *argv[])
{
    auto port = parse_port(argc, argv);
    if (!port)
    {
        std::println("Error: {}", port.error().message());
        return 1;
    }
    auto server = Server(port.value());
    auto uring_context = UringContext(1024);
    auto event_loop_thread = std::jthread([&uring_context]() { uring_context.run(); });
    auto thread_pool = exec::static_thread_pool{8};

    // Accept loop: for each new client_fd, spawn a detached client pipeline
    stdexec::sync_wait(async_accept(server, uring_context) | stdexec::then([&](FileDescriptor client_fd) {
                           // In terms of lifetime safety, moving client_fd into the detached task is safe here
                           // because the accept loop will not access client_fd after this point. The detached task
                           // takes ownership of client_fd. The uring_context reference is also safe because it
                           // outlives all client tasks and the main thread. Each client task runs in the thread
                           // pool.
                           stdexec::start_detached(stdexec::on(thread_pool.get_scheduler(),
                                                               handle_connection(std::move(client_fd), uring_context)));
                       }) |
                       exec::repeat_effect());
    return 0;
}
