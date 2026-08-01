#include <aether/aether.h>
#include "../src/style_runtime/style_property_registry_internal.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

static void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

struct update_record_s {
    aether_node_t node = nullptr;
    uint32_t impact = STYLE_IMPACT_NONE;
};

struct fixture_s {
    aether_doc_t document = nullptr;
    aether_style_runtime_t runtime = nullptr;
    aether_node_t body = nullptr;

    fixture_s(const char* markup, const char* css) {
        require(aether_doc_create(&document) == AETHER_OK, "document creation failed");
        require(aether_doc_parse_html(document, markup) == AETHER_OK, "document parse failed");
        body = aether_doc_get_body(document);
        require(body != nullptr, "body lookup failed");

        aether_style_runtime_options_t options{};
        aether_style_environment_t environment{800, 600, 1, 16, 0, 0, 0};
        require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime) == AETHER_OK,
                "style runtime creation failed");
        int64_t handle = 0;
        require(aether_style_runtime_add_stylesheet(runtime, css, 2, "style-impact.css", &handle, nullptr) == AETHER_OK,
                "stylesheet compilation failed");
        auto initial = resolve();
        (void)initial;
    }

    ~fixture_s() {
        aether_style_runtime_destroy(runtime);
        aether_doc_destroy(document);
    }

    aether_node_t id(const char* value) const {
        auto* node = aether_doc_get_element_by_id(document, value);
        require(node != nullptr, "id lookup failed");
        return node;
    }

    std::vector<update_record_s> resolve() const {
        aether_style_update_t* updates = nullptr;
        const int32_t count = aether_style_runtime_resolve_subtree(runtime, body, &updates);
        return collect_updates(updates, count);
    }

    std::vector<update_record_s> resolve_changed(const std::vector<aether_node_t>& changed) const {
        aether_style_update_t* updates = nullptr;
        const int32_t count = aether_style_runtime_resolve_state_changes(
            runtime, body, changed.data(), static_cast<int32_t>(changed.size()), &updates);
        return collect_updates(updates, count);
    }

    static std::vector<update_record_s> collect_updates(aether_style_update_t* updates, int32_t count) {
        std::vector<update_record_s> records;
        for (int32_t index = 0; index < count; ++index) {
            update_record_s record{};
            require(aether_style_runtime_get_update(updates[index], &record.node, nullptr, &record.impact) == AETHER_OK,
                    "style update lookup failed");
            records.push_back(record);
        }
        aether_style_updates_free(updates, count);
        return records;
    }
};

static uint32_t effective(uint32_t impact) {
    return impact & ~static_cast<uint32_t>(STYLE_IMPACT_INHERITED);
}

static uint32_t impact_for(const std::vector<update_record_s>& updates, aether_node_t node) {
    for (const auto& update : updates) {
        if (update.node == node) return update.impact;
    }
    require(false, "expected style update was not emitted");
    return STYLE_IMPACT_NONE;
}

static bool has_update_for(const std::vector<update_record_s>& updates, aether_node_t node) {
    for (const auto& update : updates) {
        if (update.node == node) return true;
    }
    return false;
}

static void require_only(uint32_t impact, uint32_t expected, const char* message) {
    const uint32_t actual = effective(impact);
    if ((actual & expected) != expected || (actual & ~expected) != 0) {
        std::fprintf(stderr, "impact mismatch: actual=0x%02x expected=0x%02x\n", actual, expected);
        require(false, message);
    }
}

static void paint_only_hover_does_not_escalate_to_layout() {
    fixture_s fixture(
        "<body><div id='card'><span id='child'>Child</span></div></body>",
        "#card:hover { background-color: #eeeeee; }");
    auto* card = fixture.id("card");
    auto* child = fixture.id("child");

    aether_element_set_state(card, AETHER_STATE_HOVER);
    auto updates = fixture.resolve();
    require_only(impact_for(updates, card), STYLE_IMPACT_PAINT, "paint hover impact escalated");
    require(!has_update_for(updates, child), "non-inherited paint hover changed the child style");

    aether_element_set_state(card, AETHER_STATE_NONE);
    updates = fixture.resolve();
    require_only(impact_for(updates, card), STYLE_IMPACT_PAINT, "paint hover removal impact escalated");
    require(!has_update_for(updates, child), "non-inherited paint hover removal changed the child style");
}

