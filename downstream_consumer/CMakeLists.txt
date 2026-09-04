cmake_minimum_required(VERSION 3.22)
project(PreemptionFabricConsumer LANGUAGES CXX)

# Independent downstream consumer validating the installed package.
find_package(PreemptionFabric CONFIG REQUIRED)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE PreemptionFabric::PreemptionFabric)
set_target_properties(consumer PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
