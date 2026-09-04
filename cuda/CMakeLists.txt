# Real RTX 5090 (sm_120) cooperative-preemption proof.
#
# The Visual Studio generator does not expose a CUDA toolset here, so the proof
# is built by invoking nvcc directly (host language C++20, library runtime /MD).
# Configure with -DPF_CUDA_NVCC=<path to nvcc> to enable it. When nvcc is
# unavailable the target is simply absent; the proof itself is fully validated
# and documented (see README "CUDA physical proof").
set(PF_CUDA_NVCC "" CACHE FILEPATH "Path to nvcc for the CUDA proof")
if(NOT PF_CUDA_NVCC)
  find_program(PF_CUDA_NVCC nvcc)
endif()

if(PF_CUDA_NVCC)
  set(PF_CUDA_ARCH 120)
  add_custom_target(pf_cuda_proof
    COMMAND ${PF_CUDA_NVCC} -O2 -std=c++20 -arch=sm_${PF_CUDA_ARCH} -ccbin ${CMAKE_CXX_COMPILER}
            -Xcompiler /MD
            -I ${CMAKE_CURRENT_SOURCE_DIR}/../include
            ${CMAKE_CURRENT_SOURCE_DIR}/cuda_proof.cu
            $<TARGET_FILE:preemption_fabric> -lcudart
            -o ${CMAKE_CURRENT_BINARY_DIR}/pf_cuda_proof.exe
    DEPENDS preemption_fabric
    COMMENT "Building Preemption Fabric CUDA proof (sm_${PF_CUDA_ARCH})")
endif()
