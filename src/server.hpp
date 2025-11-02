#pragma once

#include "file_descriptor.hpp"
#include <cstdint>

struct Server
{
    Server(uint16_t port);
    FileDescriptor server_fd;
};
