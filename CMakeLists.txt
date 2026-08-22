cmake_minimum_required(VERSION 3.20)
project(LinuxLive VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTING "Build tests" ON)

add_subdirectory(cpp/lili-protocol)
add_subdirectory(cpp/lili-relay)
add_subdirectory(cpp/lili-sdk)
add_subdirectory(cpp/lili-core)
add_subdirectory(cpp/lili-gui)
add_subdirectory(cpp/lili-cli)

if(BUILD_TESTING)
    enable_testing()
endif()
