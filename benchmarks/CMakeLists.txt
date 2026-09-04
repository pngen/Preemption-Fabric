# Benchmarks (not run by CTest).
add_executable(pf_benchmark ../benchmarks/benchmark.cpp)
target_link_libraries(pf_benchmark PRIVATE PreemptionFabric::PreemptionFabric)
