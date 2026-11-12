cmake_minimum_required(VERSION 3.20)
project(AetherNativePreparation LANGUAGES CXX)
add_library(aether_native_preparation STATIC
 aether_command_source.cpp native_command_source.cpp paint_snapshot.cpp preparation_metrics.cpp)
target_compile_features(aether_native_preparation PUBLIC cxx_std_20)
set_target_properties(aether_native_preparation PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(aether_native_preparation PUBLIC
 "${CMAKE_CURRENT_SOURCE_DIR}/include"
 "${CMAKE_CURRENT_SOURCE_DIR}/../../../include")
option(AETHER_NATIVE_PREPARATION_BUILD_TESTS "Build pure native preparation smoke test (no automatic execution)" OFF)
if(AETHER_NATIVE_PREPARATION_BUILD_TESTS)
 add_executable(aether_native_preparation_test
  "${CMAKE_CURRENT_SOURCE_DIR}/../../../tests/test_native_preparation.cpp")
 target_link_libraries(aether_native_preparation_test PRIVATE aether_native_preparation)
 set_target_properties(aether_native_preparation_test PROPERTIES CXX_EXTENSIONS OFF)
endif()
