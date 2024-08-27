cmake_minimum_required(VERSION 3.10)

project(Emittra VERSION 1.0)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Enable compile_commands.json generation
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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