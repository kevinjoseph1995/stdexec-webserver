#pragma once

#include <string>
#include <string_view>

class Error
{
  public:
    explicit Error(std::string_view message) : m_message(message)
    {
    }

    const std::string &message() const
    {
        return m_message;
    }

  private:
    std::string m_message;
};