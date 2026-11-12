cmake_minimum_required(VERSION 3.20)
project(AetherSoftwareSmoke LANGUAGES CXX)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../native/src/rendering/software" "${CMAKE_CURRENT_BINARY_DIR}/raster")
add_executable(aether_software_smoke main.cpp)
target_link_libraries(aether_software_smoke PRIVATE aether_software_raster)
add_executable(aether_software_raster_test ../../native/tests/test_software_renderer.cpp)
target_link_libraries(aether_software_raster_test PRIVATE aether_software_raster)
# No automatic test execution or workflows.
