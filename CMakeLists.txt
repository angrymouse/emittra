cmake_minimum_required(VERSION 3.10)
project(Emittra VERSION 0.1.0)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Enable compile_commands.json generation
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Set optimization flags for different compilers
if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /Ob2 /DNDEBUG")
    set(CMAKE_C_FLAGS_RELEASE "/O2 /Ob2 /DNDEBUG")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
    set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")
endif()

# Set the build type to Release for maximum optimizations
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Add the library
add_library(emittra
    src/emittra.cpp
)

# Set the library properties
set_target_properties(emittra PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
)

# Include directories
target_include_directories(emittra
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/concurrentqueue
)

# Ensure compatibility with Clang
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(emittra PRIVATE -Wno-deprecated-declarations)
endif()

# Export all symbols for MSVC
if(MSVC)
    set_target_properties(emittra PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

# Add benchmark executable
add_executable(emittra_benchmark src/benchmark.cpp)

# Link the benchmark with the Emittra library
target_link_libraries(emittra_benchmark PRIVATE emittra)

# Include the Emittra headers for the benchmark
target_include_directories(emittra_benchmark PRIVATE ${CMAKE_SOURCE_DIR}/include)

# Set C++17 standard for the benchmark
set_target_properties(emittra_benchmark PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)