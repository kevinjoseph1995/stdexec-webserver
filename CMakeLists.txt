cmake_minimum_required(VERSION 3.20)
project(webserver CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/CPM.cmake)
CPMAddPackage(
  NAME stdexec
  GITHUB_REPOSITORY NVIDIA/stdexec
  GIT_TAG main 
  OPTIONS "STDEXEC_BUILD_TESTS OFF" "STDEXEC_BUILD_EXAMPLES OFF"
)

FetchContent_Declare(llhttp
  URL "https://github.com/nodejs/llhttp/archive/refs/tags/release/v9.3.0.tar.gz")

FetchContent_MakeAvailable(llhttp)


find_path(liburing_INCLUDE_DIR
  NAMES liburing.h
  PATHS ${PC_liburing_INCLUDE_DIRS}
)

find_library(liburing_LIBRARY
  NAMES uring
  PATHS ${PC_liburing_LIBRARY_DIRS}
)

message(STATUS "liburing include directory: ${liburing_INCLUDE_DIR}")
message(STATUS "liburing library: ${liburing_LIBRARY}")

list(APPEND SOURCE_FILES
    src/main.cpp    
    src/server.cpp    
)
add_executable(webserver ${SOURCE_FILES})
target_compile_options(webserver PRIVATE -Wall -Wextra -pedantic -Werror)
target_link_libraries(webserver STDEXEC::stdexec ${liburing_LIBRARY} llhttp_shared)
target_include_directories(webserver PRIVATE ${liburing_INCLUDE_DIR})

# Treat stdexec headers as system to avoid third-party warnings-as-errors
target_include_directories(webserver SYSTEM PRIVATE
  $<TARGET_PROPERTY:STDEXEC::stdexec,INTERFACE_INCLUDE_DIRECTORIES>
)