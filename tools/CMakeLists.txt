# Inspection CLI tool.
add_executable(pf_inspect ../src/tools/inspect.cpp)
target_link_libraries(pf_inspect PRIVATE PreemptionFabric::PreemptionFabric)
