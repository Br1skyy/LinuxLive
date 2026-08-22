cmake_minimum_required(VERSION 3.20)
project(lili-sdk VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenSSL REQUIRED)

add_library(lili-sdk SHARED
    src/sdk.cpp
)

target_include_directories(lili-sdk PUBLIC include)
target_link_libraries(lili-sdk PRIVATE
    lili-protocol
    OpenSSL::Crypto
)

target_compile_options(lili-sdk PRIVATE -Wall -Wextra -Wpedantic -Werror)

set_target_properties(lili-sdk PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
)

install(TARGETS lili-sdk
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin)

install(DIRECTORY include/ DESTINATION include)

if(BUILD_TESTING)
    enable_testing()
    add_executable(lili-sdk-tests tests/test_sdk.cpp)
    target_link_libraries(lili-sdk-tests PRIVATE lili-sdk)
    add_test(NAME lili-sdk-tests COMMAND lili-sdk-tests)
endif()
