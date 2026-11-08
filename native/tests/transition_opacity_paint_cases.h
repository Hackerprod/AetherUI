#ifndef AETHER_TRANSITION_OPACITY_PAINT_CASES_H
#define AETHER_TRANSITION_OPACITY_PAINT_CASES_H
#include <string>

// Included after require(), near(), and measure_text() in test_transition_runtime.cpp.
// No contextual samples: the unchanged legacy layout must consume the current
// style map after observe(), without an intervening tick or layout pass.
struct opacity_paint_sample {
    bool found = false;
    aether_rectf rect{};
    aether_color_rgba color{};
};

static opacity_paint_sample opacity_paint_find(aether_display_list_t list, bool child) {
    opacity_paint_sample result{};
    for (int32_t i = 0; i < aether_display_list_get_count(list); ++i) {
        aether_paint_command_t command{};
        require(aether_display_list_get_command(list, i, &command) == AETHER_OK,
            "opacity paint: command read failed");
        if (command.tag == AETHER_PAINT_FILL_ROUNDED_RECT) {
            const auto& fill = command.data.fill_rounded_rect;
            // Distinct geometry AND RGB identify the owner and its descendant,
            // independently of canvas/background command ordering and alpha.
            const bool matches = child
                ? near(fill.rect.width, 30.f) && near(fill.rect.height, 20.f) &&
                  near(fill.color.r, 0.f) && near(fill.color.g, 0.f) && near(fill.color.b, 1.f)
                : near(fill.rect.width, 100.f) && near(fill.rect.height, 60.f) &&
                  near(fill.color.r, 1.f) && near(fill.color.g, 0.f) && near(fill.color.b, 0.f);
            if (matches) {
                require(!result.found, "opacity paint: duplicate fixture background");
                result = {true, fill.rect, fill.color};
            }
        }
        aether_paint_command_free(&command);
    }
    return result;
}

static void opacity_paint_compare(const opacity_paint_sample& actual,
                                  const opacity_paint_sample& expected) {
    require(actual.found && expected.found, "opacity paint: expected colored rectangle missing");
    require(near(actual.rect.x, expected.rect.x, 0.0001f) &&
            near(actual.rect.y, expected.rect.y, 0.0001f) &&
            near(actual.rect.width, expected.rect.width, 0.0001f) &&
            near(actual.rect.height, expected.rect.height, 0.0001f),
        "opacity paint: cancellation changed geometry");
    require(near(actual.color.r, expected.color.r, 0.0001f) &&
            near(actual.color.g, expected.color.g, 0.0001f) &&
            near(actual.color.b, expected.color.b, 0.0001f) &&
            near(actual.color.a, expected.color.a, 0.0001f),
        "opacity paint: canceled paint differs from detached-runtime paint");
}

static void opacity_paint_resolve(aether_doc_t doc, const char* css,
                                  aether_style_runtime_t* runtime,
                                  aether_style_map_t* styles) {
    aether_style_runtime_options_t options{};
    aether_style_environment_t environment{240.f, 160.f, 1.f, 16.f, 0, 0, 0};
    require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, runtime) == AETHER_OK,
        "opacity paint: style runtime creation failed");
    int64_t sheet = 0;
    require(aether_style_runtime_add_stylesheet(*runtime, css, 0, "opacity-paint.css", &sheet, nullptr) == AETHER_OK,
        "opacity paint: stylesheet creation failed");
    aether_style_update_t* updates = nullptr;
    const int32_t count = aether_style_runtime_resolve_subtree(*runtime, aether_doc_get_body(doc), &updates);
    require(count > 0, "opacity paint: style resolution failed");
    aether_style_updates_free(updates, count);
    require(aether_style_runtime_create_resolved_style_map(*runtime, doc, styles) == AETHER_OK,
        "opacity paint: resolved map creation failed");
}

static aether_computed_style_t opacity_paint_style(aether_style_map_t styles, aether_node_t node) {
    auto handle = aether_style_map_get(styles, node);
    require(handle != nullptr, "opacity paint: resolved owner style missing");
    aether_computed_style_t result{};
    require(aether_computed_style_copy(handle, &result) == AETHER_OK,
        "opacity paint: style copy failed");
    aether_computed_style_handle_destroy(handle);
    return result;
}

