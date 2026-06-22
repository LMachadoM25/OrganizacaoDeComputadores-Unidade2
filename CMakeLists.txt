cmake_minimum_required(VERSION 3.16)

project(spin_network_systemc CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SYSTEMC REQUIRED systemc)

add_executable(spin_network
    src/main.cpp
    src/packet.cpp
    src/router.cpp
    src/network.cpp
    src/config_loader.cpp
)

target_include_directories(spin_network PRIVATE
    src
    ${SYSTEMC_INCLUDE_DIRS}
)

target_link_directories(spin_network PRIVATE
    ${SYSTEMC_LIBRARY_DIRS}
)

target_link_libraries(spin_network PRIVATE
    ${SYSTEMC_LIBRARIES}
)

target_compile_options(spin_network PRIVATE
    ${SYSTEMC_CFLAGS_OTHER}
)