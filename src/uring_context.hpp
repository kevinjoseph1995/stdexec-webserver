#pragma once
#include "file_descriptor.hpp"
#include "server.hpp"

#include <cassert>
#include <cstddef>
#include <cstring>
#include <liburing.h>
#include <stdexcept>
#include <stdexec/execution.hpp>
#include <unistd.h>

enum class UringOpType
{
    Accept,
    Read,
    Write
};

struct Event
{
    UringOpType type;
    void *data;
};

struct AcceptOperationData
{
    void *op_state_ptr;
    void (*completion_handler)(void *op_state_ptr, int client_fd) noexcept;
};

struct ReadOperationData
{
    void *op_state_ptr;
    void (*completion_handler)(void *op_state_ptr, ssize_t bytes_read) noexcept;
};

struct WriteOperationData
{
    void *op_state_ptr;
    void (*completion_handler)(void *op_state_ptr, ssize_t bytes_written) noexcept;
};

class UringContext
{
  public:
    explicit UringContext(unsigned entries = 256)
    {
        int ret = io_uring_queue_init(entries, &m_ring, 0);
        if (ret < 0)
        {
            throw std::runtime_error("io_uring_queue_init failed");
        }
    }
    UringContext() = delete;
    UringContext(const UringContext &) = delete;
    UringContext &operator=(const UringContext &) = delete;

    UringContext(UringContext &&other) noexcept : m_ring(other.m_ring)
    {
        if (other.m_ring.ring_fd != -1)
        {
            io_uring_queue_exit(&m_ring);
            m_ring = other.m_ring;
            std::memset(&other.m_ring, 0, sizeof(other.m_ring));
            other.m_ring.ring_fd = -1;
        }
    }

    UringContext &operator=(UringContext &&other) noexcept
    {
        if (other.m_ring.ring_fd != -1 && this != &other)
        {
            io_uring_queue_exit(&m_ring);
            m_ring = other.m_ring;
            std::memset(&other.m_ring, 0, sizeof(other.m_ring));
            other.m_ring.ring_fd = -1;
        }
        return *this;
    }

    void run()
    {
        struct io_uring_cqe *cqe;
        while (true)
        {
            int ret = io_uring_wait_cqe(&m_ring, &cqe);
            if (ret < 0)
            {
                throw std::runtime_error("io_uring_wait_cqe failed");
            }

            Event *event = static_cast<Event *>(io_uring_cqe_get_data(cqe));
            if (event)
            {
                switch (event->type)
                {
                case UringOpType::Accept: {
                    auto *user_data = static_cast<AcceptOperationData *>(event->data);
                    assert(user_data != nullptr);
                    assert(user_data->completion_handler != nullptr);
                    user_data->completion_handler(user_data->op_state_ptr, cqe->res);
                    break;
                }
                case UringOpType::Read: {
                    auto *user_data = static_cast<ReadOperationData *>(event->data);
                    assert(user_data != nullptr);
                    assert(user_data->completion_handler != nullptr);
                    user_data->completion_handler(user_data->op_state_ptr, static_cast<ssize_t>(cqe->res));
                    break;
                }
                default:
                    break;
                }
            }
            io_uring_cqe_seen(&m_ring, cqe);
        }
    }

    ~UringContext() noexcept
    {
        io_uring_queue_exit(&m_ring);
    }

    struct io_uring *ring() noexcept
    {
        return &m_ring;
    }

  private:
    struct io_uring m_ring{};
};

struct AcceptSender
{
    Server &m_server;
    UringContext &m_uring_ctx;

    AcceptSender(Server &server, UringContext &uring_ctx) : m_server(server), m_uring_ctx(uring_ctx)
    {
    }

    using sender_concept = stdexec::sender_t;
    using completion_signatures =
        stdexec::completion_signatures<stdexec::set_value_t(FileDescriptor),     // on success, return client fd
                                       stdexec::set_error_t(std::exception_ptr), // on error, return exception
                                       stdexec::set_stopped_t()                  // on stopped
                                       >;

