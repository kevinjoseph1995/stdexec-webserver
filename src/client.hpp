#pragma once
#include "file_descriptor.hpp"
#include "http.hpp"
#include "uring_context.hpp"

#include <exec/repeat_effect_until.hpp>
#include <exec/task.hpp>

#include <cstddef>
#include <print>
#include <span>
#include <vector>

struct Client
{
    FileDescriptor m_fd;
    UringContext &m_uring_ctx;
    std::vector<std::byte> m_buffer;

    Client(FileDescriptor fd, UringContext &uring_ctx)
        : m_fd(std::move(fd)), m_uring_ctx(uring_ctx), m_buffer(std::vector<std::byte>(4096))
    {
    }
};

inline auto read_request(Client &client)
{
    using namespace stdexec;
    return async_read(client.m_fd.get(), client.m_uring_ctx, std::span<std::byte>(client.m_buffer)) |
           then([&buffer = client.m_buffer](size_t bytes_read) {
               return std::span<std::byte>(buffer.data(), bytes_read);
           });
}

inline auto handle_connection(FileDescriptor client_fd, UringContext &uring_ctx)
{
    return stdexec::let_value(stdexec::just(Client(std::move(client_fd), uring_ctx)),
                              [](Client &client) { return read_request(client); });
}