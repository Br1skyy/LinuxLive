cmake_minimum_required(VERSION 3.20)
project(lili-protocol VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

find_package(OpenSSL REQUIRED)

add_library(lili-protocol
    src/event.cpp
    src/signing.cpp
    src/nip58.cpp
    src/subnode.cpp
)

target_include_directories(lili-protocol PUBLIC include)
target_link_libraries(lili-protocol PUBLIC OpenSSL::Crypto nlohmann_json::nlohmann_json)

target_compile_options(lili-protocol PRIVATE -Wall -Wextra -Wpedantic -Werror)
set_target_properties(lili-protocol PROPERTIES POSITION_INDEPENDENT_CODE ON)

install(TARGETS lili-protocol
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin)

install(DIRECTORY include/ DESTINATION include)

if(BUILD_TESTING)
    enable_testing()
    add_executable(lili-protocol-test-event tests/test_event.cpp)
    target_link_libraries(lili-protocol-test-event PRIVATE lili-protocol)
    add_test(NAME lili-protocol-test-event COMMAND lili-protocol-test-event)

    add_executable(lili-protocol-test-signing tests/test_signing.cpp)
    target_link_libraries(lili-protocol-test-signing PRIVATE lili-protocol)
    add_test(NAME lili-protocol-test-signing COMMAND lili-protocol-test-signing)
endif()
