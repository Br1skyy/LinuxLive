cmake_minimum_required(VERSION 3.20)
project(lili-relay VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenSSL REQUIRED)

add_library(lili-relay-core
    src/relay_server.cpp
    src/client_session.cpp
    src/message_handler.cpp
)
target_include_directories(lili-relay-core PUBLIC include)
target_link_libraries(lili-relay-core PUBLIC lili-protocol OpenSSL::Crypto)
target_compile_options(lili-relay-core PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(lili-relay src/main.cpp)
target_include_directories(lili-relay PRIVATE include)
target_link_libraries(lili-relay PRIVATE lili-relay-core)
target_compile_options(lili-relay PRIVATE -Wall -Wextra -Wpedantic -Werror)

install(TARGETS lili-relay RUNTIME DESTINATION bin)

if(BUILD_TESTING)
    enable_testing()
    add_executable(lili-relay-tests tests/test_relay_server.cpp)
    target_include_directories(lili-relay-tests PRIVATE include)
    target_link_libraries(lili-relay-tests PRIVATE lili-relay-core)
    add_test(NAME lili-relay-tests COMMAND lili-relay-tests)
endif()
