#include "server.hpp"

#include <netinet/in.h>
#include <stdexcept>

Server::Server(uint16_t port)
{
    // AF_INET: IPv4, SOCK_STREAM: TCP, 0: default protocol
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        throw std::runtime_error("Failed to create socket");
    }
    server_fd = FileDescriptor(fd);

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;         // IPv4
    addr.sin_addr.s_addr = INADDR_ANY; // Bind to all interfaces
    addr.sin_port = htons(port);       // Convert port to network byte order

    // Bind the socket to the specified port
    if (bind(server_fd.get(), (sockaddr *)&addr, sizeof(addr)) < 0)
    {
        throw std::runtime_error("Failed to bind socket");
    }

    if (listen(server_fd.get(), SOMAXCONN) < 0)
    {
        throw std::runtime_error("Failed to listen on socket");
    }
}
