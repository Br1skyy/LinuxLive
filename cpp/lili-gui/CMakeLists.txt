cmake_minimum_required(VERSION 3.20)
project(lili-gui VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED gtk4)
find_package(OpenSSL REQUIRED)

add_executable(lili-gui
    src/main.cpp
    src/app.cpp
)

target_include_directories(lili-gui PRIVATE include ${GTK4_INCLUDE_DIRS})
target_link_directories(lili-gui PRIVATE ${GTK4_LIBRARY_DIRS})
target_link_libraries(lili-gui PRIVATE
    lili-protocol
    lili-sdk
    lili-core
    ${GTK4_LIBRARIES}
    OpenSSL::Crypto
    OpenSSL::SSL
)

target_compile_options(lili-gui PRIVATE -Wall -Wextra ${GTK4_CFLAGS_OTHER} -Wno-deprecated-declarations)
