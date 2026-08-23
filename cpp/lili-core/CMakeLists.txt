cmake_minimum_required(VERSION 3.20)
project(lili-core VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenSSL REQUIRED)

add_library(lili-core STATIC
    src/identity.cpp
    src/persistence.cpp
    src/passive_scanner.cpp
    src/encryption.cpp
    src/relay_client.cpp
    src/ws_client.cpp
)

target_include_directories(lili-core PUBLIC include)
target_link_libraries(lili-core PUBLIC
    lili-protocol
    OpenSSL::Crypto
    OpenSSL::SSL
    pthread
)

target_compile_definitions(lili-core PRIVATE
    LILI_ACHIEVEMENTS_DIR="${CMAKE_SOURCE_DIR}/achievements"
)

target_compile_options(lili-core PRIVATE -Wall -Wextra -Wno-deprecated-declarations)

if(BUILD_TESTING)
    enable_testing()
    add_executable(lili-core-tests tests/test_identity.cpp)
    target_link_libraries(lili-core-tests PRIVATE lili-core)
    add_test(NAME lili-core-tests COMMAND lili-core-tests)
endif()
