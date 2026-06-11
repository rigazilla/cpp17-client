cmake_minimum_required(VERSION 3.15)
project(hotrod-client-cpp VERSION 0.1.0 LANGUAGES CXX)

# C++17 required
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler warnings
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Werror -pedantic)
endif()

# Find dependencies
find_package(OpenSSL REQUIRED)

# Library source files
set(SOURCES
    src/codec/Codec.cpp
    src/codec/HeaderCodec.cpp
    src/auth/SCRAM.cpp
    src/topology/TopologyInfo.cpp
    src/hash/MurmurHash3.cpp
    src/hash/ConsistentHash.cpp
    src/transport/Connection.cpp
)

# Create library
add_library(hotrod-client STATIC ${SOURCES})

target_include_directories(hotrod-client
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(hotrod-client
    PUBLIC
        OpenSSL::SSL
        OpenSSL::Crypto
)

# Platform-specific libraries
if(WIN32)
    target_link_libraries(hotrod-client PUBLIC ws2_32)
endif()

# Testing
enable_testing()
find_package(GTest)

if(GTest_FOUND)
    # Unit tests
    add_executable(unit_tests
        tests/unit/CodecTest.cpp
        tests/unit/HeaderCodecTest.cpp
        tests/unit/SCRAMTest.cpp
        tests/unit/TopologyInfoTest.cpp
        tests/unit/MurmurHash3Test.cpp
        tests/unit/ConsistentHashTest.cpp
    )

    target_link_libraries(unit_tests
        PRIVATE
            hotrod-client
            GTest::gtest
            GTest::gtest_main
    )

    add_test(NAME UnitTests COMMAND unit_tests)

    message(STATUS "Google Test found - unit tests enabled")
else()
    message(WARNING "Google Test not found - tests disabled")
endif()

# Installation
install(TARGETS hotrod-client
    EXPORT hotrod-client-targets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/ DESTINATION include)
