cmake_minimum_required(VERSION 3.20)
project(LoadBalancerProject CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_link_options(-static-libstdc++ -static-libgcc)

add_library(common_net INTERFACE)
target_include_directories(common_net INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/common)

add_executable(load_balancer src/proxy_balancer/main.cpp)
target_link_libraries(load_balancer PRIVATE common_net)

add_executable(backend_server src/backend/main.cpp)
target_link_libraries(backend_server PRIVATE common_net)
