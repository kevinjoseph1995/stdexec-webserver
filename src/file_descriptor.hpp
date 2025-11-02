#pragma once

#include <unistd.h>

using RawFileDescriptor = int;

class FileDescriptor
{
  public:
    FileDescriptor(RawFileDescriptor fd) : m_fd(fd)
    {
    }

    FileDescriptor() : m_fd(-1)
    {
    }

    ~FileDescriptor()
    {
        close();
    }

    FileDescriptor(const FileDescriptor &) = delete;
    FileDescriptor &operator=(const FileDescriptor &) = delete;

    /* Move constructor. Transfers ownership of the file descriptor. */
    FileDescriptor(FileDescriptor &&other) noexcept : m_fd(other.m_fd)
    {
        other.m_fd = -1;
    }

    /* Move assignment operator. Transfers ownership of the file descriptor. */
    FileDescriptor &operator=(FileDescriptor &&other) noexcept
    {
        if (this != &other)
        {
            close();           // Close existing fd
            m_fd = other.m_fd; // Transfer ownership
            other.m_fd = -1;   // Invalidate the other fd
        }
        return *this;
    }

    /* Returns the raw file descriptor. */
    RawFileDescriptor get() const
    {
        return m_fd;
    }

    /* Returns true if the file descriptor is valid. */
    bool valid() const
    {
        return m_fd >= 0;
    }

    /* Releases ownership of the file descriptor. */
    RawFileDescriptor release()
    {
        RawFileDescriptor fd = m_fd;
        m_fd = -1;
        return fd;
    }

    /* Closes the file descriptor if it's valid. */
    void close()
    {
        if (valid())
        {
            ::close(m_fd);
            m_fd = -1;
        }
    }

  private:
    RawFileDescriptor m_fd;
};
