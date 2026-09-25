cmake_minimum_required(VERSION 3.12)
project(hotrod_quickstart)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find the Hot Rod client library
# Assumes the library was built in ../../build
set(HOTROD_CLIENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(HOTROD_INCLUDE_DIR "${HOTROD_CLIENT_DIR}/include")
set(HOTROD_LIB_DIR "${HOTROD_CLIENT_DIR}/build")

# Check if library exists
if(NOT EXISTS "${HOTROD_LIB_DIR}/libhotrod-client.a")
    message(FATAL_ERROR
        "Hot Rod client library not found at ${HOTROD_LIB_DIR}/libhotrod-client.a\n"
        "Please build the library first:\n"
        "  cd ${HOTROD_CLIENT_DIR}/build\n"
        "  cmake .. && make")
endif()

# Include directories
include_directories(${HOTROD_INCLUDE_DIR})

# Create executables
add_executable(quickstart quickstart.cpp)

# User-decided retry example (Step 11b)
add_executable(retry retry.cpp)

# Link against Hot Rod client library
target_link_libraries(quickstart
    ${HOTROD_LIB_DIR}/libhotrod-client.a
    pthread
)

target_link_libraries(retry
    ${HOTROD_LIB_DIR}/libhotrod-client.a
    pthread
)

# Install targets (optional)
install(TARGETS quickstart retry DESTINATION bin)

message(STATUS "Hot Rod Quickstart Example")
message(STATUS "  Include dir: ${HOTROD_INCLUDE_DIR}")
message(STATUS "  Library: ${HOTROD_LIB_DIR}/libhotrod-client.a")