    template <stdexec::receiver Receiver> struct operation_state
    {
        using operation_state_t = operation_state<Receiver>;

        Server &m_server;
        UringContext &m_uring_ctx;
        Receiver m_receiver;
        AcceptOperationData m_completion_data{};
        Event m_event{UringOpType::Accept, nullptr};

        static void complete(void *type_erased_op_state_ptr, int client_fd) noexcept
        {
            auto *op_state = static_cast<operation_state_t *>(type_erased_op_state_ptr);
            if (client_fd >= 0)
            {
                stdexec::set_value(std::move(op_state->m_receiver), FileDescriptor(client_fd));
            }
            else
            {
                stdexec::set_error(std::move(op_state->m_receiver),
                                   std::make_exception_ptr(std::runtime_error("Accept failed")));
            }
        }

        operation_state(Server &server, UringContext &uring_ctx, Receiver &&receiver)
            : m_server(server), m_uring_ctx(uring_ctx), m_receiver(std::move(receiver))
        {
            m_completion_data.op_state_ptr = this;
            m_completion_data.completion_handler = &operation_state_t::complete;
            m_event.type = UringOpType::Accept;
            m_event.data = &m_completion_data;
        }

        void start() noexcept
        {
            // Enqueue accept operation using io_uring
            io_uring_sqe *const sqe = io_uring_get_sqe(m_uring_ctx.ring());
            if (!sqe)
            {
                stdexec::set_error(std::move(m_receiver),
                                   std::make_exception_ptr(std::runtime_error("Failed to get SQE")));
                return;
            }
            io_uring_prep_accept(sqe, m_server.server_fd.get(), nullptr, nullptr, 0);
            // store Event* so run() can dispatch by type
            io_uring_sqe_set_data(sqe, &this->m_event);
            int ret = io_uring_submit(m_uring_ctx.ring());
            if (ret < 0)
            {
                stdexec::set_error(std::move(m_receiver),
                                   std::make_exception_ptr(std::runtime_error("io_uring_submit failed")));
                return;
            }
        }
    };

    template <class Receiver> friend auto tag_invoke(stdexec::connect_t, AcceptSender self, Receiver r) noexcept
    {
        return typename AcceptSender::template operation_state<Receiver>(self.m_server, self.m_uring_ctx, std::move(r));
    }
};

inline auto async_accept(Server &server, UringContext &uring_ctx) -> AcceptSender
{
    return AcceptSender(server, uring_ctx);
}

struct ReadSender
{
    RawFileDescriptor m_fd;
    UringContext &m_uring_ctx;
    std::span<std::byte> m_buffer;

    using sender_concept = stdexec::sender_t;
    using completion_signatures =
        stdexec::completion_signatures<stdexec::set_value_t(size_t),             // on success, return bytes read
                                       stdexec::set_error_t(std::exception_ptr), // on error, return exception
                                       stdexec::set_stopped_t()                  // on stopped
                                       >;

    template <stdexec::receiver Receiver> struct operation_state
    {
        using operation_state_t = operation_state<Receiver>;

        RawFileDescriptor m_fd;
        UringContext &m_uring_ctx;
        std::span<std::byte> m_buffer;
        Receiver m_receiver;
        Event m_event{UringOpType::Read, nullptr};
        ReadOperationData m_completion_data;

        static void complete(void *type_erased_op_state_ptr, ssize_t bytes_read) noexcept
        {
            auto *op_state = static_cast<operation_state_t *>(type_erased_op_state_ptr);
            if (bytes_read >= 0)
            {
                stdexec::set_value(std::move(op_state->m_receiver), static_cast<size_t>(bytes_read));
            }
            else
            {
                stdexec::set_error(std::move(op_state->m_receiver),
                                   std::make_exception_ptr(std::runtime_error("Read failed")));
            }
        }

        operation_state(RawFileDescriptor fd, UringContext &uring_ctx, std::span<std::byte> buffer, Receiver &&receiver)
            : m_fd(fd), m_uring_ctx(uring_ctx), m_buffer(buffer), m_receiver(std::move(receiver))
        {
            m_completion_data.op_state_ptr = this;
            m_completion_data.completion_handler = &operation_state_t::complete;
            m_event.type = UringOpType::Read;
            m_event.data = &m_completion_data;
        }

        void start() noexcept
        {
            // Enqueue read operation using io_uring
            io_uring_sqe *const sqe = io_uring_get_sqe(m_uring_ctx.ring());
            if (!sqe)
            {
                stdexec::set_error(std::move(m_receiver),
                                   std::make_exception_ptr(std::runtime_error("Failed to get SQE")));
                return;
            }
            io_uring_prep_read(sqe, m_fd, m_buffer.data(), m_buffer.size(), 0);
            // store Event* so run() can dispatch by type
            io_uring_sqe_set_data(sqe, &this->m_event);
            int ret = io_uring_submit(m_uring_ctx.ring());
            if (ret < 0)
            {
                stdexec::set_error(std::move(m_receiver),
                                   std::make_exception_ptr(std::runtime_error("io_uring_submit failed")));
                return;
            }
        }
    };