static void opacity_paint_case(int phase, bool zero_duration, bool fractional) {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t initial_runtime = nullptr, current_runtime = nullptr;
    aether_style_map_t initial_styles = nullptr, current_styles = nullptr;
    aether_layout_engine_t engine = nullptr;
    aether_layout_result_t layout = nullptr;
    aether_display_list_builder_t builder = nullptr;
    aether_transition_runtime_t transitions = nullptr;
    aether_display_list_t prior = nullptr, canceled = nullptr, reference = nullptr;
    require(aether_doc_create(&doc) == AETHER_OK, "opacity paint: document creation failed");
    require(aether_doc_parse_html(doc,
        "<body><div id=\"ancestor\"><div id=\"owner\"><div id=\"child\"></div></div></div></body>") == AETHER_OK,
        "opacity paint: document parsing failed");
    const auto owner = aether_doc_get_element_by_id(doc, "owner");
    require(owner != nullptr, "opacity paint: owner lookup failed");
    const float ancestor_opacity = fractional ? .5f : 1.f;
    const float current_opacity = fractional ? .6f : 1.f;
    const float delay = phase == 0 ? .2f : 0.f;
    char initial_css[1024]{};
    const char* geometry =
        "body { display: block; margin: 0; } "
        "#ancestor { display: block; width: 160px; height: 100px; } "
        "#owner { display: block; width: 100px; height: 60px; background-color: rgb(255,0,0); } "
        "#child { display: block; width: 30px; height: 20px; background-color: rgba(0,0,255,0.6); } ";
    std::snprintf(initial_css, sizeof(initial_css),
        "%s #ancestor { opacity: %.2f; } #owner { opacity: 0; transition: opacity 0.4s linear %.2fs; }",
        geometry, ancestor_opacity, delay);
    opacity_paint_resolve(doc, initial_css, &initial_runtime, &initial_styles);
    auto after = opacity_paint_style(initial_styles, owner);
    require(near(after.opacity, 0.f) && near(after.transition_duration, .4f) &&
            near(after.transition_delay, delay) &&
            (after.transition_properties & AETHER_TRANSITION_PROPERTY_OPACITY) != 0,
        "opacity paint: initial transition did not resolve");
    auto before = opacity_paint_style(initial_styles, owner);
    before.opacity = .8f;
    require(aether_layout_create(measure_text, nullptr, &engine) == AETHER_OK,
        "opacity paint: layout engine creation failed");
    require(aether_layout_layout(engine, doc, initial_styles, 240.f, 160.f, nullptr, nullptr, &layout) == AETHER_OK,
        "opacity paint: layout failed");
    require(aether_display_list_builder_create(measure_text, nullptr, &builder) == AETHER_OK,
        "opacity paint: builder creation failed");
    require(aether_transition_runtime_create(&transitions) == AETHER_OK,
        "opacity paint: transition runtime creation failed");
    aether_display_list_builder_set_transition_runtime(builder, transitions);
    require(aether_transition_runtime_observe(transitions, owner, &before, &after, 0) == AETHER_OK,
        "opacity paint: initial observe failed");
    const int64_t now = phase == 0 ? 100 : phase == 1 ? 200 : 500;
    require(aether_transition_runtime_tick(transitions, now) == (phase == 2 ? AETHER_FALSE : AETHER_TRUE),
        "opacity paint: wrong initial transition phase");
    require(aether_display_list_build_with_styles(builder, layout, initial_styles, nullptr, &prior) == AETHER_OK,
        "opacity paint: pre-cancel build failed");
    const auto old_owner = opacity_paint_find(prior, false);
    const auto old_child = opacity_paint_find(prior, true);
    if (phase == 2) {
        require(!old_owner.found && !old_child.found,
            "opacity paint: completed fade to zero must hide owner and descendant");
    } else {
        const float animated = phase == 0 ? .8f : .4f;
        require(old_owner.found && old_child.found,
            "opacity paint: pending/active background missing");
        require(near(old_owner.color.a, ancestor_opacity * animated, .0001f) &&
                near(old_child.color.a, .6f * ancestor_opacity * animated, .0001f),
            "opacity paint: legacy pending/active override not visible on subtree");
    }

    char current_css[1024]{};
    std::snprintf(current_css, sizeof(current_css),
        "%s #ancestor { opacity: %.2f; } #owner { opacity: %.2f; transition: %s; }",
        geometry, ancestor_opacity, current_opacity,
        zero_duration ? "opacity 0s linear" : "none 0.4s linear");
    opacity_paint_resolve(doc, current_css, &current_runtime, &current_styles);
    auto current = opacity_paint_style(current_styles, owner);
    require(near(current.opacity, current_opacity) &&
            (zero_duration ? current.transition_duration == 0.f &&
                (current.transition_properties & AETHER_TRANSITION_PROPERTY_OPACITY) != 0
                : current.transition_duration > 0.f &&
                (current.transition_properties & AETHER_TRANSITION_PROPERTY_OPACITY) == 0),
        "opacity paint: current map must contain requested cancellation style");
    require(aether_transition_runtime_observe(transitions, owner, &after, &current, now) == AETHER_OK,
        "opacity paint: cancellation observe failed");
    // Intentionally no tick and no relayout between observe and paint.
    require(aether_display_list_build_with_styles(builder, layout, current_styles, nullptr, &canceled) == AETHER_OK,
        "opacity paint: same-cycle cancellation build failed");
    const auto actual_owner = opacity_paint_find(canceled, false);
    const auto actual_child = opacity_paint_find(canceled, true);
    require(actual_owner.found && actual_child.found,
        "opacity paint: cancellation must restore both backgrounds immediately");
    require(near(actual_owner.color.a, ancestor_opacity * current_opacity, .0001f) &&
            near(actual_child.color.a, .6f * ancestor_opacity * current_opacity, .0001f),
        "opacity paint: restored alpha must use current style and ancestor product");

    aether_display_list_builder_set_transition_runtime(builder, nullptr);
    require(aether_display_list_build_with_styles(builder, layout, current_styles, nullptr, &reference) == AETHER_OK,
        "opacity paint: detached reference build failed");
    opacity_paint_compare(actual_owner, opacity_paint_find(reference, false));
    opacity_paint_compare(actual_child, opacity_paint_find(reference, true));
    // A visible pre-cancellation frame also anchors the geometry before observe.
    if (phase != 2) {
        auto owner_geometry = old_owner;
        auto child_geometry = old_child;
        owner_geometry.color = actual_owner.color;
        child_geometry.color = actual_child.color;
        opacity_paint_compare(actual_owner, owner_geometry);
        opacity_paint_compare(actual_child, child_geometry);
    }
    aether_computed_style_free(&before);
    aether_computed_style_free(&after);
    aether_computed_style_free(&current);
    aether_display_list_destroy(prior);
    aether_display_list_destroy(canceled);
    aether_display_list_destroy(reference);
    aether_display_list_builder_destroy(builder);
    aether_transition_runtime_destroy(transitions);
    aether_layout_result_destroy(layout);
    aether_layout_destroy(engine);
    aether_style_map_destroy(initial_styles);
    aether_style_map_destroy(current_styles);
    aether_style_runtime_destroy(initial_runtime);
    aether_style_runtime_destroy(current_runtime);
    aether_doc_destroy(doc);
}

