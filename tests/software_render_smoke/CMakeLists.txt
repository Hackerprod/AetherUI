cmake_minimum_required(VERSION 3.20)
project(AetherSoftwareSmoke LANGUAGES CXX)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../native/src/rendering/software" "${CMAKE_CURRENT_BINARY_DIR}/raster")
add_executable(aether_software_smoke main.cpp)
target_link_libraries(aether_software_smoke PRIVATE aether_software_raster)
add_executable(aether_software_raster_test ../../native/tests/test_software_renderer.cpp)
target_link_libraries(aether_software_raster_test PRIVATE aether_software_raster)
# No automatic test execution or workflows.

# This standalone emitter integration test currently uses ELF section GC.
# Other platforms need the full engine target and are not claimed validated.
if(UNIX AND NOT APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
 add_executable(aether_software_stroke_test
  ../../native/tests/test_software_stroke.cpp
  ../../native/src/rendering/border_painter.cpp
  ../../native/src/css/border_radii.cpp)
 target_link_libraries(aether_software_stroke_test PRIVATE aether_software_raster)
 target_compile_options(aether_software_stroke_test PRIVATE -ffunction-sections -fdata-sections)
 target_link_options(aether_software_stroke_test PRIVATE -Wl,--gc-sections)
endif()

add_executable(aether_software_rounded_test ../../native/tests/test_software_rounded.cpp)
target_link_libraries(aether_software_rounded_test PRIVATE aether_software_raster)
if(UNIX AND NOT APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
 add_executable(aether_software_rounded_emitter_test ../../native/tests/test_software_rounded_emitter.cpp ../../native/src/abi/aether_abi.cpp)
 target_link_libraries(aether_software_rounded_emitter_test PRIVATE aether_software_raster)
 target_compile_options(aether_software_rounded_emitter_test PRIVATE -ffunction-sections -fdata-sections)
 target_link_options(aether_software_rounded_emitter_test PRIVATE -Wl,--gc-sections)
endif()