    template <stdexec::receiver_of<completion_signatures> Receiver>
    friend auto tag_invoke(stdexec::connect_t, ReadSender self, Receiver r) noexcept
    {
        return typename ReadSender::template operation_state<Receiver>(self.m_fd, self.m_uring_ctx, self.m_buffer,
                                                                       std::move(r));
    }
};

inline auto async_read(RawFileDescriptor fd, UringContext &uring_ctx, std::span<std::byte> buffer) -> ReadSender
{
    return ReadSender{std::move(fd), uring_ctx, buffer};
}

struct WriteSender
{
    RawFileDescriptor m_fd;
    UringContext &m_uring_ctx;
    std::span<const std::byte> m_buffer;

    using sender_concept = stdexec::sender_t;
    using completion_signatures =
        stdexec::completion_signatures<stdexec::set_value_t(size_t),             // on success, return bytes written
                                       stdexec::set_error_t(std::exception_ptr), // on error, return exception
                                       stdexec::set_stopped_t()                  // on stopped
                                       >;

    template <stdexec::receiver Receiver> struct operation_state
    {
        using operation_state_t = operation_state<Receiver>;

        RawFileDescriptor m_fd;
        UringContext &m_uring_ctx;
        std::span<const std::byte> m_buffer;
        Receiver m_receiver;
        Event m_event{UringOpType::Write, nullptr};
        WriteOperationData m_completion_data;

        static void complete(void *type_erased_op_state_ptr, ssize_t bytes_written) noexcept
        {
            auto *op_state = static_cast<operation_state_t *>(type_erased_op_state_ptr);
            if (bytes_written >= 0)
            {
                stdexec::set_value(std::move(op_state->m_receiver), static_cast<size_t>(bytes_written));
            }
            else
            {
                stdexec::set_error(std::move(op_state->m_receiver),
                                   std::make_exception_ptr(std::runtime_error("Write failed")));
            }
        }

        operation_state(RawFileDescriptor fd, UringContext &uring_ctx, std::span<const std::byte> buffer,
                        Receiver &&receiver)
            : m_fd(fd), m_uring_ctx(uring_ctx), m_buffer(buffer), m_receiver(std::move(receiver))
        {
            m_completion_data.op_state_ptr = this;
            m_completion_data.completion_handler = &operation_state_t::complete;
            m_event.type = UringOpType::Write;
            m_event.data = &m_completion_data;
        }

        void start() noexcept
        {
            // Enqueue write operation using io_uring
            io_uring_sqe *const sqe = io_uring_get_sqe(m_uring_ctx.ring());
            if (!sqe)
            {
                stdexec::set_error(std::move(m_receiver),
                                   std::make_exception_ptr(std::runtime_error("Failed to get SQE")));
                return;
            }
            io_uring_prep_write(sqe, m_fd, m_buffer.data(), m_buffer.size(), 0);
            // store Event* so run() can dispatch by type
            io_uring_sqe_set_data(sqe, &this->m_event);
            int ret = io_uring_submit(m_uring_ctx.ring());
            if (ret < 0)
            {
                stdexec::set_error(std::move(m_receiver),
                                   std::make_exception_ptr(std::runtime_error("io_uring_submit failed")));
                return;
            }
        }
    };

    template <stdexec::receiver_of<completion_signatures> Receiver>
    friend auto tag_invoke(stdexec::connect_t, WriteSender self, Receiver r) noexcept
    {
        return typename WriteSender ::template operation_state<Receiver>(self.m_fd, self.m_uring_ctx, self.m_buffer,
                                                                         std::move(r));
    }
};

inline auto async_write(RawFileDescriptor fd, UringContext &uring_ctx, std::span<const std ::byte> buffer)
    -> WriteSender
{
    return WriteSender{std ::move(fd), uring_ctx, buffer};
}