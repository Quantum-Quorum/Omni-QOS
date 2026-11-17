cmake_minimum_required(VERSION 3.16)
project(OmniQCore VERSION 2.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(OpenMP REQUIRED)
find_package(OpenSSL REQUIRED)

add_compile_options(-O3 -Wall -Wextra -pthread)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    add_compile_options(-mavx2 -mfma)
endif()

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(BIN_OUT "${REPO_ROOT}/bin")
set(LIB_OUT "${REPO_ROOT}/lib")

file(MAKE_DIRECTORY "${BIN_OUT}")
file(MAKE_DIRECTORY "${LIB_OUT}")

include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include")
include_directories("${REPO_ROOT}/kernel/include/uapi")

# 1. Shared library: libomniq_core.so (OpenMP + AVX2/FMA + CHP Tableau + RT Jitter)
add_library(omniq_core SHARED
    src/core_engine.cpp
)
target_link_libraries(omniq_core PUBLIC OpenMP::OpenMP_CXX PRIVATE pthread rt m)
set_target_properties(omniq_core PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${LIB_OUT}"
)

# 2. Standalone CLI & RT Benchmark Daemon: bin/omniq-core
add_executable(omniq-core
    src/main.cpp
)
target_link_libraries(omniq-core PRIVATE omniq_core pthread rt m)
set_target_properties(omniq-core PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${BIN_OUT}"
    BUILD_RPATH "\$ORIGIN/../lib:/opt/omniq/lib:/usr/local/lib"
    INSTALL_RPATH "\$ORIGIN/../lib:/opt/omniq/lib:/usr/local/lib"
)

# 3. Standalone Hardware RT Scheduling Wakeup Latency Profiler: bin/omniq-rt-bench
add_executable(omniq-rt-bench
    src/rt_bench.cpp
)
target_link_libraries(omniq-rt-bench PRIVATE omniq_core pthread rt m)
set_target_properties(omniq-rt-bench PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${BIN_OUT}"
    BUILD_RPATH "\$ORIGIN/../lib:/opt/omniq/lib:/usr/local/lib"
    INSTALL_RPATH "\$ORIGIN/../lib:/opt/omniq/lib:/usr/local/lib"
)

# 4. Shared library: libomniq_secret.so (OpenSSL libcrypto + FIPS 203 ML-KEM-768)
add_library(omniq_secret SHARED
    src/quantum_secret_api.cpp
)
target_link_libraries(omniq_secret PRIVATE OpenSSL::Crypto pthread rt m)
set_target_properties(omniq_secret PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${LIB_OUT}"
)

# 5. Standalone Multi-Modal Quantum Secret API Daemon: bin/omniq-secret-api
add_executable(omniq-secret-api
    src/quantum_secret_api.cpp
)
target_compile_definitions(omniq-secret-api PRIVATE OMNIQ_SECRET_STANDALONE=1)
target_link_libraries(omniq-secret-api PRIVATE OpenSSL::Crypto pthread rt m)
set_target_properties(omniq-secret-api PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${BIN_OUT}"
)
