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

include(FetchContent)
FetchContent_Declare(
    picohttpparser
    GIT_REPOSITORY https://github.com/h2o/picohttpparser.git
    GIT_TAG master
)
FetchContent_MakeAvailable(picohttpparser)
add_library(picohttpparser SHARED
    ${picohttpparser_SOURCE_DIR}/picohttpparser.c
)
target_include_directories(picohttpparser
    PUBLIC ${picohttpparser_SOURCE_DIR}
)

list(APPEND SOURCE_FILES
    src/main.cpp    
    src/server.cpp    
)
add_executable(webserver ${SOURCE_FILES})
target_compile_options(webserver PRIVATE -Wall -Wextra -pedantic -Werror)
target_link_libraries(webserver STDEXEC::stdexec ${liburing_LIBRARY} picohttpparser)
target_include_directories(webserver PRIVATE ${liburing_INCLUDE_DIR})

# Treat stdexec headers as system to avoid third-party warnings-as-errors
target_include_directories(webserver SYSTEM PRIVATE
  $<TARGET_PROPERTY:STDEXEC::stdexec,INTERFACE_INCLUDE_DIRECTORIES>
)