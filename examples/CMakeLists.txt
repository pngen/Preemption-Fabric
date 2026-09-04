# Runnable examples.
add_executable(example_basic_preemption ../examples/basic_preemption.cpp)
target_link_libraries(example_basic_preemption PRIVATE PreemptionFabric::PreemptionFabric)

add_executable(example_forced_termination ../examples/forced_termination.cpp)
target_link_libraries(example_forced_termination PRIVATE PreemptionFabric::PreemptionFabric)