// Parser-originated endpoints and command alpha, separate from the legacy
// cancellation scenarios above. No pixels or browser parity are inferred.
static void opacity_numeric_endpoints() {
    aether_doc_t doc{};
    require(aether_doc_create(&doc)==AETHER_OK,"numeric opacity document");
    require(aether_doc_parse_html(doc,"<body><div id='owner'><div id='child'></div></div></body>")==AETHER_OK,"numeric opacity html");
    const auto node=aether_doc_get_element_by_id(doc,"owner");
    struct Case { const char* text; float expected; };
    for (const auto& c : {Case{"50%",.5f},Case{"150%",1},Case{"-20%",0},Case{"1e-7",1e-7f}}) {
        aether_style_runtime_t styles_runtime{}; aether_style_map_t styles{};
        const std::string css=std::string("#owner{opacity:")+c.text+";transition:opacity 1s linear}";
        opacity_paint_resolve(doc,css.c_str(),&styles_runtime,&styles);
        auto after=opacity_paint_style(styles,node);
        require(after.opacity==c.expected && std::isfinite(after.opacity),"numeric opacity resolved endpoint exact");
        auto before=opacity_paint_style(styles,node); before.opacity=(c.expected==0 || c.expected==1e-7f)?1:0;
        aether_transition_runtime_t transitions{};
        require(aether_transition_runtime_create(&transitions)==AETHER_OK,"numeric transition create");
        require(aether_transition_runtime_observe(transitions,node,&before,&after,0)==AETHER_OK,"numeric transition observe");
        require(aether_transition_runtime_tick(transitions,500)==AETHER_TRUE,"numeric midpoint active");
        float value=-1;
        require(aether_transition_runtime_opacity_override(transitions,node,&value)==AETHER_TRUE,"numeric midpoint override");
        require(value==(before.opacity+c.expected)*.5f,"numeric midpoint exact, including tiny positive");
        require(aether_transition_runtime_tick(transitions,1000)==AETHER_FALSE,"numeric complete");
        // Existing legacy transitions use float interpolation without endpoint
        // snapping, and ignore deltas <=1e-4. For 1 -> 1e-7 the final native
        // value is exactly 2^-23, not the parsed endpoint. Preserve that scope
        // limit while proving a tiny positive endpoint is not normalized away.
        const float legacy_end=c.expected==1e-7f ? 0x1p-23f : c.expected;
        require(aether_transition_runtime_opacity_override(transitions,node,&value)==AETHER_TRUE && value==legacy_end,"numeric endpoint existing float interpolation");
        if (c.expected==1e-7f) require(value>0,"tiny endpoint stays positive");
        after.transition_duration=0;
        require(aether_transition_runtime_observe(transitions,node,&before,&after,1100)==AETHER_OK,"numeric zero duration");
        require(aether_transition_runtime_has_active(transitions)==AETHER_FALSE,"zero duration inactive");
        aether_transition_runtime_destroy(transitions);
        aether_computed_style_free(&before); aether_computed_style_free(&after);
        aether_style_map_destroy(styles); aether_style_runtime_destroy(styles_runtime);
    }
    aether_style_runtime_t runtime{}; aether_style_map_t styles{};
    opacity_paint_resolve(doc,
        "body{display:block;margin:0}#owner{display:block;width:100px;height:60px;background:red;opacity:50%}"
        "#child{display:block;width:30px;height:20px;background:blue;opacity:50%}",&runtime,&styles);
    aether_layout_engine_t engine{}; aether_layout_result_t layout{};
    aether_display_list_builder_t builder{}; aether_display_list_t list{};
    require(aether_layout_create(measure_text,nullptr,&engine)==AETHER_OK,"numeric layout create");
    require(aether_layout_layout(engine,doc,styles,240,160,nullptr,nullptr,&layout)==AETHER_OK,"numeric layout");
    require(aether_display_list_builder_create(measure_text,nullptr,&builder)==AETHER_OK,"numeric builder");
    require(aether_display_list_build_with_styles(builder,layout,styles,nullptr,&list)==AETHER_OK,"numeric paint");
    const auto owner=opacity_paint_find(list,false), child=opacity_paint_find(list,true);
    require(owner.found && owner.color.a==.5f,"authored 50 percent opaque solid alpha");
    require(child.found && child.color.a==.25f,"parent and child percentages accumulate");
    aether_display_list_destroy(list); aether_display_list_builder_destroy(builder);
    aether_layout_result_destroy(layout); aether_layout_destroy(engine);
    aether_style_map_destroy(styles); aether_style_runtime_destroy(runtime); aether_doc_destroy(doc);
}

static void test_display_list_opacity_cancellation() {
    opacity_numeric_endpoints();
    for (int phase = 0; phase != 3; ++phase) {
        for (int reason = 0; reason != 2; ++reason) {
            for (int fractional = 0; fractional != 2; ++fractional) {
                opacity_paint_case(phase, reason != 0, fractional != 0);
            }
        }
    }
}

#endif
