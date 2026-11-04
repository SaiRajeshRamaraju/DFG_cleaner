cmake_minimum_required(VERSION 3.20)
project(DFG_v2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(OpenSSL REQUIRED)

add_library(dfg_net STATIC
    network/socket_io.cpp
    network/sha256.cpp
)
target_include_directories(dfg_net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dfg_net PUBLIC OpenSSL::Crypto)

add_library(dfg_storage STATIC
    server/chunk_server/chunk_store.cpp
)
target_include_directories(dfg_storage PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dfg_storage PUBLIC dfg_net)

add_executable(dfg-server
    server/main.cpp
    server/chunk_server/server.cpp
    server/include/connection_handle.cpp
)
target_link_libraries(dfg-server PRIVATE dfg_storage dfg_net pthread)

add_executable(dfg-client
    client/main.cpp
    client/connection.cpp
)
target_link_libraries(dfg-client PRIVATE dfg_net)
