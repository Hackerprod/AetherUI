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
    src/images/image_decode.cpp
    src/css/css_length.cpp
    src/css/color_rgba.cpp
    src/css/computed_style.cpp
    src/css/border_radii.cpp
    src/css/background_layers.cpp
    src/css/backdrop_filters.cpp
    src/css/transforms.cpp
    src/css/linear_gradient.cpp
    src/css/text_transform.cpp
    src/css/css_parser.cpp
    src/css/selector.cpp
    src/css/style_resolver.cpp
    src/primitives/geometry.cpp
    src/layout/layout_box.cpp
    src/layout/box_model.cpp
    src/layout/form_controls.cpp
    src/layout/inline_text.cpp
    src/layout/flex_resolver.cpp
    src/layout/intrinsic_sizing.cpp
    src/layout/margin_adjacency.cpp
    src/layout/text_justify.cpp
    src/layout/table_layout.cpp
    src/layout/scrollbar_geometry.cpp
    src/layout/layout_engine.cpp
    src/layout/layout_flex.cpp
    src/layout/layout_positioned.cpp
    src/layout/layout_scroll.cpp
    src/layout/layout_table.cpp
    src/rendering/display_list.cpp
    src/rendering/display_list_builder.cpp
    src/rendering/background_image_geometry.cpp
    src/rendering/transform_matrix.cpp
    src/rendering/border_painter.cpp
    src/rendering/render_plan.cpp
    src/rendering/stacking.cpp
    src/animations/transition_runtime.cpp
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
    src/style_runtime/ua_stylesheet.cpp
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

    add_executable(aether_border_radii_test tests/test_border_radii.cpp src/css/border_radii.cpp src/rendering/border_painter.cpp)
    target_link_libraries(aether_border_radii_test PRIVATE AetherUI::Core)
    add_test(NAME aether.border-radii COMMAND aether_border_radii_test)

    add_executable(aether_box_model_test tests/test_box_model.cpp src/layout/box_model.cpp)
    target_link_libraries(aether_box_model_test PRIVATE AetherUI::Core)
    add_test(NAME aether.box-model COMMAND aether_box_model_test)

    add_executable(aether_css_calc_test tests/test_css_calc.cpp)
    target_link_libraries(aether_css_calc_test PRIVATE AetherUI::Core)
    add_test(NAME aether.css-calc COMMAND aether_css_calc_test)

    add_executable(aether_flex_resolver_test tests/test_flex_resolver.cpp src/layout/flex_resolver.cpp)
    target_link_libraries(aether_flex_resolver_test PRIVATE AetherUI::Core)
    add_test(NAME aether.flex-resolver COMMAND aether_flex_resolver_test)

    add_executable(aether_flex_fractional_test tests/test_flex_fractional.cpp)
    target_link_libraries(aether_flex_fractional_test PRIVATE AetherUI::Core)
    add_test(NAME aether.flex-fractional COMMAND aether_flex_fractional_test)

    add_executable(aether_intrinsic_sizing_test tests/test_intrinsic_sizing.cpp src/layout/intrinsic_sizing.cpp)
    target_include_directories(aether_intrinsic_sizing_test PRIVATE src/layout)
    target_link_libraries(aether_intrinsic_sizing_test PRIVATE AetherUI::Core)
    add_test(NAME aether.intrinsic-sizing COMMAND aether_intrinsic_sizing_test)

    add_executable(aether_inline_text_test tests/test_inline_text.cpp src/layout/inline_text.cpp)
    target_include_directories(aether_inline_text_test PRIVATE src/layout)
    target_link_libraries(aether_inline_text_test PRIVATE AetherUI::Core)
    add_test(NAME aether.inline-text COMMAND aether_inline_text_test)

    add_executable(aether_margin_adjacency_test tests/test_margin_adjacency.cpp src/layout/margin_adjacency.cpp)
    add_test(NAME aether.margin-adjacency COMMAND aether_margin_adjacency_test)

    add_executable(aether_control_sizing_test tests/test_control_sizing.cpp src/layout/form_controls.cpp src/css/text_transform.cpp)
    target_include_directories(aether_control_sizing_test PRIVATE src/layout)
    target_link_libraries(aether_control_sizing_test PRIVATE AetherUI::Core)
    add_test(NAME aether.control-sizing COMMAND aether_control_sizing_test)

    add_executable(aether_linear_gradient_test tests/test_linear_gradient.cpp src/css/linear_gradient.cpp)
    target_link_libraries(aether_linear_gradient_test PRIVATE AetherUI::Core)
    add_test(NAME aether.linear-gradient COMMAND aether_linear_gradient_test)

    add_executable(aether_background_layers_test tests/test_background_layers.cpp src/css/background_layers.cpp src/css/linear_gradient.cpp)
    target_link_libraries(aether_background_layers_test PRIVATE AetherUI::Core)
    add_test(NAME aether.background-layers COMMAND aether_background_layers_test)

    add_executable(aether_background_image_display_list_test tests/test_background_image_display_list.cpp)
    target_link_libraries(aether_background_image_display_list_test PRIVATE AetherUI::Core)
    add_test(NAME aether.background-image-display-list COMMAND aether_background_image_display_list_test)

    add_executable(aether_background_value_consistency_test tests/test_background_value_consistency.cpp)
    target_link_libraries(aether_background_value_consistency_test PRIVATE AetherUI::Core)
    add_test(NAME aether.background-value-consistency COMMAND aether_background_value_consistency_test)

    add_executable(aether_z_index_test tests/test_z_index.cpp src/rendering/stacking.cpp)
    target_include_directories(aether_z_index_test PRIVATE src/rendering)
    target_link_libraries(aether_z_index_test PRIVATE AetherUI::Core)
    add_test(NAME aether.z-index COMMAND aether_z_index_test)

    add_executable(aether_canvas_background_test tests/test_canvas_background.cpp)
    target_link_libraries(aether_canvas_background_test PRIVATE AetherUI::Core)
    add_test(NAME aether.canvas-background COMMAND aether_canvas_background_test)

    # Diagnostic harness for the current Kanban avatar fidelity investigation.
    # It becomes a CTest regression only after the observed layout/painter
    # invariant is established from its numeric output.
    add_executable(aether_avatar_radius_diagnostic tests/test_avatar_radius_diagnostic.cpp)
    target_link_libraries(aether_avatar_radius_diagnostic PRIVATE AetherUI::Core)

    add_executable(aether_background_image_geometry_test tests/test_background_image_geometry.cpp src/rendering/background_image_geometry.cpp)
    target_link_libraries(aether_background_image_geometry_test PRIVATE AetherUI::Core)
    add_test(NAME aether.background-image-geometry COMMAND aether_background_image_geometry_test)

    add_executable(aether_image_decode_test tests/test_image_decode.cpp)
    target_link_libraries(aether_image_decode_test PRIVATE AetherUI::Core)
    add_test(NAME aether.image-decode COMMAND aether_image_decode_test)

    add_executable(aether_background_gradient_tiling_test tests/test_background_gradient_tiling.cpp)
    target_link_libraries(aether_background_gradient_tiling_test PRIVATE AetherUI::Core)
    add_test(NAME aether.background-gradient-tiling COMMAND aether_background_gradient_tiling_test)

    add_executable(aether_backdrop_filters_test tests/test_backdrop_filters.cpp)
    target_link_libraries(aether_backdrop_filters_test PRIVATE AetherUI::Core)
    add_test(NAME aether.backdrop-filters COMMAND aether_backdrop_filters_test)

    add_executable(aether_text_transform_test tests/test_text_transform.cpp src/css/text_transform.cpp)
    target_link_libraries(aether_text_transform_test PRIVATE AetherUI::Core)
    add_test(NAME aether.text-transform COMMAND aether_text_transform_test)

    add_executable(aether_text_justify_test tests/test_text_justify.cpp src/layout/text_justify.cpp)
    target_link_libraries(aether_text_justify_test PRIVATE AetherUI::Core)
    add_test(NAME aether.text-justify COMMAND aether_text_justify_test)

    add_executable(aether_text_alignment_test tests/test_text_alignment.cpp)
    target_link_libraries(aether_text_alignment_test PRIVATE AetherUI::Core)
    add_test(NAME aether.text-alignment COMMAND aether_text_alignment_test)

    add_executable(aether_auto_margins_test tests/test_auto_margins.cpp)
    target_link_libraries(aether_auto_margins_test PRIVATE AetherUI::Core)
    target_include_directories(aether_auto_margins_test PRIVATE src/layout)
    add_test(NAME aether.auto-margins COMMAND aether_auto_margins_test)

    add_executable(aether_scroll_metrics_test tests/test_scroll_metrics.cpp)
    target_link_libraries(aether_scroll_metrics_test PRIVATE AetherUI::Core)
    add_test(NAME aether.scroll-metrics COMMAND aether_scroll_metrics_test)

    add_executable(aether_scroll_model_test tests/test_scroll_model.cpp)
    target_link_libraries(aether_scroll_model_test PRIVATE AetherUI::Core)
    add_test(NAME aether.scroll-model COMMAND aether_scroll_model_test)

    add_executable(aether_selector_structure_test tests/test_selector_structure.cpp)
    target_link_libraries(aether_selector_structure_test PRIVATE AetherUI::Core)
    add_test(NAME aether.selector-structure COMMAND aether_selector_structure_test)

    add_executable(aether_generated_pseudo_updates_test tests/test_generated_pseudo_updates.cpp)
    target_link_libraries(aether_generated_pseudo_updates_test PRIVATE AetherUI::Core)
    add_test(NAME aether.generated-pseudo-updates COMMAND aether_generated_pseudo_updates_test)

    add_executable(aether_visibility_test tests/test_visibility.cpp)
    target_link_libraries(aether_visibility_test PRIVATE AetherUI::Core)
    add_test(NAME aether.visibility COMMAND aether_visibility_test)

    add_executable(aether_style_impact_test tests/test_style_impact.cpp)
    target_link_libraries(aether_style_impact_test PRIVATE AetherUI::Core)
    add_test(NAME aether.style-impact COMMAND aether_style_impact_test)

    add_executable(aether_shorthand_var_test tests/test_shorthand_var.cpp)
    target_include_directories(aether_shorthand_var_test PRIVATE src)
    target_link_libraries(aether_shorthand_var_test PRIVATE AetherUI::Core)
    add_test(NAME aether.shorthand-var COMMAND aether_shorthand_var_test)

    add_executable(aether_render_plan_test tests/test_render_plan.cpp)
    target_link_libraries(aether_render_plan_test PRIVATE AetherUI::Core)
    add_test(NAME aether.render-plan COMMAND aether_render_plan_test)

    add_executable(aether_layout_ownership_test tests/test_layout_ownership.cpp src/layout/layout_box.cpp)
    target_include_directories(aether_layout_ownership_test PRIVATE src/layout include)
    target_link_libraries(aether_layout_ownership_test PRIVATE AetherUI::Core)
    add_test(NAME aether.layout-ownership COMMAND aether_layout_ownership_test)

    add_executable(aether_current_color_test tests/test_current_color.cpp)
    target_link_libraries(aether_current_color_test PRIVATE AetherUI::Core)
    add_test(NAME aether.current-color COMMAND aether_current_color_test)

    add_executable(aether_transforms_test tests/test_transforms.cpp src/css/transforms.cpp src/rendering/transform_matrix.cpp)
    target_link_libraries(aether_transforms_test PRIVATE AetherUI::Core)
    target_include_directories(aether_transforms_test PRIVATE src/css src/rendering)
    add_test(NAME aether.transforms COMMAND aether_transforms_test)

    add_executable(aether_table_layout_test tests/test_table_layout.cpp src/layout/table_layout.cpp)
    target_include_directories(aether_table_layout_test PRIVATE src/layout)
    add_test(NAME aether.table-layout COMMAND aether_table_layout_test)

    add_executable(aether_table_layout_integration_test tests/test_table_layout_integration.cpp)
    target_link_libraries(aether_table_layout_integration_test PRIVATE AetherUI::Core)
    add_test(NAME aether.table-layout-integration COMMAND aether_table_layout_integration_test)

    add_executable(aether_transition_runtime_test tests/test_transition_runtime.cpp)
    target_include_directories(aether_transition_runtime_test PRIVATE src)
    target_link_libraries(aether_transition_runtime_test PRIVATE AetherUI::Core)
    add_test(NAME aether.transition-runtime COMMAND aether_transition_runtime_test)

    add_executable(aether_paint_context_test tests/test_paint_context.cpp)
    target_link_libraries(aether_paint_context_test PRIVATE AetherUI::Core)
    add_test(NAME aether.paint-context COMMAND aether_paint_context_test)

    add_executable(aether_ua_stylesheet_test tests/test_ua_stylesheet.cpp)
    target_link_libraries(aether_ua_stylesheet_test PRIVATE AetherUI::Core)
    add_test(NAME aether.ua-stylesheet COMMAND aether_ua_stylesheet_test)

    add_executable(aether_style_map_lifetime_test tests/test_style_map_lifetime.cpp)
    target_link_libraries(aether_style_map_lifetime_test PRIVATE AetherUI::Core)
    add_test(NAME aether.style-map-lifetime COMMAND aether_style_map_lifetime_test)
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
