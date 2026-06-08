cmake_minimum_required(VERSION 3.16)
project(ChatServer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenSSL REQUIRED)

add_executable(chat_server server.cpp)

target_link_libraries(chat_server
    OpenSSL::SSL
    OpenSSL::Crypto
    pthread
)

target_compile_options(chat_server PRIVATE -O2 -Wall -Wextra)
