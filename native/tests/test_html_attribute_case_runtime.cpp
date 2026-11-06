// Public C ABI only. All checks and mutating calls execute with NDEBUG defined.
#include <aether/aether.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <map>
#include <vector>

namespace {
size_t checks = 0;
void require(bool ok, const char* message) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
bool near(float a, float b) { return std::fabs(a - b) < .001f; }
std::string take(char* text) {
    std::string result = text ? text : "";
    aether_string_free(text);
    return result;
}
std::string name(const char* lower, int casing) {
    std::string result(lower);
    for (size_t i = 0; i < result.size(); ++i)
        if ((casing == 1 || (casing == 2 && i % 2 == 0)) && result[i] >= 'a' && result[i] <= 'z')
            result[i] = static_cast<char>(result[i] - 'a' + 'A');
    return result;
}
aether_text_metrics_t measure(const char* text, const aether_computed_style_t*, float, void*) {
    return {text ? 8.f * static_cast<float>(std::strlen(text)) : 0.f, 16.f, 12.f};
}
struct Values {
    float width, height, opacity;
    int width_unit, height_unit;
};
Values values(aether_computed_style_handle_t handle) {
    aether_computed_style_t style{};
    require(aether_computed_style_copy(handle, &style) == AETHER_OK, "copy style snapshot");
    Values result{style.width.value, style.height.value, style.opacity,
                  static_cast<int>(style.width.unit), static_cast<int>(style.height.unit)};
    aether_computed_style_free(&style);
    return result;
}
void equal(Values a, Values b) {
    require(near(a.width, b.width) && near(a.height, b.height) && near(a.opacity, b.opacity) &&
            a.width_unit == b.width_unit && a.height_unit == b.height_unit,
            "computed values match lowercase control");
}
struct Record { std::string path; uint32_t impact; Values style; };
struct Fixture {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    aether_node_t root = nullptr;
    std::map<aether_node_t, aether_computed_style_handle_t> latest;
    std::vector<aether_computed_style_handle_t> retained;
    std::map<aether_node_t, aether_node_t> live_pseudos;
    Fixture(const std::string& html, const std::string& css) {
        require(aether_doc_create(&doc) == AETHER_OK, "create document");
        require(aether_doc_parse_html(doc, html.c_str()) == AETHER_OK, "parse HTML");
        root = aether_doc_get_document_element(doc);
        require(root != nullptr, "document root exists");
        aether_style_runtime_options_t options{};
        aether_style_environment_t environment{300, 200, 1, 16, 0, 0, 0};
        require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime) == AETHER_OK,
                "create runtime");
        int64_t sheet = 0;
        require(aether_style_runtime_add_stylesheet(runtime, css.c_str(), 0, "attribute-case.css", &sheet, nullptr) == AETHER_OK,
                "compile stylesheet once");
    }
    ~Fixture() {
        for (auto handle : retained) aether_computed_style_handle_destroy(handle);
        aether_style_runtime_destroy(runtime); aether_doc_destroy(doc);
    }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
    aether_node_t id(const char* id) const {
        auto node = aether_doc_get_element_by_id(doc, id);
        require(node != nullptr, "ID lookup finds fixture node");
        return node;
    }
    std::string path(aether_node_t node) const {
        if (node == root) return "root";
        auto parent = aether_node_get_parent(node);
        require(parent != nullptr, "updated node is connected");
        for (int32_t i = 0; i < aether_node_get_child_count(parent); ++i)
            if (aether_node_get_child(parent, i) == node) return path(parent) + "/" + std::to_string(i);
        // Public child enumeration intentionally excludes generated nodes.
        // These fixtures create only ::before, so its host path is unambiguous.
        return path(parent) + "/::before";
    }
    std::vector<Record> resolve() {
        // Resolve the document root, not only the mutated subject: :has and
        // sibling/ancestor rules need the complete affected inheritance tree.
        aether_style_update_t* updates = nullptr;
        const int32_t count = aether_style_runtime_resolve_subtree(runtime, root, &updates);
        require(count >= 0, "resolve subtree");
        std::vector<Record> result;
        live_pseudos.clear();
        for (int32_t i = 0; i < count; ++i) {
            aether_node_t node = nullptr;
            aether_computed_style_handle_t handle = nullptr;
            uint32_t impact = 0;
            require(aether_style_runtime_get_update(updates[i], &node, &handle, &impact) == AETHER_OK,
                    "read style update and impact");
            const auto node_path = path(node);
            if (node_path.size() >= 9 && node_path.substr(node_path.size() - 9) == "/::before")
                live_pseudos[aether_node_get_parent(node)] = node;
            result.push_back({node_path, impact, values(handle)});
            latest[node] = handle;
            retained.push_back(handle);
        }
        aether_style_updates_free(updates, count);
        std::sort(result.begin(), result.end(), [](const Record& a, const Record& b) { return a.path < b.path; });
        return result;
    }
    // Update handles are owned snapshots. Retain them until fixture teardown,
    // including old generated-node snapshots after their nodes disappear.
    aether_computed_style_handle_t snapshot(aether_node_t node) const {
        auto found = latest.find(node);
        require(found != latest.end(), "resolved snapshot exists");
        return found->second;
    }
    Values style(aether_node_t node) const { return values(snapshot(node)); }
    aether_rectf rect(aether_node_t node) const {
        aether_style_map_t map = nullptr;
        aether_layout_engine_t engine = nullptr;
        aether_layout_result_t layout = nullptr;
        require(aether_style_runtime_create_resolved_style_map(runtime, doc, &map) == AETHER_OK, "layout map");
        require(aether_layout_create(measure, nullptr, &engine) == AETHER_OK, "layout engine");
        require(aether_layout_layout(engine, doc, map, 300, 200, nullptr, nullptr, &layout) == AETHER_OK, "layout");
        auto box = aether_layout_result_find_primary_box(layout, node);
        require(box != nullptr, "real primary layout box");
        auto result = aether_layout_box_get_border_box(box);
        aether_layout_result_destroy(layout);
        aether_layout_destroy(engine);
        aether_style_map_destroy(map);
        return result;
    }
};
void equal(const std::vector<Record>& a, const std::vector<Record>& b) {
    require(a.size() == b.size(), "same number of style diffs as lowercase control");
    for (size_t i = 0; i < a.size(); ++i) {
        require(a[i].path == b[i].path && a[i].impact == b[i].impact,
                "same affected tree paths and exact impact bits as lowercase control");
        equal(a[i].style, b[i].style);
    }
}
void set(Fixture& f, aether_node_t node, const char* attribute, const char* value, int casing) {
    const auto version = aether_doc_get_version(f.doc);
    require(aether_element_set_attribute(node, name(attribute, casing).c_str(), value) == AETHER_OK, "set attribute");
    require(aether_doc_get_version(f.doc) == version, "attribute setter preserves explicit-resolution version contract");
    require(take(aether_element_get_attribute(node, attribute)) == value, "setter preserves exact value");
}
std::string consumer_html(int casing, const char* tag) {
    auto attr = [casing](const char* key, const char* value) { return " " + name(key, casing) + "='" + value + "'"; };
    return "<html><body><" + std::string(tag) + attr("id", "TaRget") + attr("class", "Kept") +
        attr("data-x", "Value") + attr("style", "height:23px") + attr("required", "") +
        attr("disabled", "false") + attr("tabindex", "0") + "></" + tag + "></body></html>";
}
void consumers() {
    const std::string css = "html,body{margin:0;padding:0}"
        "#TaRget{display:block;width:11px;height:10px;padding:0;border:0;margin:0;box-sizing:content-box}"
        "#TaRget.Kept[data-x=Value]{width:37px}"
        "#target,#TaRget.kept,[data-x=value]{width:91px}"
        "#TaRget:disabled{height:99px!important}";
    for (const char* tag : {"div", "input", "button", "textarea", "select"}) {
        Fixture lower(consumer_html(0, tag), css);
        auto baseline = lower.resolve();
        auto lower_node = lower.id("TaRget");
        auto lower_rect = lower.rect(lower_node);
        for (int casing = 0; casing < 3; ++casing) {
            Fixture f(consumer_html(casing, tag), css);
            equal(baseline, f.resolve());
            auto node = f.id("TaRget");
            require(!aether_doc_get_element_by_id(f.doc, "target"), "ID value remains case sensitive");
            require(aether_element_has_class(node, "Kept") && !aether_element_has_class(node, "kept"),
                    "class token values remain case sensitive");
            require(aether_element_is_required(node) && aether_element_is_disabled(node), "boolean attribute consumers");
            require(aether_element_is_focusable(node), "tabindex and control focus consumers");
            require(aether_element_get_state(node) == AETHER_STATE_NONE, "disabled attribute does not set pseudo-state");
            auto style = f.style(node);
            require(near(style.width, 37) && near(style.height, 23), "three casings give literal 37px and inline STYLE height");
            auto rect = f.rect(node);
            require(near(rect.width, 37) && near(rect.height, 23), "control layout uses actual canonicalized attributes");
            require(near(rect.x, lower_rect.x) && near(rect.y, lower_rect.y) &&
                    near(rect.width, lower_rect.width) && near(rect.height, lower_rect.height), "layout matches lowercase");
        }
    }
}
const char* mutation_css =
    "html,body{margin:0;padding:0}main{width:100px;height:70px;opacity:.1}"
    "div{display:block;width:11px;height:10px;opacity:.2}"
    "#TaRget.Kept[data-x=Value]{width:37px}"
    "#target,#TaRget.kept,[data-x=value]{width:91px}"
    "main:has(#TaRget.Kept[DATA-X=Value]){opacity:.8}"
    "#TaRget.Kept[DaTa-X=Value] + aside{width:43px}aside{height:10px}"
    "#TaRget.Kept[\\44 ATA-\\58 =Value]::before{content:'Generated';width:7px;height:9px;opacity:.6}"
    "div:disabled{opacity:.9}div:enabled{opacity:.2}";
