# Benchmarks (not run by CTest).
add_executable(pf_benchmark ../benchmarks/benchmark.cpp)
target_link_libraries(pf_benchmark PRIVATE PreemptionFabric::PreemptionFabric)

# Orchestrated full-preemption phase benchmark (not run by CTest).
add_executable(pf_benchmark_phases ../benchmarks/benchmark_phases.cpp)
target_link_libraries(pf_benchmark_phases PRIVATE PreemptionFabric::PreemptionFabric)