static void paint_only_shorthand_hover_does_not_escalate_to_layout() {
    fixture_s fixture(
        "<body><article id='card'><div><div>vitalis_health</div></div><button id='open'>Open</button></article></body>",
        "body { margin: 0; background: #0b1324; font-family: \"Segoe UI\", Arial, sans-serif; }"
        "#open { padding: 10px 16px; background: #38bdf8; color: #06233a; border: 0; border-radius: 9px; font-weight: 700; }"
        "#open:hover { background: #7dd3fc; }"
        "#card { display: flex; justify-content: space-between; align-items: center; margin-bottom: 11px; padding: 16px 18px;"
        "        background: rgba(19, 31, 57, 0.94); border: 1px solid #263d65; border-radius: 13px; }"
        "#card:hover { background: #1a2b4d; border-color: #4a78b9; }");
    auto* card = fixture.id("card");
    auto* open = fixture.id("open");

    aether_element_set_state(card, AETHER_STATE_HOVER);
    auto updates = fixture.resolve();
    require_only(impact_for(updates, card), STYLE_IMPACT_PAINT, "catalog card hover impact escalated");

    aether_element_set_state(card, AETHER_STATE_NONE);
    aether_element_set_state(open, AETHER_STATE_HOVER);
    updates = fixture.resolve();
    require_only(impact_for(updates, card), STYLE_IMPACT_PAINT, "catalog card hover removal impact escalated");
    require_only(impact_for(updates, open), STYLE_IMPACT_PAINT, "catalog button hover impact escalated");
}

static void layout_hover_reports_layout_paint_and_hittest() {
    fixture_s fixture("<body><div id='box'></div></body>", "#box:hover { width: 120px; }");
    auto* box = fixture.id("box");
    aether_element_set_state(box, AETHER_STATE_HOVER);
    const auto updates = fixture.resolve();
    require_only(impact_for(updates, box),
                 STYLE_IMPACT_LAYOUT | STYLE_IMPACT_PAINT | STYLE_IMPACT_HITTEST,
                 "width hover did not report layout/paint/hittest impact exactly");
}

static void inherited_text_layout_hover_propagates_without_full_impact() {
    fixture_s fixture(
        "<body><div id='parent'><span id='child'>Child</span></div></body>",
        "#parent:hover { font-size: 20px; }");
    auto* parent = fixture.id("parent");
    auto* child = fixture.id("child");

    aether_element_set_state(parent, AETHER_STATE_HOVER);
    const auto updates = fixture.resolve();
    const uint32_t expected = STYLE_IMPACT_LAYOUT | STYLE_IMPACT_PAINT | STYLE_IMPACT_TEXT_LAYOUT;
    require_only(impact_for(updates, parent), expected, "font-size parent impact was not semantic");
    require_only(impact_for(updates, child), expected, "inherited font-size child impact was not semantic");
}

static void cursor_hover_reports_cursor_only() {
    fixture_s fixture("<body><div id='cursor'></div></body>", "#cursor:hover { cursor: pointer; }");
    auto* cursor = fixture.id("cursor");
    aether_element_set_state(cursor, AETHER_STATE_HOVER);
    const auto updates = fixture.resolve();
    require_only(impact_for(updates, cursor), STYLE_IMPACT_CURSOR, "cursor hover impact escalated");
}

static void pointer_events_hover_reports_hittest_only() {
    fixture_s fixture("<body><div id='hit'></div></body>", "#hit:hover { pointer-events: none; }");
    auto* hit = fixture.id("hit");
    aether_element_set_state(hit, AETHER_STATE_HOVER);
    const auto updates = fixture.resolve();
    require_only(impact_for(updates, hit), STYLE_IMPACT_HITTEST, "pointer-events hover impact escalated");
}

static void incremental_state_change_scopes_subject_local_hover() {
    fixture_s fixture(
        "<body><div id='card'><span id='child'>Child</span></div></body>",
        "#card:hover { background-color: #eeeeee; }");
    auto* card = fixture.id("card");
    auto* child = fixture.id("child");

    aether_element_set_state(card, AETHER_STATE_HOVER);
    const auto updates = fixture.resolve_changed({card});
    require(updates.size() == 1, "subject-local hover should only update the changed subject subtree");
    require_only(impact_for(updates, card), STYLE_IMPACT_PAINT, "incremental hover impact escalated");
    require(!has_update_for(updates, child), "incremental hover updated an unaffected child");
}

static void incremental_state_change_falls_back_for_ancestor_state_selectors() {
    fixture_s fixture(
        "<body><div id='parent'><span id='child'>Child</span></div></body>",
        "#parent:hover #child { color: #ff0000; }");
    auto* parent = fixture.id("parent");
    auto* child = fixture.id("child");

    aether_element_set_state(parent, AETHER_STATE_HOVER);
    const auto updates = fixture.resolve_changed({parent});
    require(has_update_for(updates, child),
            "ancestor-state selector must fall back so descendants affected by :hover are updated");
    require_only(impact_for(updates, child), STYLE_IMPACT_PAINT, "ancestor-state fallback impact escalated");
}

int main() {
    paint_only_hover_does_not_escalate_to_layout();
    paint_only_shorthand_hover_does_not_escalate_to_layout();
    layout_hover_reports_layout_paint_and_hittest();
    inherited_text_layout_hover_propagates_without_full_impact();
    cursor_hover_reports_cursor_only();
    pointer_events_hover_reports_hittest_only();
    incremental_state_change_scopes_subject_local_hover();
    incremental_state_change_falls_back_for_ancestor_state_selectors();
    return 0;
}
