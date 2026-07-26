cmake_minimum_required(VERSION 3.16)
project(AetherUI VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

option(AETHERUI_BUILD_TESTS "Build the AetherUI native test executables" "${BUILD_TESTING}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(aether_core SHARED
    src/abi/aether_abi.cpp
    src/dom/node.cpp
    src/dom/element_node.cpp
    src/dom/document_node.cpp
    src/dom/html_parser.cpp
    src/css/css_length.cpp
    src/css/color_rgba.cpp
    src/css/computed_style.cpp
    src/css/css_parser.cpp
    src/css/selector.cpp
    src/css/style_resolver.cpp
    src/primitives/geometry.cpp
    src/layout/layout_box.cpp
    src/layout/layout_engine.cpp
    src/rendering/display_list.cpp
    src/rendering/display_list_builder.cpp
    src/style_runtime/style_values.cpp
    src/style_runtime/style_computed.cpp
    src/style_runtime/style_property_registry.cpp
    src/style_runtime/builtin_properties.cpp
    src/style_runtime/cascade.cpp
    src/style_runtime/selector_matcher.cpp
    src/style_runtime/selector_parser.cpp
    src/style_runtime/rule_index.cpp
    src/style_runtime/variable_resolver.cpp
    src/style_runtime/media_condition.cpp
    src/style_runtime/style_diff.cpp
    src/style_runtime/style_source_compiler.cpp
    src/style_runtime/style_runtime_core.cpp
)

add_library(AetherUI::Core ALIAS aether_core)

target_include_directories(aether_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_definitions(aether_core PRIVATE AETHER_EXPORTS)

set_target_properties(aether_core PROPERTIES
    EXPORT_NAME Core
    VERSION "${PROJECT_VERSION}"
    SOVERSION "${PROJECT_VERSION_MAJOR}"
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

if(MSVC)
    target_compile_options(aether_core PRIVATE /W4 /utf-8)
    target_compile_definitions(aether_core PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
    target_compile_options(aether_core PRIVATE -Wall -Wextra -pedantic)
    target_compile_options(aether_core PRIVATE -ffp-contract=off)
endif()

if(AETHERUI_BUILD_TESTS)
    enable_testing()

    add_executable(aether_smoke_test tests/test_smoke.cpp)
    target_link_libraries(aether_smoke_test PRIVATE AetherUI::Core)
    add_test(NAME aether.smoke COMMAND aether_smoke_test)

    add_executable(aether_abi_c_smoke_test tests/test_abi_c.c)
    target_link_libraries(aether_abi_c_smoke_test PRIVATE AetherUI::Core)
    set_target_properties(aether_abi_c_smoke_test PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
    )
    add_test(NAME aether.abi-c COMMAND aether_abi_c_smoke_test)
endif()

set(AETHERUI_INSTALL_CMAKEDIR
    "${CMAKE_INSTALL_LIBDIR}/cmake/AetherUI"
    CACHE STRING "Install location for AetherUI CMake package files"
)

install(
    TARGETS aether_core
    EXPORT AetherUITargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)

install(
    DIRECTORY include/
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING PATTERN "*.h"
)

configure_package_config_file(
    cmake/AetherUIConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/AetherUIConfig.cmake"
    INSTALL_DESTINATION "${AETHERUI_INSTALL_CMAKEDIR}"
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/AetherUIConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMinorVersion
)

install(
    EXPORT AetherUITargets
    FILE AetherUITargets.cmake
    NAMESPACE AetherUI::
    DESTINATION "${AETHERUI_INSTALL_CMAKEDIR}"
)

install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/AetherUIConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/AetherUIConfigVersion.cmake"
    DESTINATION "${AETHERUI_INSTALL_CMAKEDIR}"
)

file(
    RELATIVE_PATH AETHERUI_PKGCONFIG_PREFIX_RELATIVE
    "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}/pkgconfig"
    "${CMAKE_CURRENT_BINARY_DIR}"
)

configure_file(
    cmake/aetherui-core.pc.in
    "${CMAKE_CURRENT_BINARY_DIR}/aetherui-core.pc"
    @ONLY
)

install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/aetherui-core.pc"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig"
)