void mutate(Fixture& f, aether_node_t host, const char* attribute, const char* value, int casing) {
    set(f, host, attribute, value, casing);
}
void mutations() {
    const char* html = "<html><body><main id='outer'><div id='Old' class='Other' data-x='Other' style='height:10px'></div><aside></aside></main></body></html>";
    for (int casing = 1; casing < 3; ++casing) {
        Fixture lower(html, mutation_css), f(html, mutation_css);
        auto a = lower.id("Old"), b = f.id("Old");
        equal(lower.resolve(), f.resolve());
        auto initial = f.snapshot(b);
        const auto initial_values = values(initial);
        // Deliberately keep both runtime and sheet alive throughout every round.
        auto step = [&](const char* key, const char* value, bool matches) {
            mutate(lower, a, key, value, 0); mutate(f, b, key, value, casing);
            equal(lower.resolve(), f.resolve());
            equal(lower.style(a), f.style(b));
            require(near(f.style(b).width, matches ? 37.f : 11.f), "mutation changes live ID/class/data selector candidates");
            if (!near(f.style(f.id("outer")).opacity, matches ? .8f : .1f))
                std::fprintf(stderr, "attribute=%s value=%s expected ancestor=%.1f actual=%.1f\n", key, value,
                             matches ? .8f : .1f, f.style(f.id("outer")).opacity);
            require(near(f.style(f.id("outer")).opacity, matches ? .8f : .1f), "ancestor :has resolves from root");
            const int count = aether_node_get_child_count(b);
            require(count == 0, "public authored child enumeration excludes generated pseudo");
            require(f.live_pseudos.count(b) == (matches ? 1u : 0u), "real generated ::before updates appear and disappear");
            if (matches) {
                auto pseudo = f.live_pseudos.at(b);
                require(aether_node_get_parent(pseudo) == b, "pseudo belongs to live host");
                require(near(f.style(pseudo).width, 7) && near(f.style(pseudo).height, 9), "pseudo style does not inherit host inline STYLE");
                require(aether_node_get_child_count(pseudo) == 1, "pseudo has one generated text child");
                auto text = aether_node_get_child(pseudo, 0);
                require(aether_node_get_type(text) == AETHER_NODE_TEXT && aether_node_get_parent(text) == pseudo,
                        "pseudo generated text has reciprocal parent link");
                require(take(aether_node_get_text_content(text)) == "Generated", "pseudo has real generated content");
                require(f.id("TaRget") == b, "copied pseudo ID never wins ID index");
                require(take(aether_element_get_attribute(pseudo, "DATA-X")) == "Value", "pseudo copied attributes use canonical keys");
            }
            equal(initial_values, values(initial)); // Retained snapshot never follows mutations.
            equal(lower.resolve(), f.resolve()); // Stable rerun, including pseudo regeneration.
        };
        step("id", "TaRget", false);
        step("class", "Kept", false);
        step("data-x", "Value", true);
        auto matched = f.snapshot(b);
        const auto matched_values = values(matched);
        auto pseudo_snapshot = f.snapshot(f.live_pseudos.at(b));
        const auto pseudo_values = values(pseudo_snapshot);
        step("id", "Other", false); step("id", "TaRget", true);
        step("class", "Other", false); step("class", "Kept", true);
        step("data-x", "Other", false); step("data-x", "Value", true);
        step("style", "height:29px", true);
        require(near(f.style(b).height, 29), "STYLE mutation recompiles inline style");
        step("style", "height:10px", true); step("style", "height:29px", true);
        equal(matched_values, values(matched)); equal(pseudo_values, values(pseudo_snapshot));
        const auto attributes = aether_element_get_attribute_count(b);
        step("data-x", "Value", true); // Equivalent spelling and identical value.
        require(aether_element_get_attribute_count(b) == attributes, "equivalent setter does not grow attribute map");
        step("disabled", "false", true);
        require(aether_element_is_disabled(b) && aether_element_get_state(b) == AETHER_STATE_NONE && near(f.style(b).opacity, .2f),
                "disabled presence stays separate from :disabled state, including mutation");
    }
}
void duplicate_ids() {
    for (int casing = 0; casing < 3; ++casing) {
        Fixture f("<html><body><section id='parent'><div id='Old'></div><div id='Old'></div></section></body></html>",
                  "div::before{content:'copy'}");
        auto parent = f.id("parent");
        auto first = aether_node_get_child(parent, 0), second = aether_node_get_child(parent, 1);
        f.resolve();
        require(f.id("Old") == first, "duplicate ID returns first real preorder element");
        set(f, first, "id", "TaRget", casing); f.resolve();
        require(f.id("Old") == second && f.id("TaRget") == first, "old duplicate remains after uppercase ID replacement");
        require(!aether_doc_get_element_by_id(f.doc, "target"), "ID lookup does not fold value");
        set(f, first, "id", "Old", casing); f.resolve();
        require(f.id("Old") == first, "restored duplicate wins preorder");
        require(aether_node_remove_child(parent, first) == AETHER_OK, "detach first duplicate");
        f.resolve();
        require(f.id("Old") == second, "detached host and copied pseudo IDs excluded");
        require(aether_node_append_child(parent, first) == AETHER_OK, "reconnect first at end");
        f.resolve();
        require(f.id("Old") == second && aether_node_get_parent(first) == parent, "reconnected order is authoritative");
        require(aether_node_append_child(parent, second) == AETHER_OK, "move second to end");
        f.resolve();
        require(f.id("Old") == first, "preorder reflects move without reparsing or recreating runtime");
    }
}
} // namespace
int main() {
    consumers();
    mutations();
    duplicate_ids();
#ifdef NDEBUG
    const char* release_checks = "yes";
#else
    const char* release_checks = "no";
#endif
    std::printf("HTML attribute runtime: %zu checks, 0 failures; NDEBUG=%s\n", checks, release_checks);
    return 0;
}
