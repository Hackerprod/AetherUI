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
    src/css/frozen_style.cpp
    src/css/style_freeze.cpp
    src/css/style_c_export.cpp
    src/css/frozen_c_string.cpp
    src/css/frozen_node_record.cpp
    src/css/immutable_style_backing.cpp
    src/css/style_map_version.cpp
    src/css/style_revision.cpp
    src/css/frozen_measure_read.cpp
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
    src/animations/contextual_transitions.cpp
    src/style_runtime/style_values.cpp
    src/style_runtime/style_computed.cpp
    src/style_runtime/style_property_registry.cpp
    src/style_runtime/builtin_properties.cpp
    src/style_runtime/shadow_values.cpp
    src/style_runtime/transition_properties.cpp
    src/style_runtime/cascade.cpp
    src/style_runtime/selector_matcher.cpp
    src/style_runtime/selector_parser.cpp
    src/style_runtime/rule_index.cpp
    src/style_runtime/css_variable_tokens.cpp
    src/style_runtime/variable_resolver.cpp
    src/style_runtime/custom_property_scope.cpp
    src/style_runtime/frozen_runtime_metadata.cpp
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

    add_executable(aether_shadow_values_test tests/test_shadow_values.cpp
        src/style_runtime/shadow_values.cpp src/style_runtime/css_variable_tokens.cpp
        src/css/css_length.cpp)
    target_compile_definitions(aether_shadow_values_test PRIVATE AETHER_CSS_NUMERIC_ONLY)
    target_link_libraries(aether_shadow_values_test PRIVATE AetherUI::Core)
    add_test(NAME aether.shadow-values COMMAND aether_shadow_values_test)

    add_executable(aether_shadow_cascade_test tests/test_shadow_cascade.cpp)
    target_link_libraries(aether_shadow_cascade_test PRIVATE AetherUI::Core)
    add_test(NAME aether.shadow-cascade COMMAND aether_shadow_cascade_test)

    add_executable(aether_shadow_symbols_test tests/test_shadow_symbols.cpp)
    target_link_libraries(aether_shadow_symbols_test PRIVATE AetherUI::Core)
    add_test(NAME aether.shadow-symbols COMMAND aether_shadow_symbols_test)

    add_executable(aether_shadow_grammar_consumers_test tests/test_shadow_grammar_consumers.cpp)
    target_link_libraries(aether_shadow_grammar_consumers_test PRIVATE AetherUI::Core)
    add_test(NAME aether.shadow-grammar-consumers COMMAND aether_shadow_grammar_consumers_test)

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_executable(aether_rgb_locale_failure_test tests/test_rgb_locale_failure.cpp
            src/css/color_rgba.cpp src/style_runtime/css_variable_tokens.cpp)
        target_include_directories(aether_rgb_locale_failure_test PRIVATE include)
        target_link_options(aether_rgb_locale_failure_test PRIVATE "-Wl,--wrap=newlocale")
        add_test(NAME aether.rgb-locale-failure COMMAND aether_rgb_locale_failure_test)

        # Interpose the actual conversion calls, not those in an already linked DSO.
        add_executable(aether_css_numeric_locale_failure_test tests/test_css_numeric_locale_failure.cpp
            src/css/css_length.cpp src/css/color_rgba.cpp src/style_runtime/css_variable_tokens.cpp)
        target_include_directories(aether_css_numeric_locale_failure_test PRIVATE include)
        target_link_options(aether_css_numeric_locale_failure_test PRIVATE
            "-Wl,--wrap=newlocale" "-Wl,--wrap=freelocale" "-Wl,--wrap=strtof_l" "-Wl,--wrap=strtod_l")
        add_test(NAME aether.css-numeric-locale-failure COMMAND aether_css_numeric_locale_failure_test)
    endif()

    # Process-local locale mutation stays in a separate, serial test executable.
    add_executable(aether_rgb_locale_test tests/test_rgb_locale.cpp)
    target_link_libraries(aether_rgb_locale_test PRIVATE AetherUI::Core)
    add_test(NAME aether.rgb-locale COMMAND aether_rgb_locale_test)

    add_executable(aether_rgb_runtime_test tests/test_rgb_runtime.cpp)
    target_link_libraries(aether_rgb_runtime_test PRIVATE AetherUI::Core)
    add_test(NAME aether.rgb-runtime COMMAND aether_rgb_runtime_test)

    add_executable(aether_background_current_color_test tests/test_background_current_color.cpp)
    target_link_libraries(aether_background_current_color_test PRIVATE AetherUI::Core)
    add_test(NAME aether.background-current-color COMMAND aether_background_current_color_test)

    add_executable(aether_named_colors_test tests/test_named_colors.cpp)
    target_link_libraries(aether_named_colors_test PRIVATE AetherUI::Core)
    add_test(NAME aether.named-colors COMMAND aether_named_colors_test)

    add_executable(aether_named_colors_runtime_test tests/test_named_colors_runtime.cpp)
    target_link_libraries(aether_named_colors_runtime_test PRIVATE AetherUI::Core)
    add_test(NAME aether.named-colors-runtime COMMAND aether_named_colors_runtime_test)

    add_executable(aether_rgb_tokens_test tests/test_rgb_tokens.cpp)
    target_link_libraries(aether_rgb_tokens_test PRIVATE AetherUI::Core)
    add_test(NAME aether.rgb-tokens COMMAND aether_rgb_tokens_test)

    add_executable(aether_css_numeric_conversion_test tests/test_css_numeric_conversion.cpp
        src/css/css_length.cpp)
    target_compile_definitions(aether_css_numeric_conversion_test PRIVATE AETHER_CSS_NUMERIC_ONLY)
    target_link_libraries(aether_css_numeric_conversion_test PRIVATE AetherUI::Core)
    add_test(NAME aether.css-numeric-conversion COMMAND aether_css_numeric_conversion_test)

    add_executable(aether_length_locale_test tests/test_length_locale.cpp)
    target_link_libraries(aether_length_locale_test PRIVATE AetherUI::Core)
    add_test(NAME aether.length-locale COMMAND aether_length_locale_test)

    find_package(Threads REQUIRED)
    add_executable(aether_length_locale_threads_test tests/test_length_locale_threads.cpp)
    target_link_libraries(aether_length_locale_threads_test PRIVATE AetherUI::Core Threads::Threads)
    add_test(NAME aether.length-locale-threads COMMAND aether_length_locale_threads_test)

    add_executable(aether_html_space_tokens_test tests/test_html_space_tokens.cpp)
    add_test(NAME aether.html-space-tokens COMMAND aether_html_space_tokens_test)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        foreach(char_mode signed unsigned)
            add_executable(aether_html_space_tokens_${char_mode}_test tests/test_html_space_tokens.cpp)
            target_compile_options(aether_html_space_tokens_${char_mode}_test PRIVATE -f${char_mode}-char)
            add_test(NAME aether.html-space-tokens-${char_mode} COMMAND aether_html_space_tokens_${char_mode}_test)
        endforeach()
    endif()

    add_executable(aether_class_token_consumers_test tests/test_class_token_consumers.cpp
        src/style_runtime/selector_parser.cpp src/style_runtime/selector_matcher.cpp)
    target_include_directories(aether_class_token_consumers_test PRIVATE include src/style_runtime)
    target_link_libraries(aether_class_token_consumers_test PRIVATE AetherUI::Core)
    add_test(NAME aether.class-token-consumers COMMAND aether_class_token_consumers_test)

    add_executable(aether_class_token_runtime_test tests/test_class_token_runtime.cpp)
    target_link_libraries(aether_class_token_runtime_test PRIVATE AetherUI::Core)
    add_test(NAME aether.class-token-runtime COMMAND aether_class_token_runtime_test)

    add_executable(aether_ascii_text_test tests/test_ascii_text.cpp)
    add_test(NAME aether.ascii-text COMMAND aether_ascii_text_test)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        foreach(char_mode signed unsigned)
            add_executable(aether_ascii_text_${char_mode}_test tests/test_ascii_text.cpp)
            target_compile_options(aether_ascii_text_${char_mode}_test PRIVATE -f${char_mode}-char)
            add_test(NAME aether.ascii-text-${char_mode} COMMAND aether_ascii_text_${char_mode}_test)
        endforeach()
    endif()

    add_executable(aether_selector_attribute_values_test tests/test_selector_attribute_values.cpp
        src/style_runtime/selector_parser.cpp src/style_runtime/selector_matcher.cpp)
    target_include_directories(aether_selector_attribute_values_test PRIVATE include src/style_runtime)
    add_test(NAME aether.selector-attribute-values COMMAND aether_selector_attribute_values_test)

    add_executable(aether_html_attribute_values_runtime_test tests/test_html_attribute_values_runtime.cpp)
    target_link_libraries(aether_html_attribute_values_runtime_test PRIVATE AetherUI::Core)
    add_test(NAME aether.html-attribute-values-runtime COMMAND aether_html_attribute_values_runtime_test)

    # Locale changes are process-local; keep this regression in its own executable.
    add_executable(aether_selector_ascii_locale_test tests/test_selector_ascii_locale.cpp
        src/style_runtime/selector_parser.cpp src/style_runtime/selector_matcher.cpp
        src/style_runtime/rule_index.cpp)
    target_include_directories(aether_selector_ascii_locale_test PRIVATE include src/style_runtime)
    target_link_libraries(aether_selector_ascii_locale_test PRIVATE AetherUI::Core)
    add_test(NAME aether.selector-ascii-locale COMMAND aether_selector_ascii_locale_test)
    if(MSVC)
        target_compile_options(aether_selector_attribute_values_test PRIVATE /utf-8)
        target_compile_options(aether_html_attribute_values_runtime_test PRIVATE /utf-8)
        target_compile_options(aether_selector_ascii_locale_test PRIVATE /utf-8)
    endif()

    add_executable(aether_html_attribute_case_runtime_test tests/test_html_attribute_case_runtime.cpp)
    target_link_libraries(aether_html_attribute_case_runtime_test PRIVATE AetherUI::Core)
    add_test(NAME aether.html-attribute-case-runtime COMMAND aether_html_attribute_case_runtime_test)

    add_executable(aether_selector_attribute_case_test tests/test_selector_attribute_case.cpp
        src/style_runtime/selector_parser.cpp src/style_runtime/selector_matcher.cpp)
    target_include_directories(aether_selector_attribute_case_test PRIVATE include src/style_runtime)
    add_test(NAME aether.selector-attribute-case COMMAND aether_selector_attribute_case_test)
    if(MSVC)
        target_compile_options(aether_selector_attribute_case_test PRIVATE /utf-8)
    endif()

    add_executable(aether_html_attribute_case_test tests/test_html_attribute_case.cpp)
    target_link_libraries(aether_html_attribute_case_test PRIVATE AetherUI::Core)
    add_test(NAME aether.html-attribute-case COMMAND aether_html_attribute_case_test)

    add_executable(aether_html_unquoted_attributes_test tests/test_html_unquoted_attributes.cpp)
    target_link_libraries(aether_html_unquoted_attributes_test PRIVATE AetherUI::Core)
    add_test(NAME aether.html-unquoted-attributes COMMAND aether_html_unquoted_attributes_test)

    add_executable(aether_html_duplicate_attributes_test tests/test_html_duplicate_attributes.cpp)
    target_link_libraries(aether_html_duplicate_attributes_test PRIVATE AetherUI::Core)
    add_test(NAME aether.html-duplicate-attributes COMMAND aether_html_duplicate_attributes_test)

    add_executable(aether_html_numeric_references_test tests/test_html_numeric_references.cpp)
    target_link_libraries(aether_html_numeric_references_test PRIVATE AetherUI::Core)
    add_test(NAME aether.html-numeric-references COMMAND aether_html_numeric_references_test)

    add_executable(aether_html_raw_text_test tests/test_html_raw_text.cpp)
    target_link_libraries(aether_html_raw_text_test PRIVATE AetherUI::Core)
    add_test(NAME aether.html-raw-text COMMAND aether_html_raw_text_test)

    add_executable(aether_html_rcdata_test tests/test_html_rcdata.cpp)
    target_link_libraries(aether_html_rcdata_test PRIVATE AetherUI::Core)
    add_test(NAME aether.html-rcdata COMMAND aether_html_rcdata_test)

    add_executable(aether_html_end_tag_suffix_test tests/test_html_end_tag_suffix.cpp)
    target_link_libraries(aether_html_end_tag_suffix_test PRIVATE AetherUI::Core)
    add_test(NAME aether.html-end-tag-suffix COMMAND aether_html_end_tag_suffix_test)

    add_executable(aether_html_comments_test tests/test_html_comments.cpp)
    target_link_libraries(aether_html_comments_test PRIVATE AetherUI::Core)
    add_test(NAME aether.html-comments COMMAND aether_html_comments_test)

    add_executable(aether_html_comment_consumers_test tests/test_html_comment_consumers.cpp)
    target_link_libraries(aether_html_comment_consumers_test PRIVATE AetherUI::Core)
    add_test(NAME aether.html-comment-consumers COMMAND aether_html_comment_consumers_test)

    add_executable(aether_html_end_tags_test tests/test_html_end_tags.cpp)
    target_link_libraries(aether_html_end_tags_test PRIVATE AetherUI::Core)
    add_test(NAME aether.html-end-tags COMMAND aether_html_end_tags_test)

    add_executable(aether_html_tag_open_test tests/test_html_tag_open.cpp)
    target_link_libraries(aether_html_tag_open_test PRIVATE AetherUI::Core)
    add_test(NAME aether.html-tag-open COMMAND aether_html_tag_open_test)

    add_executable(aether_html_tag_names_test tests/test_html_tag_names.cpp)
    target_link_libraries(aether_html_tag_names_test PRIVATE AetherUI::Core)
    add_test(NAME aether.html-tag-names COMMAND aether_html_tag_names_test)

    add_executable(aether_html_start_tag_eof_test tests/test_html_start_tag_eof.cpp)
    target_link_libraries(aether_html_start_tag_eof_test PRIVATE AetherUI::Core)
    add_test(NAME aether.html-start-tag-eof COMMAND aether_html_start_tag_eof_test)

    add_executable(aether_html_start_tag_eof_consumers_test tests/test_html_start_tag_eof_consumers.cpp)
    target_link_libraries(aether_html_start_tag_eof_consumers_test PRIVATE AetherUI::Core)
    add_test(NAME aether.html-start-tag-eof-consumers COMMAND aether_html_start_tag_eof_consumers_test)

    add_executable(aether_html_tag_name_consumers_test tests/test_html_tag_name_consumers.cpp
        src/layout/form_controls.cpp src/css/text_transform.cpp)
    target_include_directories(aether_html_tag_name_consumers_test PRIVATE src/layout)
    target_link_libraries(aether_html_tag_name_consumers_test PRIVATE AetherUI::Core)
    add_test(NAME aether.html-tag-name-consumers COMMAND aether_html_tag_name_consumers_test)

    add_executable(aether_dom_id_order_test tests/test_dom_id_order.cpp)
    target_link_libraries(aether_dom_id_order_test PRIVATE AetherUI::Core)
    add_test(NAME aether.dom-id-order COMMAND aether_dom_id_order_test)

    add_executable(aether_dom_mutation_integrity_test tests/test_dom_mutation_integrity.cpp)
    target_link_libraries(aether_dom_mutation_integrity_test PRIVATE AetherUI::Core)
    add_test(NAME aether.dom-mutation-integrity COMMAND aether_dom_mutation_integrity_test)

    # Complement public layout/DOM fixtures with an intentionally nonreciprocal
    # private relation and provenance flags independent of tag names.
    add_executable(aether_dom_mutation_boundary_test tests/test_dom_mutation_boundary.cpp)
    target_link_libraries(aether_dom_mutation_boundary_test PRIVATE AetherUI::Core)
    add_test(NAME aether.dom-mutation-boundary COMMAND aether_dom_mutation_boundary_test)

    add_executable(aether_css_component_consumers_test tests/test_css_component_consumers.cpp
        src/style_runtime/css_variable_tokens.cpp src/css/border_radii.cpp
        src/css/background_layers.cpp src/css/backdrop_filters.cpp
        src/css/transforms.cpp src/css/linear_gradient.cpp src/css/css_length.cpp)
    target_compile_definitions(aether_css_component_consumers_test PRIVATE AETHER_CSS_NUMERIC_ONLY)
    target_link_libraries(aether_css_component_consumers_test PRIVATE AetherUI::Core)
    add_test(NAME aether.css-component-consumers COMMAND aether_css_component_consumers_test)

    add_executable(aether_border_radii_test tests/test_border_radii.cpp src/css/border_radii.cpp src/rendering/border_painter.cpp src/style_runtime/css_variable_tokens.cpp)
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

    add_executable(aether_flex_decoration_test tests/test_flex_decoration.cpp)
    target_link_libraries(aether_flex_decoration_test PRIVATE AetherUI::Core)
    add_test(NAME aether.flex-decoration COMMAND aether_flex_decoration_test)

    add_executable(aether_flex_intrinsic_basis_test tests/test_flex_intrinsic_basis.cpp)
    target_link_libraries(aether_flex_intrinsic_basis_test PRIVATE AetherUI::Core)
    add_test(NAME aether.flex-intrinsic-basis COMMAND aether_flex_intrinsic_basis_test)

    add_executable(aether_descendant_intrinsic_width_test tests/test_descendant_intrinsic_width.cpp)
    target_link_libraries(aether_descendant_intrinsic_width_test PRIVATE AetherUI::Core)
    add_test(NAME aether.descendant-intrinsic-width COMMAND aether_descendant_intrinsic_width_test)

    add_executable(aether_cyclic_intrinsic_edges_test tests/test_cyclic_intrinsic_edges.cpp)
    target_link_libraries(aether_cyclic_intrinsic_edges_test PRIVATE AetherUI::Core)
    add_test(NAME aether.cyclic-intrinsic-edges COMMAND aether_cyclic_intrinsic_edges_test)

    add_executable(aether_intrinsic_sizing_test tests/test_intrinsic_sizing.cpp src/layout/intrinsic_sizing.cpp)
    target_include_directories(aether_intrinsic_sizing_test PRIVATE src/layout)
    target_link_libraries(aether_intrinsic_sizing_test PRIVATE AetherUI::Core)
    add_test(NAME aether.intrinsic-sizing COMMAND aether_intrinsic_sizing_test)

    add_executable(aether_inline_text_test tests/test_inline_text.cpp src/layout/inline_text.cpp)
    target_include_directories(aether_inline_text_test PRIVATE src/layout)
    target_link_libraries(aether_inline_text_test PRIVATE AetherUI::Core)
    add_test(NAME aether.inline-text COMMAND aether_inline_text_test)

    add_executable(aether_inline_text_visibility_test
        tests/test_inline_text_visibility.cpp src/layout/inline_text.cpp)
    target_include_directories(aether_inline_text_visibility_test PRIVATE include src/layout)
    add_test(NAME aether.inline-text-visibility COMMAND aether_inline_text_visibility_test)

    add_executable(aether_margin_adjacency_test tests/test_margin_adjacency.cpp src/layout/margin_adjacency.cpp)
    add_test(NAME aether.margin-adjacency COMMAND aether_margin_adjacency_test)

    add_executable(aether_control_sizing_test tests/test_control_sizing.cpp src/layout/form_controls.cpp src/css/text_transform.cpp)
    target_include_directories(aether_control_sizing_test PRIVATE src/layout)
    target_link_libraries(aether_control_sizing_test PRIVATE AetherUI::Core)
    add_test(NAME aether.control-sizing COMMAND aether_control_sizing_test)

    add_executable(aether_linear_gradient_test tests/test_linear_gradient.cpp src/css/linear_gradient.cpp src/style_runtime/css_variable_tokens.cpp)
    target_link_libraries(aether_linear_gradient_test PRIVATE AetherUI::Core)
    add_test(NAME aether.linear-gradient COMMAND aether_linear_gradient_test)

    add_executable(aether_gradient_angle_test tests/test_gradient_angle.cpp
        src/css/linear_gradient.cpp src/style_runtime/css_variable_tokens.cpp)
    target_link_libraries(aether_gradient_angle_test PRIVATE AetherUI::Core Threads::Threads)
    add_test(NAME aether.gradient-angle COMMAND aether_gradient_angle_test)
    set_tests_properties(aether.gradient-angle PROPERTIES RUN_SERIAL TRUE)

    add_executable(aether_gradient_angle_runtime_test tests/test_gradient_angle_runtime.cpp)
    target_link_libraries(aether_gradient_angle_runtime_test PRIVATE AetherUI::Core)
    add_test(NAME aether.gradient-angle-runtime COMMAND aether_gradient_angle_runtime_test)
    set_tests_properties(aether.gradient-angle-runtime PROPERTIES RUN_SERIAL TRUE)

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # Interpose the real gradient, dispatch and numeric conversion sources.
        add_executable(aether_gradient_angle_failure_test tests/test_gradient_angle_failure.cpp
            src/style_runtime/builtin_properties.cpp src/style_runtime/transition_properties.cpp
            src/style_runtime/shadow_values.cpp src/style_runtime/css_variable_tokens.cpp
            src/css/css_length.cpp src/css/background_layers.cpp src/css/linear_gradient.cpp
            src/css/color_rgba.cpp src/css/border_radii.cpp src/css/backdrop_filters.cpp src/css/transforms.cpp)
        target_compile_definitions(aether_gradient_angle_failure_test PRIVATE AETHER_CSS_NUMERIC_ONLY)
        target_link_libraries(aether_gradient_angle_failure_test PRIVATE AetherUI::Core)
        target_link_options(aether_gradient_angle_failure_test PRIVATE
            "-Wl,--wrap=newlocale" "-Wl,--wrap=freelocale" "-Wl,--wrap=strtof_l"
            "-Wl,--wrap=strtod_l" "-Wl,--wrap=strtof" "-Wl,--wrap=strtod")
        add_test(NAME aether.gradient-angle-failure COMMAND aether_gradient_angle_failure_test)
    endif()

    add_executable(aether_gradient_boundary_test tests/test_gradient_boundary.cpp
        src/css/linear_gradient.cpp src/css/background_layers.cpp src/style_runtime/css_variable_tokens.cpp)
    target_link_libraries(aether_gradient_boundary_test PRIVATE AetherUI::Core)
    add_test(NAME aether.gradient-boundary COMMAND aether_gradient_boundary_test)

    add_executable(aether_gradient_boundary_runtime_test tests/test_gradient_boundary_runtime.cpp)
    target_link_libraries(aether_gradient_boundary_runtime_test PRIVATE AetherUI::Core)
    add_test(NAME aether.gradient-boundary-runtime COMMAND aether_gradient_boundary_runtime_test)

    # Exercise registered private callbacks, including the shorthand fallback,
    # without adding exports to the public library.
    add_executable(aether_gradient_boundary_dispatch_test tests/test_gradient_boundary_dispatch.cpp
        src/style_runtime/builtin_properties.cpp src/style_runtime/transition_properties.cpp
        src/style_runtime/shadow_values.cpp src/style_runtime/css_variable_tokens.cpp
        src/css/css_length.cpp src/css/background_layers.cpp src/css/linear_gradient.cpp
        src/css/border_radii.cpp src/css/backdrop_filters.cpp src/css/transforms.cpp)
    target_compile_definitions(aether_gradient_boundary_dispatch_test PRIVATE AETHER_CSS_NUMERIC_ONLY)
    target_link_libraries(aether_gradient_boundary_dispatch_test PRIVATE AetherUI::Core)
    add_test(NAME aether.gradient-boundary-dispatch COMMAND aether_gradient_boundary_dispatch_test)

    add_executable(aether_flex_number_pipeline_test tests/test_flex_number_pipeline.cpp)
    target_link_libraries(aether_flex_number_pipeline_test PRIVATE AetherUI::Core ${CMAKE_DL_LIBS})
    add_test(NAME aether.flex-number-pipeline COMMAND aether_flex_number_pipeline_test)

    # Private registered callbacks and interposed conversions, without exports.
    add_executable(aether_flex_number_registry_test tests/test_flex_number_registry.cpp
        tests/test_flex_number_locale.cpp
        src/style_runtime/builtin_properties.cpp src/style_runtime/transition_properties.cpp
        src/style_runtime/shadow_values.cpp src/style_runtime/css_variable_tokens.cpp
        src/css/css_length.cpp src/css/background_layers.cpp src/css/linear_gradient.cpp
        src/css/border_radii.cpp src/css/backdrop_filters.cpp src/css/transforms.cpp)
    target_compile_definitions(aether_flex_number_registry_test PRIVATE AETHER_CSS_NUMERIC_ONLY)
    target_link_libraries(aether_flex_number_registry_test PRIVATE AetherUI::Core)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_definitions(aether_flex_number_registry_test PRIVATE AETHER_FLEX_NUMBER_LOCALE_WRAPPERS)
        target_link_options(aether_flex_number_registry_test PRIVATE
            "-Wl,--wrap=newlocale,--wrap=freelocale,--wrap=strtof_l,--wrap=strtod_l,--wrap=strtof,--wrap=strtod")
    endif()
    add_test(NAME aether.flex-number-registry COMMAND aether_flex_number_registry_test)

    add_executable(aether_opacity_inheritance_test tests/test_opacity_inheritance.cpp)
    target_link_libraries(aether_opacity_inheritance_test PRIVATE AetherUI::Core)
    add_test(NAME aether.opacity-inheritance COMMAND aether_opacity_inheritance_test)

    add_executable(aether_opacity_pipeline_test tests/test_opacity_pipeline.cpp)
    target_link_libraries(aether_opacity_pipeline_test PRIVATE AetherUI::Core ${CMAKE_DL_LIBS})
    add_test(NAME aether.opacity-pipeline COMMAND aether_opacity_pipeline_test)

    add_executable(aether_transform_origin_dispatch_test tests/test_transform_origin_dispatch.cpp
        tests/test_corner_radius_registry.cpp tests/test_opacity_registry.cpp
        src/style_runtime/builtin_properties.cpp src/style_runtime/transition_properties.cpp
        src/style_runtime/shadow_values.cpp src/style_runtime/css_variable_tokens.cpp
        src/css/css_length.cpp src/css/background_layers.cpp src/css/linear_gradient.cpp
        src/css/border_radii.cpp src/css/backdrop_filters.cpp src/css/transforms.cpp)
    target_compile_definitions(aether_transform_origin_dispatch_test PRIVATE AETHER_CSS_NUMERIC_ONLY)
    target_link_libraries(aether_transform_origin_dispatch_test PRIVATE AetherUI::Core)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_definitions(aether_transform_origin_dispatch_test PRIVATE AETHER_OPACITY_LOCALE_WRAPPERS)
        target_link_options(aether_transform_origin_dispatch_test PRIVATE
            "-Wl,--wrap=newlocale,--wrap=freelocale,--wrap=strtod_l,--wrap=strtod,--wrap=strtof")
    endif()
    add_test(NAME aether.transform-origin-dispatch COMMAND aether_transform_origin_dispatch_test)

    add_executable(aether_transform_dispatch_test tests/test_transform_dispatch.cpp
        src/style_runtime/builtin_properties.cpp src/style_runtime/transition_properties.cpp
        src/style_runtime/shadow_values.cpp src/style_runtime/css_variable_tokens.cpp
        src/css/css_length.cpp src/css/background_layers.cpp src/css/linear_gradient.cpp
        src/css/border_radii.cpp src/css/backdrop_filters.cpp src/css/transforms.cpp)
    target_compile_definitions(aether_transform_dispatch_test PRIVATE AETHER_CSS_NUMERIC_ONLY)
    target_link_libraries(aether_transform_dispatch_test PRIVATE AetherUI::Core)
    add_test(NAME aether.transform-dispatch COMMAND aether_transform_dispatch_test)

    add_executable(aether_measure_property_scratch_test tests/test_measure_property_scratch.cpp
        src/style_runtime/builtin_properties.cpp src/style_runtime/transition_properties.cpp
        src/style_runtime/shadow_values.cpp src/style_runtime/css_variable_tokens.cpp
        src/css/css_length.cpp src/css/background_layers.cpp src/css/linear_gradient.cpp
        src/css/border_radii.cpp src/css/backdrop_filters.cpp src/css/transforms.cpp)
    target_compile_definitions(aether_measure_property_scratch_test PRIVATE AETHER_CSS_NUMERIC_ONLY)
    target_link_libraries(aether_measure_property_scratch_test PRIVATE AetherUI::Core)
    add_test(NAME aether.measure-property-scratch COMMAND aether_measure_property_scratch_test)

    add_executable(aether_frozen_style_test tests/test_frozen_style.cpp
        src/css/frozen_style.cpp src/css/style_freeze.cpp src/css/style_c_export.cpp
        src/css/frozen_c_string.cpp)
    target_include_directories(aether_frozen_style_test PRIVATE src)
    target_link_libraries(aether_frozen_style_test PRIVATE AetherUI::Core)
    add_test(NAME aether.frozen-style COMMAND aether_frozen_style_test)

    add_executable(aether_frozen_backing_test tests/test_frozen_backing.cpp
        src/css/frozen_c_string.cpp src/css/frozen_node_record.cpp
        src/css/immutable_style_backing.cpp src/css/style_map_version.cpp
        src/style_runtime/frozen_runtime_metadata.cpp)
    target_include_directories(aether_frozen_backing_test PRIVATE include src)
    add_test(NAME aether.frozen-backing COMMAND aether_frozen_backing_test)

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # Compile exporter AND owned free here: both allocation and destruction
        # must reach wrappers, not calls inside an already-linked DSO.
        add_executable(aether_frozen_style_oom_test tests/test_frozen_style_oom.cpp
            src/abi/aether_abi.cpp src/css/computed_style.cpp
            src/css/frozen_style.cpp src/css/style_freeze.cpp src/css/style_c_export.cpp
            src/css/frozen_c_string.cpp)
        target_include_directories(aether_frozen_style_oom_test PRIVATE src)
        target_link_libraries(aether_frozen_style_oom_test PRIVATE AetherUI::Core)
        target_link_options(aether_frozen_style_oom_test PRIVATE "-Wl,--wrap=malloc" "-Wl,--wrap=free")
        add_test(NAME aether.frozen-style-oom COMMAND aether_frozen_style_oom_test)

        add_executable(aether_frozen_backing_oom_test tests/test_frozen_backing_oom.cpp
            src/css/frozen_c_string.cpp src/css/frozen_node_record.cpp
            src/css/immutable_style_backing.cpp src/css/style_map_version.cpp)
        target_include_directories(aether_frozen_backing_oom_test PRIVATE include src)
        target_link_options(aether_frozen_backing_oom_test PRIVATE "-Wl,--wrap=malloc" "-Wl,--wrap=free")
        add_test(NAME aether.frozen-backing-oom COMMAND aether_frozen_backing_oom_test)

        add_executable(aether_immutable_style_revision_test tests/test_immutable_style_revision.cpp
            src/css/style_revision.cpp src/css/style_map_version.cpp
            src/css/immutable_style_backing.cpp src/css/frozen_node_record.cpp
            src/css/frozen_c_string.cpp src/style_runtime/frozen_runtime_metadata.cpp)
        target_include_directories(aether_immutable_style_revision_test PRIVATE include src)
        target_link_options(aether_immutable_style_revision_test PRIVATE "-Wl,--wrap=malloc" "-Wl,--wrap=free")
        add_test(NAME aether.immutable-style-revision COMMAND aether_immutable_style_revision_test)

        add_executable(aether_immutable_style_handle_test tests/test_immutable_style_handle.cpp
            src/abi/aether_abi.cpp src/css/style_resolver.cpp src/css/style_map_version.cpp
            src/css/style_revision.cpp src/css/immutable_style_backing.cpp src/css/frozen_node_record.cpp
            src/css/frozen_c_string.cpp src/css/frozen_style.cpp src/css/style_freeze.cpp
            src/css/style_c_export.cpp src/css/computed_style.cpp src/css/css_length.cpp
            src/css/background_layers.cpp src/css/linear_gradient.cpp src/css/backdrop_filters.cpp)
        target_include_directories(aether_immutable_style_handle_test PRIVATE include src)
        target_compile_options(aether_immutable_style_handle_test PRIVATE -ffunction-sections -fdata-sections -ffp-contract=off)
        target_link_options(aether_immutable_style_handle_test PRIVATE "-Wl,--gc-sections" "-Wl,--wrap=malloc" "-Wl,--wrap=free")
        add_test(NAME aether.immutable-style-handle COMMAND aether_immutable_style_handle_test)

        add_executable(aether_frozen_runtime_metadata_test tests/test_frozen_runtime_metadata.cpp
            src/style_runtime/frozen_runtime_metadata.cpp src/css/frozen_measure_read.cpp
            src/css/frozen_c_string.cpp src/css/style_freeze.cpp src/css/css_length.cpp)
        target_include_directories(aether_frozen_runtime_metadata_test PRIVATE include src)
        add_test(NAME aether.frozen-runtime-metadata COMMAND aether_frozen_runtime_metadata_test)
    endif()

    add_executable(aether_background_layers_test tests/test_background_layers.cpp src/css/background_layers.cpp src/css/linear_gradient.cpp src/style_runtime/css_variable_tokens.cpp)
    target_link_libraries(aether_background_layers_test PRIVATE AetherUI::Core)
    add_test(NAME aether.background-layers COMMAND aether_background_layers_test)

    add_executable(aether_background_image_display_list_test tests/test_background_image_display_list.cpp)
    target_link_libraries(aether_background_image_display_list_test PRIVATE AetherUI::Core)
    add_test(NAME aether.background-image-display-list COMMAND aether_background_image_display_list_test)

    add_executable(aether_background_position_parser_test tests/test_background_position_parser.cpp
        src/style_runtime/builtin_properties.cpp src/style_runtime/transition_properties.cpp
        src/style_runtime/shadow_values.cpp src/style_runtime/css_variable_tokens.cpp
        src/css/css_length.cpp src/css/background_layers.cpp src/css/linear_gradient.cpp
        src/css/border_radii.cpp src/css/backdrop_filters.cpp src/css/transforms.cpp)
    target_compile_definitions(aether_background_position_parser_test PRIVATE AETHER_CSS_NUMERIC_ONLY)
    target_link_libraries(aether_background_position_parser_test PRIVATE AetherUI::Core)
    add_test(NAME aether.background-position-parser COMMAND aether_background_position_parser_test)
    add_executable(aether_background_position_pipeline_test tests/test_background_position_pipeline.cpp)
    target_link_libraries(aether_background_position_pipeline_test PRIVATE AetherUI::Core)
    add_test(NAME aether.background-position-pipeline COMMAND aether_background_position_pipeline_test)
    add_executable(aether_background_position_display_list_test tests/test_background_position_display_list.cpp)
    target_link_libraries(aether_background_position_display_list_test PRIVATE AetherUI::Core)
    add_test(NAME aether.background-position-display-list COMMAND aether_background_position_display_list_test)

    add_executable(aether_background_repeat_runtime_test tests/test_background_repeat_runtime.cpp)
    target_link_libraries(aether_background_repeat_runtime_test PRIVATE AetherUI::Core)
    add_test(NAME aether.background-repeat-runtime COMMAND aether_background_repeat_runtime_test)

    add_executable(aether_background_position_context_test tests/test_background_position_context.cpp)
    target_link_libraries(aether_background_position_context_test PRIVATE AetherUI::Core)
    add_test(NAME aether.background-position-context COMMAND aether_background_position_context_test)

    add_executable(aether_background_value_consistency_test tests/test_background_value_consistency.cpp)
    target_link_libraries(aether_background_value_consistency_test PRIVATE AetherUI::Core)
    add_test(NAME aether.background-value-consistency COMMAND aether_background_value_consistency_test)

    add_executable(aether_z_index_test tests/test_z_index.cpp src/rendering/stacking.cpp src/layout/layout_box.cpp
        src/css/style_freeze.cpp src/css/frozen_c_string.cpp src/css/frozen_node_record.cpp
        src/css/immutable_style_backing.cpp src/css/style_map_version.cpp)
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
    add_executable(aether_backdrop_blur_semantics_test tests/test_backdrop_blur_semantics.cpp)
    target_link_libraries(aether_backdrop_blur_semantics_test PRIVATE AetherUI::Core)
    add_test(NAME aether.backdrop-blur-semantics COMMAND aether_backdrop_blur_semantics_test)

    add_executable(aether_text_transform_test tests/test_text_transform.cpp src/css/text_transform.cpp)
    target_link_libraries(aether_text_transform_test PRIVATE AetherUI::Core)
    add_test(NAME aether.text-transform COMMAND aether_text_transform_test)

    add_executable(aether_text_justify_test tests/test_text_justify.cpp src/layout/text_justify.cpp)
    target_link_libraries(aether_text_justify_test PRIVATE AetherUI::Core)
    add_test(NAME aether.text-justify COMMAND aether_text_justify_test)

    add_executable(aether_text_alignment_test tests/test_text_alignment.cpp)
    target_link_libraries(aether_text_alignment_test PRIVATE AetherUI::Core)
    add_test(NAME aether.text-alignment COMMAND aether_text_alignment_test)

    add_executable(aether_flex_negative_space_test tests/test_flex_negative_space.cpp)
    target_link_libraries(aether_flex_negative_space_test PRIVATE AetherUI::Core)
    target_include_directories(aether_flex_negative_space_test PRIVATE src/layout)
    add_test(NAME aether.flex-negative-space COMMAND aether_flex_negative_space_test)

    add_executable(aether_positioned_auto_margins_test tests/test_positioned_auto_margins.cpp)
    target_link_libraries(aether_positioned_auto_margins_test PRIVATE AetherUI::Core)
    target_include_directories(aether_positioned_auto_margins_test PRIVATE src)
    add_test(NAME aether.positioned-auto-margins COMMAND aether_positioned_auto_margins_test)

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

    add_executable(aether_media_percentage_lengths_test tests/test_media_percentage_lengths.cpp)
    target_link_libraries(aether_media_percentage_lengths_test PRIVATE AetherUI::Core)
    add_test(NAME aether.media-percentage-lengths COMMAND aether_media_percentage_lengths_test)

    add_executable(aether_media_typed_values_test tests/test_media_typed_values.cpp)
    target_link_libraries(aether_media_typed_values_test PRIVATE AetherUI::Core)
    add_test(NAME aether.media-typed-values COMMAND aether_media_typed_values_test)

    # Private AST/diagnostic coverage is separate from the public ABI fixture.
    add_executable(aether_media_condition_parser_test tests/test_media_condition_parser.cpp
        src/style_runtime/media_condition.cpp src/style_runtime/css_variable_tokens.cpp
        src/css/css_length.cpp)
    target_include_directories(aether_media_condition_parser_test PRIVATE include src/style_runtime)
    target_compile_definitions(aether_media_condition_parser_test PRIVATE AETHER_CSS_NUMERIC_ONLY)
    target_link_libraries(aether_media_condition_parser_test PRIVATE AetherUI::Core)
    if(NOT MSVC)
        target_compile_options(aether_media_condition_parser_test PRIVATE -ffp-contract=off)
    endif()
    add_test(NAME aether.media-condition-parser COMMAND aether_media_condition_parser_test)

    add_executable(aether_not_strict_list_test tests/test_not_strict_list.cpp)
    target_link_libraries(aether_not_strict_list_test PRIVATE AetherUI::Core)
    add_test(NAME aether.not-strict-list COMMAND aether_not_strict_list_test)

    add_executable(aether_has_relative_anchor_test tests/test_has_relative_anchor.cpp)
    target_link_libraries(aether_has_relative_anchor_test PRIVATE AetherUI::Core)
    add_test(NAME aether.has-relative-anchor COMMAND aether_has_relative_anchor_test)

    add_executable(aether_has_strict_list_test tests/test_has_strict_list.cpp)
    target_link_libraries(aether_has_strict_list_test PRIVATE AetherUI::Core)
    add_test(NAME aether.has-strict-list COMMAND aether_has_strict_list_test)

    # Strict list transactions and metadata without exporting private AST types.
    add_executable(aether_has_strict_list_parser_test tests/test_has_strict_list_parser.cpp
        src/style_runtime/selector_parser.cpp)
    target_include_directories(aether_has_strict_list_parser_test PRIVATE include src/style_runtime)
    add_test(NAME aether.has-strict-list-parser COMMAND aether_has_strict_list_parser_test)

    add_executable(aether_has_strict_list_compiler_test tests/test_has_strict_list_compiler.cpp
        src/style_runtime/style_source_compiler.cpp src/style_runtime/selector_parser.cpp
        src/style_runtime/rule_index.cpp src/style_runtime/media_condition.cpp
        src/style_runtime/css_variable_tokens.cpp src/css/css_length.cpp)
    target_include_directories(aether_has_strict_list_compiler_test PRIVATE include src/style_runtime)
    target_compile_definitions(aether_has_strict_list_compiler_test PRIVATE AETHER_CSS_NUMERIC_ONLY)
    target_link_libraries(aether_has_strict_list_compiler_test PRIVATE AetherUI::Core)
    add_test(NAME aether.has-strict-list-compiler COMMAND aether_has_strict_list_compiler_test)

    # Empty forgiving arguments remain valid AST nodes, not universal selectors.
    add_executable(aether_forgiving_empty_list_parser_test tests/test_forgiving_empty_list_parser.cpp
        src/style_runtime/selector_parser.cpp src/style_runtime/selector_matcher.cpp)
    target_include_directories(aether_forgiving_empty_list_parser_test PRIVATE include src/style_runtime)
    add_test(NAME aether.forgiving-empty-list-parser COMMAND aether_forgiving_empty_list_parser_test)

    add_executable(aether_forgiving_empty_list_compiler_test tests/test_forgiving_empty_list_compiler.cpp
        src/style_runtime/style_source_compiler.cpp src/style_runtime/selector_parser.cpp
        src/style_runtime/rule_index.cpp src/style_runtime/media_condition.cpp
        src/style_runtime/css_variable_tokens.cpp src/css/css_length.cpp)
    target_include_directories(aether_forgiving_empty_list_compiler_test PRIVATE include src/style_runtime)
    target_compile_definitions(aether_forgiving_empty_list_compiler_test PRIVATE AETHER_CSS_NUMERIC_ONLY)
    target_link_libraries(aether_forgiving_empty_list_compiler_test PRIVATE AetherUI::Core)
    add_test(NAME aether.forgiving-empty-list-compiler COMMAND aether_forgiving_empty_list_compiler_test)

    add_executable(aether_forgiving_empty_list_test tests/test_forgiving_empty_list.cpp)
    target_link_libraries(aether_forgiving_empty_list_test PRIVATE AetherUI::Core)
    add_test(NAME aether.forgiving-empty-list COMMAND aether_forgiving_empty_list_test)

    # HAS arguments reject pseudo-elements while preserving local list recovery.
    find_package(Threads REQUIRED)
    add_executable(aether_has_pseudo_element_parser_test tests/test_has_pseudo_element_parser.cpp
        src/style_runtime/selector_parser.cpp)
    target_include_directories(aether_has_pseudo_element_parser_test PRIVATE include src/style_runtime)
    target_link_libraries(aether_has_pseudo_element_parser_test PRIVATE Threads::Threads)
    add_test(NAME aether.has-pseudo-element-parser COMMAND aether_has_pseudo_element_parser_test)

    add_executable(aether_has_pseudo_element_compiler_test tests/test_has_pseudo_element_compiler.cpp
        src/style_runtime/style_source_compiler.cpp src/style_runtime/selector_parser.cpp
        src/style_runtime/rule_index.cpp src/style_runtime/media_condition.cpp
        src/style_runtime/css_variable_tokens.cpp src/css/css_length.cpp)
    target_include_directories(aether_has_pseudo_element_compiler_test PRIVATE include src/style_runtime)
    target_compile_definitions(aether_has_pseudo_element_compiler_test PRIVATE AETHER_CSS_NUMERIC_ONLY)
    target_link_libraries(aether_has_pseudo_element_compiler_test PRIVATE AetherUI::Core)
    add_test(NAME aether.has-pseudo-element-compiler COMMAND aether_has_pseudo_element_compiler_test)

    add_executable(aether_has_pseudo_element_test tests/test_has_pseudo_element.cpp)
    target_link_libraries(aether_has_pseudo_element_test PRIVATE AetherUI::Core)
    add_test(NAME aether.has-pseudo-element COMMAND aether_has_pseudo_element_test)

    add_executable(aether_selector_attributes_test tests/test_selector_attributes.cpp)
    target_link_libraries(aether_selector_attributes_test PRIVATE AetherUI::Core)
    add_test(NAME aether.selector-attributes COMMAND aether_selector_attributes_test)

    add_executable(aether_selector_escapes_test tests/test_selector_escapes.cpp)
    target_link_libraries(aether_selector_escapes_test PRIVATE AetherUI::Core)
    if(MSVC)
        target_compile_options(aether_selector_escapes_test PRIVATE /utf-8)
    endif()
    add_test(NAME aether.selector-escapes COMMAND aether_selector_escapes_test)

    # Private EOF/AST coverage, without extending the installed C ABI.
    add_executable(aether_selector_escape_parser_test tests/test_selector_escape_parser.cpp
        src/style_runtime/selector_parser.cpp)
    target_include_directories(aether_selector_escape_parser_test PRIVATE include src/style_runtime)
    add_test(NAME aether.selector-escape-parser COMMAND aether_selector_escape_parser_test)

    # CSS identifier admission is source-based; CDC is contextual to ident/hash.
    add_executable(aether_selector_identifier_start_parser_test tests/test_selector_identifier_start_parser.cpp
        src/style_runtime/selector_parser.cpp)
    target_include_directories(aether_selector_identifier_start_parser_test PRIVATE include src/style_runtime)
    add_test(NAME aether.selector-identifier-start-parser COMMAND aether_selector_identifier_start_parser_test)

    add_executable(aether_selector_identifier_start_compiler_test tests/test_selector_identifier_start_compiler.cpp
        src/style_runtime/style_source_compiler.cpp src/style_runtime/selector_parser.cpp
        src/style_runtime/rule_index.cpp src/style_runtime/media_condition.cpp
        src/style_runtime/css_variable_tokens.cpp src/css/css_length.cpp)
    target_include_directories(aether_selector_identifier_start_compiler_test PRIVATE include src/style_runtime)
    target_compile_definitions(aether_selector_identifier_start_compiler_test PRIVATE AETHER_CSS_NUMERIC_ONLY)
    target_link_libraries(aether_selector_identifier_start_compiler_test PRIVATE AetherUI::Core)
    add_test(NAME aether.selector-identifier-start-compiler COMMAND aether_selector_identifier_start_compiler_test)

    add_executable(aether_selector_identifier_start_runtime_test tests/test_selector_identifier_start_runtime.cpp)
    target_link_libraries(aether_selector_identifier_start_runtime_test PRIVATE AetherUI::Core)
    add_test(NAME aether.selector-identifier-start-runtime COMMAND aether_selector_identifier_start_runtime_test)
    if(MSVC)
        target_compile_options(aether_selector_identifier_start_parser_test PRIVATE /utf-8)
        target_compile_options(aether_selector_identifier_start_runtime_test PRIVATE /utf-8)
    endif()

    # Every direct ID occurrence is a constraint and contributes specificity.
    add_executable(aether_selector_multiple_ids_parser_test tests/test_selector_multiple_ids_parser.cpp
        src/style_runtime/selector_parser.cpp)
    target_include_directories(aether_selector_multiple_ids_parser_test PRIVATE include src/style_runtime)
    add_test(NAME aether.selector-multiple-ids-parser COMMAND aether_selector_multiple_ids_parser_test)

    add_executable(aether_selector_multiple_ids_compiler_test tests/test_selector_multiple_ids_compiler.cpp
        src/style_runtime/style_source_compiler.cpp src/style_runtime/selector_parser.cpp
        src/style_runtime/rule_index.cpp src/style_runtime/media_condition.cpp
        src/style_runtime/css_variable_tokens.cpp src/css/css_length.cpp)
    target_include_directories(aether_selector_multiple_ids_compiler_test PRIVATE include src/style_runtime)
    target_compile_definitions(aether_selector_multiple_ids_compiler_test PRIVATE AETHER_CSS_NUMERIC_ONLY)
    target_link_libraries(aether_selector_multiple_ids_compiler_test PRIVATE AetherUI::Core)
    add_test(NAME aether.selector-multiple-ids-compiler COMMAND aether_selector_multiple_ids_compiler_test)

    add_executable(aether_selector_multiple_ids_test tests/test_selector_multiple_ids.cpp)
    target_link_libraries(aether_selector_multiple_ids_test PRIVATE AetherUI::Core)
    add_test(NAME aether.selector-multiple-ids COMMAND aether_selector_multiple_ids_test)
    if(MSVC)
        target_compile_options(aether_selector_multiple_ids_parser_test PRIVATE /utf-8)
        target_compile_options(aether_selector_multiple_ids_test PRIVATE /utf-8)
    endif()

    # Comment boundaries, trivia and An+B without exporting private AST types.
    add_executable(aether_selector_comments_parser_test tests/test_selector_comments_parser.cpp
        src/style_runtime/selector_parser.cpp)
    target_include_directories(aether_selector_comments_parser_test PRIVATE include src/style_runtime)
    add_test(NAME aether.selector-comments-parser COMMAND aether_selector_comments_parser_test)

    add_executable(aether_selector_comments_runtime_test tests/test_selector_comments_runtime.cpp)
    target_link_libraries(aether_selector_comments_runtime_test PRIVATE AetherUI::Core)
    add_test(NAME aether.selector-comments-runtime COMMAND aether_selector_comments_runtime_test)

    add_executable(aether_selector_comments_compiler_test tests/test_selector_comments_compiler.cpp
        src/style_runtime/style_source_compiler.cpp src/style_runtime/selector_parser.cpp
        src/style_runtime/media_condition.cpp src/style_runtime/css_variable_tokens.cpp
        src/css/css_length.cpp)
    target_include_directories(aether_selector_comments_compiler_test PRIVATE src/style_runtime)
    target_compile_definitions(aether_selector_comments_compiler_test PRIVATE AETHER_CSS_NUMERIC_ONLY)
    target_link_libraries(aether_selector_comments_compiler_test PRIVATE AetherUI::Core)
    add_test(NAME aether.selector-comments-compiler COMMAND aether_selector_comments_compiler_test)

    # Structural EOF recovery and typed component boundaries; no ABI additions.
    add_executable(aether_stylesheet_eof_test tests/test_stylesheet_eof.cpp)
    target_link_libraries(aether_stylesheet_eof_test PRIVATE AetherUI::Core)
    add_test(NAME aether.stylesheet-eof COMMAND aether_stylesheet_eof_test)

    add_executable(aether_stylesheet_eof_compiler_test tests/test_stylesheet_eof_compiler.cpp
        src/style_runtime/style_source_compiler.cpp src/style_runtime/selector_parser.cpp
        src/style_runtime/media_condition.cpp src/style_runtime/css_variable_tokens.cpp
        src/css/css_length.cpp)
    target_include_directories(aether_stylesheet_eof_compiler_test PRIVATE src/style_runtime)
    target_compile_definitions(aether_stylesheet_eof_compiler_test PRIVATE AETHER_CSS_NUMERIC_ONLY)
    target_link_libraries(aether_stylesheet_eof_compiler_test PRIVATE AetherUI::Core)
    add_test(NAME aether.stylesheet-eof-compiler COMMAND aether_stylesheet_eof_compiler_test)

    add_executable(aether_stylesheet_block_index_test tests/test_stylesheet_block_index.cpp
        src/style_runtime/css_variable_tokens.cpp)
    target_include_directories(aether_stylesheet_block_index_test PRIVATE src/style_runtime)
    add_test(NAME aether.stylesheet-block-index COMMAND aether_stylesheet_block_index_test)
    add_test(NAME aether.stylesheet-block-index-deep COMMAND aether_stylesheet_block_index_test deep)

    add_executable(aether_media_condition_iterative_test tests/test_media_condition_iterative.cpp
        src/style_runtime/media_condition.cpp src/style_runtime/css_variable_tokens.cpp
        src/css/css_length.cpp)
    target_include_directories(aether_media_condition_iterative_test PRIVATE include src/style_runtime)
    target_compile_definitions(aether_media_condition_iterative_test PRIVATE AETHER_CSS_NUMERIC_ONLY)
    target_link_libraries(aether_media_condition_iterative_test PRIVATE AetherUI::Core)
    if(NOT MSVC)
        target_compile_options(aether_media_condition_iterative_test PRIVATE -ffp-contract=off)
    endif()
    add_test(NAME aether.media-condition-iterative COMMAND aether_media_condition_iterative_test)

    foreach(mode closed open mixed)
        add_test(NAME aether.stylesheet-eof-${mode}-deep COMMAND aether_stylesheet_eof_test ${mode})
        add_test(NAME aether.stylesheet-eof-compiler-${mode}-deep COMMAND aether_stylesheet_eof_compiler_test ${mode})
    endforeach()

    add_executable(aether_stylesheet_source_order_test tests/test_stylesheet_source_order.cpp)
    target_link_libraries(aether_stylesheet_source_order_test PRIVATE AetherUI::Core)
    add_test(NAME aether.stylesheet-source-order COMMAND aether_stylesheet_source_order_test)

    add_executable(aether_stylesheet_diagnostic_reentrancy_test tests/test_stylesheet_diagnostic_reentrancy.cpp)
    target_link_libraries(aether_stylesheet_diagnostic_reentrancy_test PRIVATE AetherUI::Core)
    add_test(NAME aether.stylesheet-diagnostic-reentrancy COMMAND aether_stylesheet_diagnostic_reentrancy_test)

    add_executable(aether_stylesheet_source_order_compiler_test tests/test_stylesheet_source_order_compiler.cpp
        src/style_runtime/style_source_compiler.cpp src/style_runtime/selector_parser.cpp
        src/style_runtime/media_condition.cpp src/style_runtime/css_variable_tokens.cpp
        src/style_runtime/cascade.cpp src/css/css_length.cpp)
    target_include_directories(aether_stylesheet_source_order_compiler_test PRIVATE src/style_runtime)
    target_compile_definitions(aether_stylesheet_source_order_compiler_test PRIVATE AETHER_CSS_NUMERIC_ONLY)
    target_link_libraries(aether_stylesheet_source_order_compiler_test PRIVATE AetherUI::Core)
    add_test(NAME aether.stylesheet-source-order-compiler COMMAND aether_stylesheet_source_order_compiler_test)

    # White-box runtime counters plus a test-only reduced source-size predicate.
    # Compile real core sources into this executable, without exporting test hooks
    # or changing the library's limits. The test TU includes runtime_core.cpp.
    get_target_property(source_order_runtime_sources aether_core SOURCES)
    list(REMOVE_ITEM source_order_runtime_sources
        src/style_runtime/style_runtime_core.cpp src/style_runtime/style_source_compiler.cpp)
    add_executable(aether_stylesheet_source_order_runtime_test
        tests/test_stylesheet_source_order_runtime.cpp tests/test_stylesheet_source_order_small_source.cpp
        ${source_order_runtime_sources})
    target_include_directories(aether_stylesheet_source_order_runtime_test PRIVATE include)
    target_compile_definitions(aether_stylesheet_source_order_runtime_test PRIVATE AETHER_EXPORTS)
    if(MSVC)
        target_compile_options(aether_stylesheet_source_order_runtime_test PRIVATE /W4 /utf-8)
        target_compile_definitions(aether_stylesheet_source_order_runtime_test PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
    else()
        target_compile_options(aether_stylesheet_source_order_runtime_test PRIVATE -Wall -Wextra -pedantic -ffp-contract=off)
    endif()
    add_test(NAME aether.stylesheet-source-order-runtime COMMAND aether_stylesheet_source_order_runtime_test)

    # Pseudo-name identity and recursive metadata without exporting private AST types.
    add_executable(aether_pseudo_name_parser_test tests/test_pseudo_name_parser.cpp
        src/style_runtime/selector_parser.cpp)
    target_include_directories(aether_pseudo_name_parser_test PRIVATE include src/style_runtime)
    add_test(NAME aether.pseudo-name-parser COMMAND aether_pseudo_name_parser_test)

    add_executable(aether_pseudo_name_runtime_test tests/test_pseudo_name_runtime.cpp)
    target_link_libraries(aether_pseudo_name_runtime_test PRIVATE AetherUI::Core)
    add_test(NAME aether.pseudo-name-runtime COMMAND aether_pseudo_name_runtime_test)

    add_executable(aether_selector_structure_test tests/test_selector_structure.cpp)
    target_link_libraries(aether_selector_structure_test PRIVATE AetherUI::Core)
    add_test(NAME aether.selector-structure COMMAND aether_selector_structure_test)

    add_executable(aether_selector_empty_test tests/test_selector_empty.cpp)
    target_link_libraries(aether_selector_empty_test PRIVATE AetherUI::Core)
    add_test(NAME aether.selector-empty COMMAND aether_selector_empty_test)

    # Empty text nodes are representable internally, but need no new public ABI.
    add_executable(aether_selector_empty_text_test tests/test_selector_empty_text.cpp)
    target_link_libraries(aether_selector_empty_text_test PRIVATE AetherUI::Core)
    add_test(NAME aether.selector-empty-text COMMAND aether_selector_empty_text_test)

    add_executable(aether_generated_pseudo_updates_test tests/test_generated_pseudo_updates.cpp)
    target_link_libraries(aether_generated_pseudo_updates_test PRIVATE AetherUI::Core)
    add_test(NAME aether.generated-pseudo-updates COMMAND aether_generated_pseudo_updates_test)

    # Private malformed/synthetic-node boundary checks, with no ABI extension.
    add_executable(aether_selector_dom_boundary_test tests/test_selector_dom_boundary.cpp
        src/style_runtime/selector_parser.cpp src/style_runtime/selector_matcher.cpp)
    target_include_directories(aether_selector_dom_boundary_test PRIVATE include src/style_runtime)
    add_test(NAME aether.selector-dom-boundary COMMAND aether_selector_dom_boundary_test)

    # Generated content must never change the logical DOM used by selectors.
    add_executable(aether_generated_selector_metamorphic_test tests/test_generated_selector_metamorphic.cpp)
    target_link_libraries(aether_generated_selector_metamorphic_test PRIVATE AetherUI::Core)
    add_test(NAME aether.generated-selector-metamorphic COMMAND aether_generated_selector_metamorphic_test)

    add_executable(aether_generated_selector_origin_test tests/test_generated_selector_origin.cpp)
    target_link_libraries(aether_generated_selector_origin_test PRIVATE AetherUI::Core)
    add_test(NAME aether.generated-selector-origin COMMAND aether_generated_selector_origin_test)

    add_executable(aether_generated_selector_lifecycle_test tests/test_generated_selector_lifecycle.cpp)
    target_link_libraries(aether_generated_selector_lifecycle_test PRIVATE AetherUI::Core)
    add_test(NAME aether.generated-selector-lifecycle COMMAND aether_generated_selector_lifecycle_test)

    add_executable(aether_generated_selector_display_list_test tests/test_generated_selector_display_list.cpp)
    target_link_libraries(aether_generated_selector_display_list_test PRIVATE AetherUI::Core)
    add_test(NAME aether.generated-selector-display-list COMMAND aether_generated_selector_display_list_test)

    add_executable(aether_visibility_test tests/test_visibility.cpp)
    target_link_libraries(aether_visibility_test PRIVATE AetherUI::Core)
    add_test(NAME aether.visibility COMMAND aether_visibility_test)

    add_executable(aether_style_impact_test tests/test_style_impact.cpp)
    target_link_libraries(aether_style_impact_test PRIVATE AetherUI::Core)
    add_test(NAME aether.style-impact COMMAND aether_style_impact_test)

    add_executable(aether_css_variable_strings_test tests/test_css_variable_strings.cpp src/style_runtime/css_variable_tokens.cpp src/style_runtime/variable_resolver.cpp)
    target_include_directories(aether_css_variable_strings_test PRIVATE src)
    target_link_libraries(aether_css_variable_strings_test PRIVATE AetherUI::Core)
    add_test(NAME aether.css-variable-strings COMMAND aether_css_variable_strings_test)

    # Token equivalence and deterministic work-limit tests stay private.
    add_executable(aether_css_variable_boundaries_test tests/test_css_variable_boundaries.cpp
        src/style_runtime/css_variable_tokens.cpp src/style_runtime/variable_resolver.cpp)
    target_include_directories(aether_css_variable_boundaries_test PRIVATE include src)
    target_link_libraries(aether_css_variable_boundaries_test PRIVATE AetherUI::Core)
    add_test(NAME aether.css-variable-boundaries COMMAND aether_css_variable_boundaries_test)

    add_executable(aether_css_calc_trivia_test tests/test_css_calc_trivia.cpp)
    target_link_libraries(aether_css_calc_trivia_test PRIVATE AetherUI::Core)
    add_test(NAME aether.css-calc-trivia COMMAND aether_css_calc_trivia_test)

    add_executable(aether_pending_declaration_syntax_test
        tests/test_pending_declaration_syntax.cpp src/style_runtime/css_variable_tokens.cpp)
    target_include_directories(aether_pending_declaration_syntax_test PRIVATE src/style_runtime)
    add_test(NAME aether.pending-declaration-syntax COMMAND aether_pending_declaration_syntax_test)

    add_executable(aether_shorthand_compiler_test tests/test_shorthand_compiler.cpp
        src/style_runtime/style_source_compiler.cpp src/style_runtime/selector_parser.cpp
        src/style_runtime/media_condition.cpp src/style_runtime/css_variable_tokens.cpp
        src/css/css_length.cpp)
    target_include_directories(aether_shorthand_compiler_test PRIVATE src/style_runtime)
    target_compile_definitions(aether_shorthand_compiler_test PRIVATE AETHER_CSS_NUMERIC_ONLY)
    target_link_libraries(aether_shorthand_compiler_test PRIVATE AetherUI::Core)
    add_test(NAME aether.shorthand-compiler COMMAND aether_shorthand_compiler_test)

    add_executable(aether_shorthand_atomicity_test tests/test_shorthand_atomicity.cpp)
    target_link_libraries(aether_shorthand_atomicity_test PRIVATE AetherUI::Core)
    add_test(NAME aether.shorthand-atomicity COMMAND aether_shorthand_atomicity_test)

    add_executable(aether_shorthand_var_test tests/test_shorthand_var.cpp)
    target_include_directories(aether_shorthand_var_test PRIVATE src)
    target_link_libraries(aether_shorthand_var_test PRIVATE AetherUI::Core)
    add_test(NAME aether.shorthand-var COMMAND aether_shorthand_var_test)

    add_executable(aether_render_plan_test tests/test_render_plan.cpp)
    target_link_libraries(aether_render_plan_test PRIVATE AetherUI::Core)
    add_test(NAME aether.render-plan COMMAND aether_render_plan_test)

    add_executable(aether_layout_ownership_test tests/test_layout_ownership.cpp src/layout/layout_box.cpp src/css/computed_style.cpp
        src/css/frozen_style.cpp src/css/frozen_c_string.cpp src/css/style_map_version.cpp)
    target_include_directories(aether_layout_ownership_test PRIVATE src/layout include)
    target_link_libraries(aether_layout_ownership_test PRIVATE AetherUI::Core)
    add_test(NAME aether.layout-ownership COMMAND aether_layout_ownership_test)

    add_executable(aether_current_color_test tests/test_current_color.cpp)
    target_link_libraries(aether_current_color_test PRIVATE AetherUI::Core)
    add_test(NAME aether.current-color COMMAND aether_current_color_test)

    add_executable(aether_transforms_test tests/test_transforms.cpp src/css/transforms.cpp src/rendering/transform_matrix.cpp src/style_runtime/css_variable_tokens.cpp src/css/css_length.cpp)
    target_compile_definitions(aether_transforms_test PRIVATE AETHER_CSS_NUMERIC_ONLY)
    target_link_libraries(aether_transforms_test PRIVATE AetherUI::Core)
    target_include_directories(aether_transforms_test PRIVATE src/css src/rendering)
    add_test(NAME aether.transforms COMMAND aether_transforms_test)

    add_executable(aether_transform_origin_parser_test tests/test_transform_origin_parser.cpp
        src/css/transforms.cpp src/rendering/transform_matrix.cpp
        src/style_runtime/css_variable_tokens.cpp src/css/css_length.cpp)
    target_compile_definitions(aether_transform_origin_parser_test PRIVATE AETHER_CSS_NUMERIC_ONLY)
    target_include_directories(aether_transform_origin_parser_test PRIVATE src/css src/rendering)
    target_link_libraries(aether_transform_origin_parser_test PRIVATE AetherUI::Core)
    add_test(NAME aether.transform-origin-parser COMMAND aether_transform_origin_parser_test)

    add_executable(aether_transform_origin_runtime_test tests/test_transform_origin_runtime.cpp)
    target_link_libraries(aether_transform_origin_runtime_test PRIVATE AetherUI::Core)
    add_test(NAME aether.transform-origin-runtime COMMAND aether_transform_origin_runtime_test)

    add_executable(aether_transform_parser_test tests/test_transform_parser.cpp
        src/css/transforms.cpp src/style_runtime/css_variable_tokens.cpp src/css/css_length.cpp)
    target_compile_definitions(aether_transform_parser_test PRIVATE AETHER_CSS_NUMERIC_ONLY)
    target_include_directories(aether_transform_parser_test PRIVATE src/css)
    target_link_libraries(aether_transform_parser_test PRIVATE AetherUI::Core)
    add_test(NAME aether.transform-parser COMMAND aether_transform_parser_test)

    add_executable(aether_transform_runtime_test tests/test_transform_runtime.cpp)
    target_link_libraries(aether_transform_runtime_test PRIVATE AetherUI::Core)
    add_test(NAME aether.transform-runtime COMMAND aether_transform_runtime_test)

    add_executable(aether_transform_locale_test tests/test_transform_locale.cpp
        src/css/transforms.cpp src/style_runtime/css_variable_tokens.cpp src/css/css_length.cpp)
    target_compile_definitions(aether_transform_locale_test PRIVATE AETHER_CSS_NUMERIC_ONLY)
    target_link_libraries(aether_transform_locale_test PRIVATE AetherUI::Core)
    add_test(NAME aether.transform-locale COMMAND aether_transform_locale_test)
    set_tests_properties(aether.transform-locale PROPERTIES RUN_SERIAL TRUE)

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_executable(aether_transform_locale_failure_test tests/test_transform_locale_failure.cpp
            src/css/transforms.cpp src/style_runtime/css_variable_tokens.cpp src/css/css_length.cpp)
        target_compile_definitions(aether_transform_locale_failure_test PRIVATE AETHER_CSS_NUMERIC_ONLY)
        target_link_libraries(aether_transform_locale_failure_test PRIVATE AetherUI::Core)
        target_link_options(aether_transform_locale_failure_test PRIVATE
            "-Wl,--wrap=newlocale" "-Wl,--wrap=freelocale" "-Wl,--wrap=strtof_l"
            "-Wl,--wrap=strtod_l" "-Wl,--wrap=strtof" "-Wl,--wrap=strtod")
        add_test(NAME aether.transform-locale-failure COMMAND aether_transform_locale_failure_test)
    endif()

    add_executable(aether_table_layout_test tests/test_table_layout.cpp src/layout/table_layout.cpp)
    target_include_directories(aether_table_layout_test PRIVATE src/layout)
    add_test(NAME aether.table-layout COMMAND aether_table_layout_test)

    add_executable(aether_table_cell_final_contract_test tests/test_table_cell_final_contract.cpp)
    target_link_libraries(aether_table_cell_final_contract_test PRIVATE AetherUI::Core)
    add_test(NAME aether.table-cell-final-contract COMMAND aether_table_cell_final_contract_test)

    add_executable(aether_table_cell_final_height_test tests/test_table_cell_final_height.cpp)
    target_link_libraries(aether_table_cell_final_height_test PRIVATE AetherUI::Core)
    add_test(NAME aether.table-cell-final-height COMMAND aether_table_cell_final_height_test)

    add_executable(aether_table_layout_integration_test tests/test_table_layout_integration.cpp)
    target_link_libraries(aether_table_layout_integration_test PRIVATE AetherUI::Core)
    add_test(NAME aether.table-layout-integration COMMAND aether_table_layout_integration_test)

    add_executable(aether_relative_percentages_test tests/test_relative_percentages.cpp)
    target_link_libraries(aether_relative_percentages_test PRIVATE AetherUI::Core)
    add_test(NAME aether.relative-percentages COMMAND aether_relative_percentages_test)

    add_executable(aether_fixed_viewport_test tests/test_fixed_viewport.cpp
        src/css/style_freeze.cpp src/css/frozen_c_string.cpp src/css/frozen_node_record.cpp
        src/css/immutable_style_backing.cpp src/css/style_map_version.cpp)
    target_include_directories(aether_fixed_viewport_test PRIVATE src)
    target_link_libraries(aether_fixed_viewport_test PRIVATE AetherUI::Core)
    add_test(NAME aether.fixed-viewport COMMAND aether_fixed_viewport_test)

    add_executable(aether_transition_runtime_test tests/test_transition_runtime.cpp)
    target_include_directories(aether_transition_runtime_test PRIVATE src)
    target_link_libraries(aether_transition_runtime_test PRIVATE AetherUI::Core)
    add_test(NAME aether.transition-runtime COMMAND aether_transition_runtime_test)

    # This contract target must build before/without Core: the module owns its
    # parse/apply behavior, not the DOM, cascade, or global property catalog.
    add_executable(aether_transition_property_module_test
        tests/test_transition_property_module.cpp
        src/style_runtime/transition_properties.cpp
        src/style_runtime/css_variable_tokens.cpp)
    target_include_directories(aether_transition_property_module_test PRIVATE include src/style_runtime)
    target_compile_features(aether_transition_property_module_test PRIVATE cxx_std_20)
    add_test(NAME aether.transition-property-module COMMAND aether_transition_property_module_test)

    # Each private header must compile as the sole include in its own TU.
    set(transition_header_checks)
    foreach(header transition_properties_internal style_property_registry_internal
            style_string_utils_internal builtin_property_parse_utils)
        set(check_source "${CMAKE_CURRENT_BINARY_DIR}/transition_header_${header}.cpp")
        file(WRITE "${check_source}" "#include \"${header}.h\"\n")
        list(APPEND transition_header_checks "${check_source}")
    endforeach()
    add_library(aether_transition_property_headers_test OBJECT ${transition_header_checks})
    target_include_directories(aether_transition_property_headers_test PRIVATE include src/style_runtime)
    target_compile_features(aether_transition_property_headers_test PRIVATE cxx_std_20)
    add_dependencies(aether_transition_property_module_test aether_transition_property_headers_test)

    add_executable(aether_transition_property_pipeline_test tests/test_transition_property_pipeline.cpp)
    target_link_libraries(aether_transition_property_pipeline_test PRIVATE AetherUI::Core)
    add_test(NAME aether.transition-property-pipeline COMMAND aether_transition_property_pipeline_test)

    add_executable(aether_paint_context_test tests/test_paint_context.cpp)
    target_link_libraries(aether_paint_context_test PRIVATE AetherUI::Core)
    add_test(NAME aether.paint-context COMMAND aether_paint_context_test)

    add_executable(aether_ua_stylesheet_test tests/test_ua_stylesheet.cpp)
    target_link_libraries(aether_ua_stylesheet_test PRIVATE AetherUI::Core)
    add_test(NAME aether.ua-stylesheet COMMAND aether_ua_stylesheet_test)

    add_executable(aether_computed_custom_property_scope_test tests/test_computed_custom_property_scope.cpp
        src/style_runtime/custom_property_scope.cpp src/style_runtime/variable_resolver.cpp
        src/style_runtime/css_variable_tokens.cpp)
    target_include_directories(aether_computed_custom_property_scope_test PRIVATE include)
    add_test(NAME aether.computed-custom-property-scope COMMAND aether_computed_custom_property_scope_test)

    add_executable(aether_inherited_custom_properties_test tests/test_inherited_custom_properties.cpp)
    target_link_libraries(aether_inherited_custom_properties_test PRIVATE AetherUI::Core)
    if(MSVC)
        target_compile_options(aether_computed_custom_property_scope_test PRIVATE /utf-8)
        target_compile_options(aether_inherited_custom_properties_test PRIVATE /utf-8)
    endif()
    add_test(NAME aether.inherited-custom-properties COMMAND aether_inherited_custom_properties_test)

    add_executable(aether_custom_property_case_test tests/test_custom_property_case.cpp)
    target_link_libraries(aether_custom_property_case_test PRIVATE AetherUI::Core)
    if(MSVC)
        target_compile_options(aether_custom_property_case_test PRIVATE /utf-8)
    endif()
    add_test(NAME aether.custom-property-case COMMAND aether_custom_property_case_test)

    add_executable(aether_important_tokens_test
        tests/test_important_tokens.cpp
        src/style_runtime/css_variable_tokens.cpp)
    target_include_directories(aether_important_tokens_test PRIVATE src/style_runtime)
    target_compile_features(aether_important_tokens_test PRIVATE cxx_std_20)
    add_test(NAME aether.important-tokens COMMAND aether_important_tokens_test)

    add_executable(aether_important_test tests/test_important.cpp)
    target_link_libraries(aether_important_test PRIVATE AetherUI::Core)
    add_test(NAME aether.important COMMAND aether_important_test)

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

if(AETHERUI_BUILD_TESTS)
    add_executable(aether_contextual_abi_test tests/test_contextual_abi.c)
    set_target_properties(aether_contextual_abi_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
    target_link_libraries(aether_contextual_abi_test PRIVATE AetherUI::Core)
    add_test(NAME aether.contextual-abi COMMAND aether_contextual_abi_test)
    foreach(measure_test measure_computed contextual_used_geometry style_revisions_atomic contextual_transition_frame contextual_rotated_clip)
        add_executable(aether_${measure_test}_test tests/test_${measure_test}.cpp)
        target_include_directories(aether_${measure_test}_test PRIVATE src)
        target_link_libraries(aether_${measure_test}_test PRIVATE AetherUI::Core)
        add_test(NAME aether.${measure_test} COMMAND aether_${measure_test}_test)
    endforeach()
    add_executable(aether_contextual_paint_geometry_test tests/test_contextual_paint_geometry.cpp
        src/rendering/background_image_geometry.cpp src/rendering/border_painter.cpp
        src/css/border_radii.cpp src/style_runtime/css_variable_tokens.cpp)
    target_include_directories(aether_contextual_paint_geometry_test PRIVATE src)
    target_link_libraries(aether_contextual_paint_geometry_test PRIVATE AetherUI::Core)
    add_test(NAME aether.contextual-paint-geometry COMMAND aether_contextual_paint_geometry_test)
    add_executable(aether_contextual_transitions_test tests/test_contextual_transitions.cpp src/layout/layout_scroll.cpp src/rendering/transform_matrix.cpp
        src/layout/layout_box.cpp src/css/style_resolver.cpp src/css/style_map_version.cpp
        src/css/computed_style.cpp src/css/style_c_export.cpp src/css/frozen_style.cpp src/css/frozen_c_string.cpp
        src/css/frozen_node_record.cpp src/css/immutable_style_backing.cpp)
    target_include_directories(aether_contextual_transitions_test PRIVATE src)
    target_link_libraries(aether_contextual_transitions_test PRIVATE AetherUI::Core)
    add_test(NAME aether.contextual-transitions COMMAND aether_contextual_transitions_test)
endif()

if(AETHERUI_BUILD_TESTS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(aether_style_malloc_atomic_test tests/test_style_malloc_atomic.cpp)
    target_link_libraries(aether_style_malloc_atomic_test PRIVATE AetherUI::Core)
    add_test(NAME aether.style-malloc-atomic COMMAND aether_style_malloc_atomic_test)
    set_tests_properties(aether.style-malloc-atomic PROPERTIES SKIP_RETURN_CODE 77)
    # malloc-backed list copies are separate failure boundaries from operator new.
    # Link actual core objects so --wrap reaches internal allocations too.
    get_target_property(contextual_malloc_core_sources aether_core SOURCES)
    add_executable(aether_contextual_geometry_malloc_test
        tests/test_contextual_geometry_malloc.cpp ${contextual_malloc_core_sources})
    target_include_directories(aether_contextual_geometry_malloc_test PRIVATE include src)
    target_compile_definitions(aether_contextual_geometry_malloc_test PRIVATE AETHER_EXPORTS)
    target_compile_options(aether_contextual_geometry_malloc_test PRIVATE -ffp-contract=off)
    target_link_options(aether_contextual_geometry_malloc_test PRIVATE -Wl,--wrap=malloc)
    add_test(NAME aether.contextual-geometry-malloc COMMAND aether_contextual_geometry_malloc_test)
endif()

if(AETHERUI_BUILD_TESTS)
    # These white-box dispatcher tests compile their real registry locally.
    # Its immutable parent readers stay private rather than expanding the ABI.
    foreach(reader_test aether_flex_number_registry_test aether_background_position_parser_test aether_gradient_boundary_dispatch_test aether_transform_origin_dispatch_test
            aether_transform_dispatch_test aether_measure_property_scratch_test)
        target_sources(${reader_test} PRIVATE src/css/computed_style.cpp src/css/frozen_style.cpp src/css/frozen_c_string.cpp
            src/css/frozen_measure_read.cpp src/css/style_freeze.cpp
            src/style_runtime/frozen_runtime_metadata.cpp)
    endforeach()
    if(TARGET aether_gradient_angle_failure_test)
        target_sources(aether_gradient_angle_failure_test PRIVATE
            src/css/computed_style.cpp src/css/frozen_style.cpp src/css/frozen_c_string.cpp
            src/css/frozen_measure_read.cpp src/css/style_freeze.cpp
            src/style_runtime/frozen_runtime_metadata.cpp)
    endif()
    foreach(string_reader_test aether_shorthand_var_test aether_style_map_lifetime_test aether_measure_computed_test
            aether_style_revisions_atomic_test)
        target_sources(${string_reader_test} PRIVATE src/css/frozen_c_string.cpp)
    endforeach()
    add_executable(aether_frozen_paint_kernel_readers_test tests/test_frozen_paint_kernel_readers.cpp
        src/rendering/transform_matrix.cpp src/rendering/stacking.cpp src/rendering/border_painter.cpp
        src/css/linear_gradient.cpp src/css/border_radii.cpp src/style_runtime/css_variable_tokens.cpp)
    target_include_directories(aether_frozen_paint_kernel_readers_test PRIVATE include src)
    target_link_libraries(aether_frozen_paint_kernel_readers_test PRIVATE AetherUI::Core)
    add_test(NAME aether.frozen-paint-kernel-readers COMMAND aether_frozen_paint_kernel_readers_test)
    add_executable(aether_frozen_paint_callback_pins_test tests/test_frozen_paint_callback_pins.cpp)
    target_link_libraries(aether_frozen_paint_callback_pins_test PRIVATE AetherUI::Core)
    add_test(NAME aether.frozen-paint-callback-pins COMMAND aether_frozen_paint_callback_pins_test)
    get_target_property(immutable_layout_core_sources aether_core SOURCES)
    add_executable(aether_layout_immutable_consumers_test tests/test_layout_immutable_consumers.cpp
        ${immutable_layout_core_sources})
    target_include_directories(aether_layout_immutable_consumers_test PRIVATE include src)
    target_compile_definitions(aether_layout_immutable_consumers_test PRIVATE AETHER_EXPORTS)
    if(NOT MSVC)
        target_compile_options(aether_layout_immutable_consumers_test PRIVATE -ffp-contract=off)
    endif()
    add_test(NAME aether.layout-immutable-consumers COMMAND aether_layout_immutable_consumers_test)

    # Share authentic core objects between these private tests. Direct object
    # linkage enables internal assertions and process-local allocation failure
    # injection without expanding the installed ABI.
    add_library(aether_flex_text_test_core OBJECT ${immutable_layout_core_sources})
    target_include_directories(aether_flex_text_test_core PRIVATE include src)
    target_compile_definitions(aether_flex_text_test_core PRIVATE AETHER_EXPORTS)
    # The semantic classifier is private. Reuse authentic core objects so its
    # allocation check and exhaustive inventory do not expand the public ABI.
    add_executable(aether_style_diff_classifier_test tests/test_style_diff_classifier.cpp
        $<TARGET_OBJECTS:aether_flex_text_test_core>)
    target_include_directories(aether_style_diff_classifier_test PRIVATE include src)
    target_compile_definitions(aether_style_diff_classifier_test PRIVATE AETHER_EXPORTS)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_definitions(aether_style_diff_classifier_test PRIVATE AETHER_DIFF_WRAP_MALLOC)
        target_link_options(aether_style_diff_classifier_test PRIVATE "-Wl,--wrap=malloc")
    endif()
    add_test(NAME aether.style-diff-classifier COMMAND aether_style_diff_classifier_test)
    add_executable(aether_style_diff_catalog_test tests/test_style_diff_catalog.cpp
        $<TARGET_OBJECTS:aether_flex_text_test_core>)
    target_include_directories(aether_style_diff_catalog_test PRIVATE include src)
    target_compile_definitions(aether_style_diff_catalog_test PRIVATE AETHER_EXPORTS)
    add_test(NAME aether.style-diff-catalog COMMAND aether_style_diff_catalog_test)
    add_executable(aether_style_semantic_diff_test tests/test_style_semantic_diff.cpp src/css/frozen_c_string.cpp)
    target_link_libraries(aether_style_semantic_diff_test PRIVATE AetherUI::Core)
    add_test(NAME aether.style-semantic-diff COMMAND aether_style_semantic_diff_test)
    add_executable(aether_style_semantic_diff_oom_test tests/test_style_semantic_diff_oom.cpp src/css/frozen_c_string.cpp)
    target_link_libraries(aether_style_semantic_diff_oom_test PRIVATE AetherUI::Core)
    add_test(NAME aether.style-semantic-diff-oom COMMAND aether_style_semantic_diff_oom_test)

    add_executable(aether_table_cell_final_private_test
        tests/test_table_cell_final_private.cpp
        $<TARGET_OBJECTS:aether_flex_text_test_core>)
    target_include_directories(aether_table_cell_final_private_test PRIVATE include src)
    target_compile_definitions(aether_table_cell_final_private_test PRIVATE AETHER_EXPORTS)
    add_test(NAME aether.table-cell-final-private COMMAND aether_table_cell_final_private_test)

    foreach(flex_text_test lifetime sequences)
        add_executable(aether_flex_text_${flex_text_test}_test
            tests/test_flex_text_${flex_text_test}.cpp
            $<TARGET_OBJECTS:aether_flex_text_test_core>)
        target_include_directories(aether_flex_text_${flex_text_test}_test PRIVATE include src)
        target_compile_definitions(aether_flex_text_${flex_text_test}_test PRIVATE AETHER_EXPORTS)
        add_test(NAME aether.flex-text-${flex_text_test} COMMAND aether_flex_text_${flex_text_test}_test)
    endforeach()

    add_executable(aether_flex_cross_auto_test tests/test_flex_cross_auto.cpp)
    target_link_libraries(aether_flex_cross_auto_test PRIVATE AetherUI::Core)
    add_test(NAME aether.flex-cross-auto COMMAND aether_flex_cross_auto_test)

    # Use authentic core objects for the full layout/clone allocation sweep.
    add_executable(aether_flex_cross_auto_lifetime_test
        tests/test_flex_cross_auto_lifetime.cpp
        $<TARGET_OBJECTS:aether_flex_text_test_core>)
    target_include_directories(aether_flex_cross_auto_lifetime_test PRIVATE include src)
    target_compile_definitions(aether_flex_cross_auto_lifetime_test PRIVATE AETHER_EXPORTS)
    add_test(NAME aether.flex-cross-auto-lifetime COMMAND aether_flex_cross_auto_lifetime_test)

    add_executable(aether_flex_content_basis_lifetime_test
        tests/test_flex_content_basis_lifetime.cpp
        $<TARGET_OBJECTS:aether_flex_text_test_core>)
    target_include_directories(aether_flex_content_basis_lifetime_test PRIVATE include src)
    target_compile_definitions(aether_flex_content_basis_lifetime_test PRIVATE AETHER_EXPORTS)
    add_test(NAME aether.flex-content-basis-lifetime COMMAND aether_flex_content_basis_lifetime_test)

    add_executable(aether_flex_intrinsic_basis_lifetime_test
        tests/test_flex_intrinsic_basis_lifetime.cpp
        $<TARGET_OBJECTS:aether_flex_text_test_core>)
    target_include_directories(aether_flex_intrinsic_basis_lifetime_test PRIVATE include src)
    target_compile_definitions(aether_flex_intrinsic_basis_lifetime_test PRIVATE AETHER_EXPORTS)
    add_test(NAME aether.flex-intrinsic-basis-lifetime COMMAND aether_flex_intrinsic_basis_lifetime_test)

    add_executable(aether_descendant_intrinsic_width_lifetime_test
        tests/test_descendant_intrinsic_width_lifetime.cpp
        $<TARGET_OBJECTS:aether_flex_text_test_core>)
    target_include_directories(aether_descendant_intrinsic_width_lifetime_test PRIVATE include src)
    target_compile_definitions(aether_descendant_intrinsic_width_lifetime_test PRIVATE AETHER_EXPORTS)
    add_test(NAME aether.descendant-intrinsic-width-lifetime COMMAND aether_descendant_intrinsic_width_lifetime_test)

    add_executable(aether_cyclic_intrinsic_edges_lifetime_test
        tests/test_cyclic_intrinsic_edges_lifetime.cpp
        $<TARGET_OBJECTS:aether_flex_text_test_core>)
    target_include_directories(aether_cyclic_intrinsic_edges_lifetime_test PRIVATE include src)
    target_compile_definitions(aether_cyclic_intrinsic_edges_lifetime_test PRIVATE AETHER_EXPORTS)
    add_test(NAME aether.cyclic-intrinsic-edges-lifetime COMMAND aether_cyclic_intrinsic_edges_lifetime_test)

    # The test includes the authentic layout engine to exercise its private
    # final validator. Compile every other core source exactly once, without a DSO.
    get_target_property(intrusive_registry_core_sources aether_core SOURCES)
    list(REMOVE_ITEM intrusive_registry_core_sources src/layout/layout_engine.cpp)
    add_executable(aether_layout_intrusive_registry_test tests/test_layout_intrusive_registry.cpp
        ${intrusive_registry_core_sources})
    target_include_directories(aether_layout_intrusive_registry_test PRIVATE include src src/layout)
    target_compile_definitions(aether_layout_intrusive_registry_test PRIVATE AETHER_EXPORTS)
    if(NOT MSVC)
        target_compile_options(aether_layout_intrusive_registry_test PRIVATE -ffp-contract=off)
    endif()
    add_test(NAME aether.layout-intrusive-registry COMMAND aether_layout_intrusive_registry_test)

    # Standalone resolver tests also compile both authentic scope readers.
    # Satisfy their private implementation dependencies without exporting them.
    get_property(reader_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(reader_target IN LISTS reader_targets)
        get_target_property(reader_sources ${reader_target} SOURCES)
        if("src/style_runtime/variable_resolver.cpp" IN_LIST reader_sources OR
           "src/style_runtime/custom_property_scope.cpp" IN_LIST reader_sources)
            foreach(reader_source src/style_runtime/frozen_runtime_metadata.cpp src/css/frozen_c_string.cpp)
                if(NOT reader_source IN_LIST reader_sources)
                    target_sources(${reader_target} PRIVATE ${reader_source})
                endif()
            endforeach()
        endif()
    endforeach()
endif()

if(AETHERUI_BUILD_TESTS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Link the real core objects so wrapping reaches every checked-edge call.
    get_target_property(checked_edges_core_sources aether_core SOURCES)
    add_executable(aether_layout_checked_edges_test tests/test_layout_checked_edges.cpp
        ${checked_edges_core_sources})
    target_include_directories(aether_layout_checked_edges_test PRIVATE include src)
    target_compile_definitions(aether_layout_checked_edges_test PRIVATE AETHER_EXPORTS)
    set_target_properties(aether_layout_checked_edges_test PROPERTIES INTERPROCEDURAL_OPTIMIZATION FALSE)
    target_compile_options(aether_layout_checked_edges_test PRIVATE -ffp-contract=off -fno-lto)
    target_link_options(aether_layout_checked_edges_test PRIVATE -fno-lto -Wl,--wrap=aether_css_length_resolve)
    add_test(NAME aether.layout-checked-edges COMMAND aether_layout_checked_edges_test)

    add_executable(aether_layout_checked_edges_atomic_test tests/test_layout_checked_edges_atomic.cpp
        ${checked_edges_core_sources})
    target_include_directories(aether_layout_checked_edges_atomic_test PRIVATE include src)
    target_compile_definitions(aether_layout_checked_edges_atomic_test PRIVATE AETHER_EXPORTS)
    set_target_properties(aether_layout_checked_edges_atomic_test PROPERTIES INTERPROCEDURAL_OPTIMIZATION FALSE)
    target_compile_options(aether_layout_checked_edges_atomic_test PRIVATE -ffp-contract=off -fno-lto)
    target_link_options(aether_layout_checked_edges_atomic_test PRIVATE -fno-lto
        -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc)
    add_test(NAME aether.layout-checked-edges-atomic COMMAND aether_layout_checked_edges_atomic_test)
endif()

if(AETHERUI_BUILD_TESTS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Interpose real sampler boundaries while preserving the production core target.
    get_target_property(contextual_sampler_core_sources aether_core SOURCES)
    add_executable(aether_contextual_sampler_atomic_test tests/test_contextual_sampler_atomic.cpp
        ${contextual_sampler_core_sources})
    target_include_directories(aether_contextual_sampler_atomic_test PRIVATE include src)
    target_compile_definitions(aether_contextual_sampler_atomic_test PRIVATE AETHER_EXPORTS)
    set_target_properties(aether_contextual_sampler_atomic_test PROPERTIES INTERPROCEDURAL_OPTIMIZATION FALSE)
    target_compile_options(aether_contextual_sampler_atomic_test PRIVATE
        -ffp-contract=off -fno-lto -g -fno-omit-frame-pointer -fno-pie)
    target_link_options(aether_contextual_sampler_atomic_test PRIVATE -fno-lto -no-pie
        -Wl,--wrap=_ZN6aether5style19export_style_handleERKNS0_10MapVersionERKSt10shared_ptrIKNS0_16FrozenNodeRecordEEPP30aether_computed_style_handle_s
        -Wl,--wrap=_Z39accumulate_scrollable_descendant_extentPK19aether_layout_box_sbbRfS2_
        -Wl,--wrap=aether_style_map_get -Wl,--wrap=aether_computed_style_handle_clone
        -Wl,--wrap=aether_computed_style_handle_destroy
        -Wl,--wrap=__cxa_throw
        -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc)
    add_test(NAME aether.contextual-sampler-atomic COMMAND aether_contextual_sampler_atomic_test)
endif()

if(AETHERUI_BUILD_TESTS)
    add_executable(aether_inline_hanging_oom_test tests/test_inline_hanging_oom.cpp)
    target_link_libraries(aether_inline_hanging_oom_test PRIVATE AetherUI::Core ${CMAKE_DL_LIBS})
    add_test(NAME aether.inline-hanging-oom COMMAND aether_inline_hanging_oom_test)

    add_executable(aether_inline_hanging_scroll_test tests/test_inline_hanging_scroll.cpp src/layout/layout_scroll.cpp)
    target_include_directories(aether_inline_hanging_scroll_test PRIVATE src)
    target_link_libraries(aether_inline_hanging_scroll_test PRIVATE AetherUI::Core ${CMAKE_DL_LIBS})
    add_test(NAME aether.inline-hanging-scroll COMMAND aether_inline_hanging_scroll_test)

    add_executable(aether_inline_hanging_paint_test tests/test_inline_hanging_paint.cpp)
    target_link_libraries(aether_inline_hanging_paint_test PRIVATE AetherUI::Core ${CMAKE_DL_LIBS})
    add_test(NAME aether.inline-hanging-paint COMMAND aether_inline_hanging_paint_test)

    add_executable(aether_inline_hanging_contract_test tests/test_inline_hanging_contract.cpp)
    target_link_libraries(aether_inline_hanging_contract_test PRIVATE AetherUI::Core ${CMAKE_DL_LIBS})
    add_test(NAME aether.inline-hanging-contract COMMAND aether_inline_hanging_contract_test)

    add_executable(aether_inline_hanging_test tests/test_inline_hanging.cpp)
    target_link_libraries(aether_inline_hanging_test PRIVATE AetherUI::Core ${CMAKE_DL_LIBS})
    add_test(NAME aether.inline-hanging COMMAND aether_inline_hanging_test)

    add_executable(aether_flex_intrinsic_width_test tests/test_flex_intrinsic_width.cpp)
    target_link_libraries(aether_flex_intrinsic_width_test PRIVATE AetherUI::Core ${CMAKE_DL_LIBS})
    add_test(NAME aether.flex-intrinsic-width COMMAND aether_flex_intrinsic_width_test)
endif()

if(AETHERUI_BUILD_TESTS)
    add_executable(aether_block_static_position_test tests/test_block_static_position.cpp)
    target_link_libraries(aether_block_static_position_test PRIVATE AetherUI::Core ${CMAKE_DL_LIBS})
    add_test(NAME aether.block-static-position COMMAND aether_block_static_position_test)

    add_executable(aether_block_static_position_lifetime_test
        tests/test_block_static_position_lifetime.cpp
        $<TARGET_OBJECTS:aether_flex_text_test_core>)
    target_include_directories(aether_block_static_position_lifetime_test PRIVATE include src)
    target_compile_definitions(aether_block_static_position_lifetime_test PRIVATE AETHER_EXPORTS)
    add_test(NAME aether.block-static-position-lifetime COMMAND aether_block_static_position_lifetime_test)
endif()

if(AETHERUI_BUILD_TESTS)
    add_executable(aether_password_presentation_test tests/test_password_presentation.cpp)
    target_link_libraries(aether_password_presentation_test PRIVATE AetherUI::Core ${CMAKE_DL_LIBS})
    add_test(NAME aether.password-presentation COMMAND aether_password_presentation_test)

    # Authentic shared core objects make process-local allocation injection
    # exercise the implementation without adding an installed ABI seam.
    add_executable(aether_password_presentation_lifetime_test
        tests/test_password_presentation_lifetime.cpp
        $<TARGET_OBJECTS:aether_flex_text_test_core>)
    target_include_directories(aether_password_presentation_lifetime_test PRIVATE include src)
    target_compile_definitions(aether_password_presentation_lifetime_test PRIVATE AETHER_EXPORTS)
    add_test(NAME aether.password-presentation-lifetime COMMAND aether_password_presentation_lifetime_test)
endif()

if(AETHERUI_BUILD_TESTS AND NOT MSVC)
    # Keep every test translation unit on the same floating-point contract as
    # the core without changing the core's benchmark-comparable compiler flags.
    get_property(fp_test_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(fp_test_target IN LISTS fp_test_targets)
        if(NOT "${fp_test_target}" STREQUAL "aether_core")
            get_target_property(fp_test_type ${fp_test_target} TYPE)
            if(fp_test_type STREQUAL "EXECUTABLE" OR fp_test_type STREQUAL "OBJECT_LIBRARY")
                get_target_property(fp_test_options ${fp_test_target} COMPILE_OPTIONS)
                if(NOT "-ffp-contract=off" IN_LIST fp_test_options)
                    target_compile_options(${fp_test_target} PRIVATE -ffp-contract=off)
                endif()
            endif()
        endif()
    endforeach()
endif()

if(AETHERUI_BUILD_TESTS)
    add_executable(aether_inline_capitalization_test tests/test_inline_capitalization.cpp)
    target_link_libraries(aether_inline_capitalization_test PRIVATE AetherUI::Core)
    add_test(NAME aether.inline-capitalization COMMAND aether_inline_capitalization_test)
    foreach(text_locale C en_US tr_TR)
        add_test(NAME aether.text-locale-${text_locale}
            COMMAND aether_inline_capitalization_test --text-locale ${text_locale})
        set_tests_properties(aether.text-locale-${text_locale} PROPERTIES RUN_SERIAL TRUE)
    endforeach()
    set_tests_properties(aether.text-locale-en_US aether.text-locale-tr_TR
        PROPERTIES SKIP_RETURN_CODE 77)

endif()

if(AETHERUI_BUILD_TESTS)
    add_executable(aether_inline_capitalization_intrinsic_test
        tests/test_inline_capitalization_intrinsic.cpp
        $<TARGET_OBJECTS:aether_flex_text_test_core>)
    target_include_directories(aether_inline_capitalization_intrinsic_test PRIVATE include src)
    target_compile_definitions(aether_inline_capitalization_intrinsic_test PRIVATE AETHER_EXPORTS)
    add_test(NAME aether.inline-capitalization-intrinsic COMMAND aether_inline_capitalization_intrinsic_test)
endif()

if(AETHERUI_BUILD_TESTS)
    add_executable(aether_flex_static_position_lifetime_test
        tests/test_flex_static_position_lifetime.cpp
        $<TARGET_OBJECTS:aether_flex_text_test_core>)
    target_include_directories(aether_flex_static_position_lifetime_test PRIVATE include src)
    target_compile_definitions(aether_flex_static_position_lifetime_test PRIVATE AETHER_EXPORTS)
    add_test(NAME aether.flex-static-position-lifetime COMMAND aether_flex_static_position_lifetime_test)
endif()

if(AETHERUI_BUILD_TESTS)
    add_executable(aether_flex_static_position_test tests/test_flex_static_position.cpp)
    target_link_libraries(aether_flex_static_position_test PRIVATE AetherUI::Core)
    add_test(NAME aether.flex-static-position COMMAND aether_flex_static_position_test)
endif()

if(AETHERUI_BUILD_TESTS)
    add_executable(aether_flex_static_position_contextual_test tests/test_flex_static_position_contextual.cpp)
    target_link_libraries(aether_flex_static_position_contextual_test PRIVATE AetherUI::Core)
    add_test(NAME aether.flex-static-position-contextual COMMAND aether_flex_static_position_contextual_test)
endif()

if(AETHERUI_BUILD_TESTS)
    foreach(kind pipeline geometry context)
        add_executable(aether_corner_radius_${kind}_test tests/test_corner_radius_${kind}.cpp)
        target_link_libraries(aether_corner_radius_${kind}_test PRIVATE AetherUI::Core ${CMAKE_DL_LIBS})
        add_test(NAME aether.corner-radius-${kind} COMMAND aether_corner_radius_${kind}_test)
    endforeach()
endif()

if(AETHERUI_BUILD_TESTS AND NOT MSVC)
    # These selected regressions are registered below the common FP policy loop.
    foreach(target aether_inline_capitalization_test aether_flex_static_position_test
            aether_flex_static_position_contextual_test aether_flex_static_position_lifetime_test)
        target_compile_options(${target} PRIVATE -ffp-contract=off)
    endforeach()
endif()

if(AETHERUI_BUILD_TESTS)
    add_executable(aether_inline_units_test tests/test_inline_units.cpp)
    target_link_libraries(aether_inline_units_test PRIVATE AetherUI::Core ${CMAKE_DL_LIBS})
    add_test(NAME aether.inline-units COMMAND aether_inline_units_test)
endif()
if(AETHERUI_BUILD_TESTS)
    add_executable(aether_inline_units_intrinsic_test tests/test_inline_units_intrinsic.cpp $<TARGET_OBJECTS:aether_flex_text_test_core>)
    target_include_directories(aether_inline_units_intrinsic_test PRIVATE include src)
    target_compile_definitions(aether_inline_units_intrinsic_test PRIVATE AETHER_EXPORTS)
    target_link_libraries(aether_inline_units_intrinsic_test PRIVATE ${CMAKE_DL_LIBS})
    add_test(NAME aether.inline-units-intrinsic COMMAND aether_inline_units_intrinsic_test)
endif()
if(AETHERUI_BUILD_TESTS)
    add_executable(aether_inline_units_oom_test tests/test_inline_units_oom.cpp)
    target_link_libraries(aether_inline_units_oom_test PRIVATE AetherUI::Core ${CMAKE_DL_LIBS})
    add_test(NAME aether.inline-units-oom COMMAND aether_inline_units_oom_test)
endif()
if(AETHERUI_BUILD_TESTS AND NOT MSVC)
    foreach(target aether_inline_units_test aether_inline_units_intrinsic_test aether_inline_units_oom_test)
        target_compile_options(${target} PRIVATE -ffp-contract=off)
    endforeach()
endif()

if(AETHERUI_BUILD_TESTS)
    add_executable(aether_html_data_newlines_test tests/test_html_data_newlines.cpp)
    target_link_libraries(aether_html_data_newlines_test PRIVATE AetherUI::Core ${CMAKE_DL_LIBS})
    add_test(NAME aether.html-data-newlines COMMAND aether_html_data_newlines_test)
    if(NOT MSVC)
        target_compile_options(aether_html_data_newlines_test PRIVATE -ffp-contract=off)
    endif()
endif()

if(AETHERUI_BUILD_TESTS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(aether_html_data_newlines_oom_test tests/test_html_data_newlines_oom.cpp)
    target_link_libraries(aether_html_data_newlines_oom_test PRIVATE AetherUI::Core)
    add_test(NAME aether.html-data-newlines-oom COMMAND aether_html_data_newlines_oom_test)
endif()

if(AETHERUI_BUILD_TESTS)
    add_executable(aether_html_source_newlines_test tests/test_html_source_newlines.cpp)
    target_link_libraries(aether_html_source_newlines_test PRIVATE AetherUI::Core)
    add_test(NAME aether.html-source-newlines COMMAND aether_html_source_newlines_test)
endif()

if(AETHERUI_BUILD_TESTS)
    add_executable(aether_flex_inflexible_intrinsic_test tests/test_flex_inflexible_intrinsic.cpp)
    target_link_libraries(aether_flex_inflexible_intrinsic_test PRIVATE AetherUI::Core)
    add_test(NAME aether.flex-inflexible-intrinsic COMMAND aether_flex_inflexible_intrinsic_test)

    add_executable(aether_flex_inflexible_intrinsic_pure_test
        tests/test_flex_inflexible_intrinsic_pure.cpp
        src/layout/flex_resolver.cpp src/layout/intrinsic_sizing.cpp)
    target_include_directories(aether_flex_inflexible_intrinsic_pure_test PRIVATE include src/layout)
    add_test(NAME aether.flex-inflexible-intrinsic-pure COMMAND aether_flex_inflexible_intrinsic_pure_test)

    add_executable(aether_flex_inflexible_intrinsic_lifetime_test
        tests/test_flex_inflexible_intrinsic_lifetime.cpp
        $<TARGET_OBJECTS:aether_flex_text_test_core>)
    target_include_directories(aether_flex_inflexible_intrinsic_lifetime_test PRIVATE include src)
    target_compile_definitions(aether_flex_inflexible_intrinsic_lifetime_test PRIVATE AETHER_EXPORTS)
    add_test(NAME aether.flex-inflexible-intrinsic-lifetime COMMAND aether_flex_inflexible_intrinsic_lifetime_test)
endif()
