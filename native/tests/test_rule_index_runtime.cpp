// The TU includes the opaque runtime solely for transaction/certificate assertions.
// All resolutions and style observations still use the public contextual ABI.
#include "../src/style_runtime/style_runtime_core.cpp"
#include <cstdio>
#include <cstdlib>
#include <new>

static long fail_after = -1;
static bool fault_delivered = false;
static size_t ordinary_test_calls = 0, ordinary_test_failures = 0;
void* operator new(std::size_t n) {
    ++ordinary_test_calls;
    if (fail_after >= 0 && fail_after-- == 0) {
        fault_delivered = true; ++ordinary_test_failures; throw std::bad_alloc();
    }
    if (auto p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { try { return ::operator new(n); } catch (...) { return nullptr; } }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { try { return ::operator new[](n); } catch (...) { return nullptr; } }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
#include "fault_inject_aligned.h"
#include "fault_inject_pmr_contract.h"

namespace {
unsigned checks = 0;
void check(bool ok, const char* label) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL rule-index runtime: %s\n", label); std::exit(1); }
}
std::string classes() {
    std::string result;
    for (int i = 0; i < 16; ++i) result += "c" + std::to_string(i) + " ";
    return result + "c7 c8 c0 c15 c8 missing c7";
}
std::string class_rules() {
    std::string result;
    for (int i = 0; i < 16; ++i) result += ".c" + std::to_string(i) + "{opacity:" + std::to_string(.1 + .02 * i) + "}";
    return result;
}
struct Fixture {
    aether_doc_t doc{};
    aether_style_runtime_t rt{};
    aether_node_t node{};
    aether_style_environment_t env{800, 600, 1, 16, 0, 0, 0};
    std::unordered_set<media_condition_s*> media;
    void keep_media(media_condition_s* value) {
        if (!value || value == media_condition_always() || value == media_condition_never() ||
            value == media_condition_unknown() || !media.insert(value).second) return;
        keep_media(value->left); keep_media(value->right); keep_media(value->inner);
    }
    Fixture() {
        check(aether_doc_create(&doc) == AETHER_OK, "document");
        check(aether_doc_parse_html(doc, "<html><body><div id='x'><span id='child'></span></div></body></html>") == AETHER_OK, "DOM");
        node = aether_doc_get_element_by_id(doc, "x");
        check(aether_style_runtime_create(nullptr, &env, nullptr, nullptr, &rt) == AETHER_OK, "runtime");
        set_classes(classes());
    }
    ~Fixture() {
        aether_style_runtime_destroy(rt); aether_doc_destroy(doc);
        // Successful compiler media graphs are currently raw-owned. Retain their
        // identities across sheet removals, then release each shallow node once.
        for (auto* value : media) delete value;
    }
    void set_classes(const std::string& value) { check(aether_element_set_attribute(node, "class", value.c_str()) == AETHER_OK, "set classes"); }
    int64_t add(const std::string& css, int layer = 2) {
        int64_t handle = -1;
        check(aether_style_runtime_add_stylesheet(rt, css.c_str(), layer, "index", &handle, nullptr) == AETHER_OK, "add sheet");
        check(rt->index.has_disjoint_runtime_rules(), "committed runtime index certified");
        for (const auto& rule : rt->sheets.at(handle)->rules) keep_media(rule.condition);
        return handle;
    }
    void generic() { rule_index_s copy(rt->index); rt->index = std::move(copy); check(!rt->index.has_disjoint_runtime_rules(), "generic copy revokes certificate"); }
    aether_style_revision_t resolve() {
        aether_style_transaction_t tx{};
        check(aether_style_runtime_prepare_revision(rt, doc, &env, &tx) == AETHER_OK, "prepare");
        check(tx->candidate->index.has_disjoint_runtime_rules() == rt->index.has_disjoint_runtime_rules(), "trusted staged copy preserves certificate");
        aether_style_revision_t result{};
        check(aether_style_runtime_commit_revision(rt, tx, &result) == AETHER_OK, "commit");
        aether_style_transaction_destroy(tx);
        return result;
    }
};
struct Observation {
    std::string node;
    uint32_t impact{};
    bool has_style{};
    float opacity{}, width{}, height{}, font_size{}, margin{}, color[4]{}, background[4]{};
    int width_units{}, height_units{};
    std::vector<std::pair<std::string, std::string>> custom;
    bool operator==(const Observation&) const = default;
};
std::string node_key(aether_node_t node) {
    std::string result = node->tag_name;
    if (auto it = node->attributes.find("id"); it != node->attributes.end()) result += "#" + it->second;
    result += ":" + std::to_string(static_cast<int>(node->pseudo_element));
    if (node->is_generated && !node->children.empty()) result += ":" + node->children.front()->text;
    return result;
}
std::vector<Observation> observe(aether_style_revision_t revision, bool changes) {
    std::vector<Observation> result;
    const int count = changes ? aether_style_revision_change_count(revision) : aether_style_revision_style_count(revision);
    for (int i = 0; i < count; ++i) {
        aether_node_t node{}; aether_computed_style_handle_t handle{}; Observation item;
        check((changes ? aether_style_revision_get_change : aether_style_revision_get_style)(revision, i, &node, &handle, &item.impact) == AETHER_OK, "public revision entry");
        item.node = node_key(node); item.has_style = handle != nullptr;
        if (handle) {
            aether_computed_style_t style{};
            check(aether_computed_style_copy(handle, &style) == AETHER_OK, "public style copy");
            item.opacity = style.opacity; item.width = style.width.value; item.height = style.height.value;
            item.width_units = static_cast<int>(style.width.unit); item.height_units = static_cast<int>(style.height.unit);
            item.font_size = style.font_size; item.margin = style.margin.top.value;
            item.color[0] = style.color.r; item.color[1] = style.color.g; item.color[2] = style.color.b; item.color[3] = style.color.a;
            item.background[0] = style.background_color.r; item.background[1] = style.background_color.g;
            item.background[2] = style.background_color.b; item.background[3] = style.background_color.a;
            for (auto* scope = handle->record->custom_properties().get(); scope; scope = scope->parent())
                for (const auto& entry : scope->local_entries()) item.custom.emplace_back(entry.name(), entry.value());
            aether_computed_style_free(&style);
        }
        aether_computed_style_handle_destroy(handle); result.push_back(std::move(item));
    }
    return result;
}
// Compare the complete scalar/payload manifest and contextual sidecars, not
// struct bytes (which contain padding and owned pointers). The only normalized
// field is the document identity, necessarily distinct in the two fixtures.
void compare_full(aether_style_revision_t a, aether_style_revision_t b) {
    const auto count = aether_style_revision_style_count(a);
    check(count == aether_style_revision_style_count(b), "full style count");
    for (int i = 0; i < count; ++i) {
        aether_computed_style_handle_t left{}, right{};
        check(aether_style_revision_get_style(a, i, nullptr, &left, nullptr) == AETHER_OK, "full left entry");
        check(aether_style_revision_get_style(b, i, nullptr, &right, nullptr) == AETHER_OK, "full right entry");
        const auto& m = *right->measures; const auto& scalars = m.scalars();
        const auto& d = m.document().values();
        auto document = std::make_shared<document_measure_context_s>();
        document->viewport_width = d.viewport_width; document->viewport_height = d.viewport_height;
        document->initial_font_size = d.initial_font_size; document->root_font_size = d.root_font_size;
        document->generation = d.generation; document->identity = left->measures->document().values().identity;
        measure_state_s measure; measure.document = document;
        measure.parent_font_size = scalars.parent_font_size; measure.self_font_size = scalars.self_font_size;
        switch (scalars.line_height_kind) {
        case FrozenLineHeightKind::normal: measure.line_height.kind = measure_line_height_kind_e::normal; break;
        case FrozenLineHeightKind::number: measure.line_height.kind = measure_line_height_kind_e::number; break;
        case FrozenLineHeightKind::absolute_css_px: measure.line_height.kind = measure_line_height_kind_e::absolute_css_px; break;
        }
        measure.line_height.value = scalars.line_height_value;
        measure.row_gap = scalars.row_gap; measure.column_gap = scalars.column_gap; measure.dependencies = scalars.dependencies;
        measure.background_positions.clear(); measure.background_sizes.clear();
        for (const auto& v : m.background_positions()) measure.background_positions.push_back({v.x, v.y});
        for (const auto& v : m.background_sizes()) measure.background_sizes.push_back({v.kind, v.x, v.y});
        for (const auto& v : m.property_dependencies()) measure.property_dependencies.emplace(std::string(v.name().c_str(), v.name().size()), v.flags());
        color_resolution_state_s colors; const auto& c = *right->record->color_references();
        colors.background_uses_current_color = c.background();
        for (size_t j = 0; j < 4; ++j) colors.border_uses_current_color[j] = c.border(j);
        for (size_t j = 0; j < c.text_shadow_count(); ++j) colors.text_shadow_uses_current_color.push_back(c.text_shadow_at(j));
        for (size_t j = 0; j < c.box_shadow_count(); ++j) colors.box_shadow_uses_current_color.push_back(c.box_shadow_at(j));
        const auto diff = classify_style_diff(left->record->style_view(), *left->measures,
            *left->record->color_references(), right->style, measure, colors);
        check(diff.style_storage_equivalent && !diff.unclassified && !diff.metadata_changed &&
            !diff.inherited_metadata_changed && diff.own_impact == 0 && diff.inherited_value_impact == 0,
            "full scalar, payload, measure and color equivalence");
        aether_computed_style_handle_destroy(left); aether_computed_style_handle_destroy(right);
    }
}
void compare(Fixture& fast, Fixture& oracle) {
    oracle.generic();
    auto a = fast.resolve(), b = oracle.resolve();
    compare_full(a, b);
    check(observe(a, false) == observe(b, false), "exact projected values and complete entry impacts");
    check(observe(a, true) == observe(b, true), "exact change order, values and masks");
    aether_style_revision_destroy(a); aether_style_revision_destroy(b);
}
void differential() {
    Fixture fast, oracle;
    const std::string base = "*{opacity:.1;color:red}DIV{opacity:.2;width:11px}body{--w:27px}"
        ".c0,.c1{opacity:.3;width:var(--w)}#x{opacity:.8}#x.c15{height:17px}" + class_rules();
    const std::string conditional = "@media(min-width:900px){.c8{width:43px}}"
        ".c7::before,.c8::after{content:'candidate';color:blue}.c0{--w:31px}";
    fast.add(base); oracle.add(base);
    auto extra = fast.add(conditional), extra_oracle = oracle.add(conditional);
    compare(fast, oracle); compare(fast, oracle);
    fast.set_classes("c8 c7 c8 c7"); oracle.set_classes("c8 c7 c8 c7"); compare(fast, oracle);
    fast.env.viewport_width = oracle.env.viewport_width = 1000; compare(fast, oracle);
    fast.set_classes(classes()); oracle.set_classes(classes()); compare(fast, oracle);
    auto layer = fast.add("#x{opacity:.6;width:52px}", 1), layer_oracle = oracle.add("#x{opacity:.6;width:52px}", 1);
    compare(fast, oracle);
    aether_style_runtime_clear_cache(fast.rt); aether_style_runtime_clear_cache(oracle.rt); compare(fast, oracle);
    check(aether_style_runtime_remove_stylesheet(fast.rt, extra), "remove fast conditional");
    check(aether_style_runtime_remove_stylesheet(oracle.rt, extra_oracle), "remove oracle conditional"); compare(fast, oracle);
    check(aether_style_runtime_remove_stylesheet(fast.rt, layer), "remove fast layer");
    check(aether_style_runtime_remove_stylesheet(oracle.rt, layer_oracle), "remove oracle layer"); compare(fast, oracle);
    fast.add(conditional); oracle.add(conditional); compare(fast, oracle);
    fast.set_classes("missing"); oracle.set_classes("missing"); compare(fast, oracle);
}
struct Saved {
    int64_t next_handle;
    int32_t next_rule;
    uint64_t epoch, generation;
    size_t sheets;
    std::shared_ptr<const style_revision_s> revision;
    std::unordered_map<aether_node_s*, FrozenNodeRef> states;
    std::vector<const compiled_rule_s*> candidates;
    bool certified;
    explicit Saved(Fixture& f) : next_handle(f.rt->next_sheet_handle), next_rule(f.rt->next_rule_id),
        epoch(f.rt->sheet_epoch), generation(f.rt->generation), sheets(f.rt->sheets.size()),
        revision(f.rt->revision), states(f.rt->states), certified(f.rt->index.has_disjoint_runtime_rules()) {
        auto info = get_node_info(f.node); rule_index_get_candidates(&f.rt->index, &info, candidates);
    }
    void unchanged(Fixture& f) const {
        check(f.rt->next_sheet_handle == next_handle && f.rt->next_rule_id == next_rule, "OOM keeps both counters");
        check(f.rt->sheet_epoch == epoch && f.rt->generation == generation && f.rt->sheets.size() == sheets, "OOM keeps epoch, generation and sheets");
        check(f.rt->revision == revision && f.rt->states == states, "OOM keeps revision and exact cached records");
        check(f.rt->index.has_disjoint_runtime_rules() == certified, "OOM keeps committed certification");
        auto info = get_node_info(f.node); std::vector<const compiled_rule_s*> now;
        rule_index_get_candidates(&f.rt->index, &info, now);
        check(now == candidates, "OOM keeps exact committed candidates");
    }
};
void oom_sheets(bool removing) {
    Fixture f, oracle; const auto base = f.add(class_rules()); oracle.add(class_rules());
    const auto added_css = std::string("#x{width:59px}.c0,.c8{height:29px}");
    const auto removable = removing ? f.add(added_css) : 0;
    const auto oracle_removable = removing ? oracle.add(added_css) : 0;
    auto old = f.resolve(), oracle_old = oracle.resolve(); Saved saved(f);
    if (removing) check(aether_style_runtime_remove_stylesheet(oracle.rt, oracle_removable), "oracle removal");
    else oracle.add(added_css);
    oracle.generic(); auto expected = oracle.resolve();
    aether_style_transaction_t stale{};
    check(aether_style_runtime_prepare_revision(f.rt, f.doc, nullptr, &stale) == AETHER_OK, "pre-change transaction");
    unsigned failures = 0; bool done = false;
    for (long allocation = 0; allocation < 20000; ++allocation) {
        int64_t handle = -77; int32_t count = -88;
        fail_after = allocation; fault_delivered = false;
        const auto status = removing ? (aether_style_runtime_remove_stylesheet(f.rt, removable) ? AETHER_OK : AETHER_ERROR_OUT_OF_MEMORY)
            : aether_style_runtime_add_stylesheet(f.rt, added_css.c_str(), 2, "index", &handle, &count);
        fail_after = -1;
        if (status == AETHER_OK) {
            check(!fault_delivered, "successful sheet change does not swallow OOM");
            if (!removing) check(handle == saved.next_handle && count == 3, "successful retry consumes handle once");
            done = true; break;
        }
        check(status == AETHER_ERROR_OUT_OF_MEMORY && fault_delivered, "sheet failure is injected OOM");
        check(handle == -77 && count == -88, "sheet OOM leaves outputs unchanged");
        saved.unchanged(f); ++failures;
    }
    check(done && failures > 16, "sheet sweep reaches build and certificate allocations");
    check(f.rt->sheet_epoch == saved.epoch + 1 && f.rt->states.empty(), "success publishes once and clears cache");
    check(f.rt->index.has_disjoint_runtime_rules(), "rebuild certifies after final allocation");
    aether_style_revision_t rejected = reinterpret_cast<aether_style_revision_t>(1);
    check(aether_style_runtime_commit_revision(f.rt, stale, &rejected) == AETHER_ERROR_INVALID_STATE && !rejected, "sheet epoch rejects stale transaction");
    aether_style_transaction_destroy(stale);
    auto current = f.resolve();
    compare_full(current, expected);
    const auto observed = observe(current, false);
    check(observed == observe(expected, false), "sheet retry exact values and impacts");
    check(observe(current, true) == observe(expected, true), "sheet retry exact changes and masks");
    bool found = false;
    for (const auto& item : observed) if (item.node == "div#x:0") { found = true; check(removing || item.width == 59, "retry resolves complete new sheet"); }
    check(found && f.rt->sheets.count(base), "surviving rules still owned");
    aether_style_revision_destroy(current); aether_style_revision_destroy(old);
    aether_style_revision_destroy(oracle_old); aether_style_revision_destroy(expected);
    std::printf("rule index %s OOM points: %u\n", removing ? "remove" : "add", failures);
}
void oom_prepare() {
    const auto family = pmr_fault_test::qualify(check);
    Fixture f, oracle; f.add(class_rules()); oracle.add(class_rules());
    auto old = f.resolve(), oracle_old = oracle.resolve();
    f.set_classes("c0 c0"); oracle.set_classes("c0 c0");
    check(aether_element_set_attribute(f.node, "style", "width:var(--width,71px);--width:73px") == AETHER_OK, "candidate style");
    check(aether_element_set_attribute(oracle.node, "style", "width:var(--width,71px);--width:73px") == AETHER_OK, "oracle style");
    // Restore all sixteen class buckets, including duplicates around overflow.
    f.set_classes(classes()); oracle.set_classes(classes());
    oracle.generic(); auto expected = oracle.resolve(); Saved saved(f);
    const pmr_fault_test::Counters start;
    unsigned failures = 0; bool done = false;
    for (long allocation = 0; allocation < 20000; ++allocation) {
        aether_style_transaction_t tx = reinterpret_cast<aether_style_transaction_t>(1);
        fail_after = allocation; fault_delivered = false;
        const auto status = aether_style_runtime_prepare_revision(f.rt, f.doc, &f.env, &tx);
        fail_after = -1;
        if (status == AETHER_OK) {
            check(!fault_delivered && tx->candidate->index.has_disjoint_runtime_rules(), "successful trusted copy and candidate");
            aether_style_revision_t revision{};
            check(aether_style_runtime_commit_revision(f.rt, tx, &revision) == AETHER_OK, "successful retry commit");
            compare_full(revision, expected);
            check(observe(revision, false) == observe(expected, false), "OOM retry exact values and impacts");
            check(observe(revision, true) == observe(expected, true), "OOM retry exact update list and masks");
            aether_style_revision_destroy(revision); aether_style_transaction_destroy(tx); done = true; break;
        }
        check(status == AETHER_ERROR_OUT_OF_MEMORY && fault_delivered && !tx, "prepare failure resets output");
        saved.unchanged(f); ++failures;
    }
    check(done && failures > 100, "prepare sweep includes copy, output, overflow and frozen storage");
    pmr_fault_test::require_sweep_coverage(family, start, check);
    aether_style_revision_destroy(old); aether_style_revision_destroy(oracle_old); aether_style_revision_destroy(expected);
    std::printf("rule index prepare OOM points: %u\n", failures);
}
} // namespace
int main() {
    differential(); oom_sheets(false); oom_sheets(true); oom_prepare();
    std::printf("rule index runtime: %u checks passed\n", checks);
}
