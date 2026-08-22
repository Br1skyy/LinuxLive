cmake_minimum_required(VERSION 3.20)
project(lili-cli VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

add_executable(lili-cli src/main.cpp)
target_include_directories(lili-cli PRIVATE include)
target_link_libraries(lili-cli PRIVATE
    lili-core
    lili-relay-core
    lili-protocol
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)
target_compile_options(lili-cli PRIVATE -Wall -Wextra -Wpedantic)

install(TARGETS lili-cli RUNTIME DESTINATION bin)
