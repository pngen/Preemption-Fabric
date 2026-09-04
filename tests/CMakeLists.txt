# Test executables. Each is standalone, returns nonzero on failure, and is
# registered with CTest. No test timeouts are ever set anywhere.
set(PF_TESTS
  test_identity
  test_lifecycle
  test_persistence
  test_protocol
  test_runtime
  test_property
  test_concurrency
  test_adversarial
)

foreach(t IN LISTS PF_TESTS)
  add_executable(${t} ${t}.cpp)
  target_link_libraries(${t} PRIVATE PreemptionFabric::PreemptionFabric)
  target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
  add_test(NAME ${t} COMMAND ${t})
endforeach()

# Real multiprocess proof (needs the coordinator/worker executables).
add_executable(test_multiprocess test_multiprocess.cpp)
target_link_libraries(test_multiprocess PRIVATE PreemptionFabric::PreemptionFabric)
target_include_directories(test_multiprocess PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
add_test(NAME test_multiprocess
  COMMAND test_multiprocess $<TARGET_FILE:pf_coordinator> $<TARGET_FILE:pf_worker>)
