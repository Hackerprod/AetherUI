// Both arms execute the exact same public runtime entrypoint. Only the matcher
// call in this translation unit is redirected; production ABI has no test switch.
#include "../src/style_runtime/style_runtime_types.h"
#include "../src/style_runtime/selector_match_context.h"
struct selector_match_context_test_access {
    static bool nth_built(const selector_match_context_s& context) {
        if (!context.parents_) return false;
        for (const auto& entry : *context.parents_) if (entry.second.complete) return true;
        return false;
    }
    static bool partial(const selector_match_context_s& context) {
        return context.previous_parents_ && !context.previous_parents_->empty();
    }
    static int failure_stage(const selector_match_context_s& context) {
        if (!context.previous_parents_ || context.previous_parents_->empty()) return 0;
        for (const auto& entry : *context.previous_parents_) if (!entry.second.complete && entry.second.remaining == 0) return 2;
        return 1;
    }
    static bool built(const selector_match_context_s& context) {
        if (!context.previous_parents_) return false;
        for (const auto& entry : *context.previous_parents_) if (entry.second.complete) return true;
        return false;
    }
};
static long fail_after = -1, matcher_fail_after = -1;
static bool fault_delivered = false, inside_matcher = false;
static bool saw_complete_summary = false, saw_partial_summary = false, saw_nth_summary = false;
static bool failed_metadata = false, failed_memo = false, failed_bulk = false;
static size_t matcher_allocation_failures = 0;
static bool uncached_oracle = false;
static size_t cached_calls = 0, oracle_calls = 0;
static bool test_selector_match(const compiled_selector_s& selector, aether_node_s* node,
                                selector_match_context_s& context) {
    if (uncached_oracle) { ++oracle_calls; return selector_match(selector, node); }
    ++cached_calls;
    struct guard { guard() { inside_matcher = true; } ~guard() { inside_matcher = false; } } active;
    bool result;
    try { result = selector_match(selector, node, context); }
    catch (...) {
        if (matcher_fail_after < 0 && fault_delivered) {
            const auto stage = selector_match_context_test_access::failure_stage(context);
            if (stage == 0) failed_metadata = true;
            if (stage == 1) failed_memo = true;
            if (stage == 2) failed_bulk = true;
        }
        throw;
    }
    saw_partial_summary |= selector_match_context_test_access::partial(context);
    saw_complete_summary |= selector_match_context_test_access::built(context);
    saw_nth_summary |= selector_match_context_test_access::nth_built(context);
    return result;
}
static unsigned active_match_scopes = 0;
struct tracked_match_context_s : selector_match_context_s {
    tracked_match_context_s() { ++active_match_scopes; }
    ~tracked_match_context_s() { --active_match_scopes; }
};
#define selector_match_context_s tracked_match_context_s
#define selector_match test_selector_match
#include "../src/style_runtime/style_runtime_core.cpp"
#undef selector_match
#undef selector_match_context_s
#include <cstdio>
#include <cstdlib>
#include <new>

static size_t ordinary_test_calls = 0, ordinary_test_failures = 0;
void* operator new(std::size_t n) {
    ++ordinary_test_calls;
    if (inside_matcher && matcher_fail_after >= 0 && matcher_fail_after-- == 0) {
        fault_delivered = true; ++ordinary_test_failures; ++matcher_allocation_failures; throw std::bad_alloc();
    }
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
    if (!ok) { std::fprintf(stderr, "FAIL adjacent-cache runtime: %s\n", label); std::exit(1); }
}
using UpdateKeys = std::vector<std::pair<std::string, uint32_t>>;
UpdateKeys update_keys(aether_style_transaction_t tx);
struct Fixture {
    aether_doc_t doc{};
    aether_style_runtime_t rt{};
    aether_node_t node{};
    aether_style_environment_t env{800, 600, 1, 16, 0, 0, 0};
    bool oracle = false;
    UpdateKeys last_updates;
    explicit Fixture(bool use_oracle = false, bool wide = false) : oracle(use_oracle) {
        check(aether_doc_create(&doc) == AETHER_OK, "document");
        std::string html = "<html><body><section id='p'>text<!--comment--><i id='a'></i>gap<!--between--><div id='x'><span id='child'></span></div>gap<!--between--><i id='b'></i>gap<!--between--><span id='c'></span>";
        if (wide) for (int i = 0; i < 192; ++i) html += "<i class='filler'></i>";
        html += "</section><section id='q'><i id='d'></i></section></body></html>";
        check(aether_doc_parse_html(doc, html.c_str()) == AETHER_OK, "DOM");
        node = aether_doc_get_element_by_id(doc, "x");
        check(aether_style_runtime_create(nullptr, &env, nullptr, nullptr, &rt) == AETHER_OK, "runtime");
        set_classes("item active");
    }
    ~Fixture() {
        aether_style_runtime_destroy(rt); aether_doc_destroy(doc);
    }
    void set_classes(const std::string& value) { check(aether_element_set_attribute(node, "class", value.c_str()) == AETHER_OK, "set classes"); }
    int64_t add(const std::string& css, int layer = 2) {
        int64_t handle = -1;
        check(aether_style_runtime_add_stylesheet(rt, css.c_str(), layer, "index", &handle, nullptr) == AETHER_OK, "add sheet");
        check(rt->index.has_disjoint_runtime_rules(), "committed runtime index certified");
        return handle;
    }
    aether_style_revision_t resolve() {
        uncached_oracle = oracle;
        aether_style_transaction_t tx{};
        check(aether_style_runtime_prepare_revision(rt, doc, &env, &tx) == AETHER_OK, "prepare");
        check(tx->candidate->index.has_disjoint_runtime_rules() == rt->index.has_disjoint_runtime_rules(), "trusted staged copy preserves certificate");
        last_updates = update_keys(tx);
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
    if (!node) return "retired";
    std::string result = node->tag_name;
    if (auto it = node->attributes.find("id"); it != node->attributes.end()) result += "#" + it->second;
    result += ":" + std::to_string(static_cast<int>(node->pseudo_element));
    if (node->is_generated && !node->children.empty()) result += ":" + node->children.front()->text;
    return result;
}
UpdateKeys update_keys(aether_style_transaction_t tx) {
    UpdateKeys result;
    for (const auto& update : tx->updates) {
        aether_node_t node{}; aether_computed_style_handle_t current{}; uint32_t impact{};
        check(aether_style_runtime_get_update(update.get(), &node, &current, &impact) == AETHER_OK, "public legacy update");
        check(current && current->record == tx->candidate->states.at(node) &&
            current->revision == tx->revision && current->measures == current->record->measures(),
            "update retains complete candidate frozen record and sidecars");
        result.emplace_back(node_key(node), impact);
        aether_computed_style_handle_destroy(current);
    }
    return result;
}
std::vector<Observation> observe(aether_style_revision_t revision, bool changes) {
    std::vector<Observation> result;
    const int count = changes ? aether_style_revision_change_count(revision) : aether_style_revision_style_count(revision);
    for (int i = 0; i < count; ++i) {
        aether_node_t node{}; aether_computed_style_handle_t handle{}; Observation item;
        check((changes ? aether_style_revision_get_change : aether_style_revision_get_style)(revision, i, &node, &handle, &item.impact) == AETHER_OK, "public revision entry");
        item.node = handle && handle->document_lifetime && handle->document_lifetime->alive &&
            handle->node_lifetime && handle->node_lifetime->alive ? node_key(node) : "retired";
        item.has_style = handle != nullptr;
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
        check(left->record->declared_impact() == right->record->declared_impact() &&
            left->record->declared_inherited_impact() == right->record->declared_inherited_impact() &&
            left->record->inherited_change_impact() == right->record->inherited_change_impact(),
            "full declared/inherited contextual metadata");
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

    auto a = fast.resolve(), b = oracle.resolve();
    check(fast.last_updates == oracle.last_updates, "complete legacy update order and impact masks");
    check(aether_style_revision_generation(a) == aether_style_revision_generation(b), "revision generation");
    check(a->value->input_epoch() == b->value->input_epoch() && a->value->structure_epoch() == b->value->structure_epoch() && a->value->style_tree_epoch() == b->value->style_tree_epoch(), "revision epochs");
    compare_full(a, b);
    check(observe(a, false) == observe(b, false), "exact projected values and complete entry impacts");
    check(observe(a, true) == observe(b, true), "exact change order, values and masks");
    aether_style_revision_destroy(a); aether_style_revision_destroy(b);
}
const std::string adjacent_css = [] {
    std::string result;
    for (int i = 0; i < 12; ++i) result += "section>*+*{min-width:1px}";
    return result;
}() +
    "body{--w:27px;color:rgb(30,60,90);font-size:18px}"
    "section>*{width:var(--w);height:12px;opacity:.1}"
    "section>i+div{opacity:.8;--w:31px;margin:2px}"
    "section>div+i{height:23px;border:2px solid currentColor}"
    "section>i+div+i{width:43px}"
    "section>div+i+span{height:37px}"
    "section>:is(i+div,div+i){padding:3px}"
    "section>:not(i+div){border-radius:4px}"
    "section>:where(i+div){font-size:1.2em}"
    "section:has(i+div){--w:39px}"
    // Leading-combinator :has arguments are not supported by this parser.
    "body:has(section i+div){padding:1px}"
    "section>i+div~span{transform:translate(2px,3px)}"
    "section>i+div span{letter-spacing:2px}"
    "section>i+div::before{content:'adjacent-before';color:blue}"
    "section>div+i::after{content:'adjacent-after';color:red}"
    "section>i+.active{background:currentColor}"
    "#x{box-shadow:1px 2px 3px currentColor;text-shadow:1px 1px currentColor;"
    "background-image:linear-gradient(30deg,red,blue);background-position:20% 30%;"
    "background-size:50% auto;transition:opacity 2s ease 1s}";
const std::string mixed_css = adjacent_css +
    "section>:nth-child(n)+*:nth-child(n){padding:2px}"
    "section>:nth-child(2n)+*{opacity:.6}"
    "section>:nth-child(2)+i::before{content:'mixed'}";
void move(Fixture& f, const char* child, const char* parent) {
    check(aether_node_append_child(aether_doc_get_element_by_id(f.doc, parent),
        aether_doc_get_element_by_id(f.doc, child)) == AETHER_OK, "reparent/reorder");
}
void require_selector_coverage(Fixture& f) {
    // Every source rule must compile, and every compiled selector must match an
    // actual node in this fixture. This rejects unsupported/vacuous pseudo arms
    // even when both cached and uncached paths would agree on not matching.
    const auto& rules = f.rt->sheets.at(1)->rules;
    size_t offset = 0;
    while (offset < adjacent_css.size()) {
        const auto end = adjacent_css.find('}', offset);
        const auto brace = adjacent_css.find('{', offset);
        const auto source = adjacent_css.substr(offset, brace - offset);
        compiled_selector_s parsed;
        check(selector_parse(source.c_str(), parsed), "each source selector parses successfully");
        offset = end + 1;
    }
    check(rules.size() == static_cast<size_t>(std::count(adjacent_css.begin(), adjacent_css.end(), '{')),
        "all fixture selectors compile without silently dropping rules");
    for (const auto& rule : rules) {
        std::function<bool(aether_node_s*)> matches = [&](aether_node_s* node) {
            if (selector_match(rule.selector, node)) return true;
            for (auto* child : node->children) if (matches(child)) return true;
            return false;
        };
        check(matches(f.doc), "each fixture selector has an actual positive match");
    }
    unsigned before = 0, after = 0;
    for (const auto& entry : f.rt->states) {
        before += entry.first->is_generated && entry.first->pseudo_element == aether_pseudo_element_t::before;
        after += entry.first->is_generated && entry.first->pseudo_element == aether_pseudo_element_t::after;
    }
    check(before > 0 && after > 0, "both adjacent-generated pseudo arms exist in committed runtime");
    check(f.rt->states.at(f.node)->style_view().scalars().opacity == .8f, "adjacent positive rule changes actual runtime opacity");
}
void differential() {
    Fixture fast(false, true), oracle(true, true); fast.add(adjacent_css); oracle.add(adjacent_css);
    compare(fast, oracle);
    require_selector_coverage(fast); require_selector_coverage(oracle);
    compare(fast, oracle);
    check(cached_calls > 0 && oracle_calls > 0, "both matcher arms actually exercised");
    check(saw_complete_summary, "differential actually promotes a parent summary");
    check(saw_partial_summary, "differential actually creates partial memo metadata");
    move(fast, "a", "p"); move(oracle, "a", "p"); compare(fast, oracle);
    move(fast, "x", "q"); move(oracle, "x", "q"); compare(fast, oracle);
    move(fast, "x", "p"); move(oracle, "x", "p"); compare(fast, oracle);
    // Reconstruct a real leaf at exactly the same address between preparation
    // scopes. Frozen records retain the old identity token, never the new node.
    for (auto* f : {&fast, &oracle}) {
        auto* leaf = aether_doc_get_element_by_id(f->doc, "b");
        auto* parent = leaf->parent;
        check(leaf->children.empty(), "address-reuse fixture is a leaf");
        auto old_identity = leaf->identity;
        check(aether_node_remove_child(parent, leaf) == AETHER_OK, "detach reused address");
        leaf->~aether_node_s();
        new (leaf) aether_node_s();
        leaf->type = AETHER_NODE_ELEMENT; leaf->tag_name = "span";
        leaf->attributes["id"] = "b";
        check(!old_identity->alive && leaf->identity != old_identity, "address reused with distinct identity");
        check(aether_node_append_child(parent, leaf) == AETHER_OK, "attach reused address at new ordinal");
    }
    compare(fast, oracle);
    fast.set_classes("changed"); oracle.set_classes("changed"); compare(fast, oracle);
    for (auto* f : {&fast, &oracle}) f->add("i+div:hover{height:79px}i+.active{width:61px}");
    fast.set_classes("item active"); oracle.set_classes("item active"); compare(fast, oracle);
    check(fast.rt->states.at(fast.node)->style_view().scalars().width.value == 61.f,
        "restored class positively changes adjacent runtime width");
    for (auto* f : {&fast, &oracle}) aether_element_set_state(f->node, AETHER_STATE_HOVER);
    compare(fast, oracle);
    check(fast.rt->states.at(fast.node)->style_view().scalars().height.value == 79.f,
        "hover positively changes adjacent runtime height");
    for (auto* f : {&fast, &oracle}) aether_element_set_state(f->node, 0);
    compare(fast, oracle);
    // Detach a real element, resolve without it, then reinsert it in a new slot.
    auto* detached_fast = aether_doc_get_element_by_id(fast.doc, "c");
    auto* detached_oracle = aether_doc_get_element_by_id(oracle.doc, "c");
    check(aether_node_remove_child(detached_fast->parent, detached_fast) == AETHER_OK, "remove fast element");
    check(aether_node_remove_child(detached_oracle->parent, detached_oracle) == AETHER_OK, "remove oracle element");
    compare(fast, oracle);
    check(aether_node_append_child(aether_doc_get_element_by_id(fast.doc, "q"), detached_fast) == AETHER_OK, "insert fast element");
    check(aether_node_append_child(aether_doc_get_element_by_id(oracle.doc, "q"), detached_oracle) == AETHER_OK, "insert oracle element");
    compare(fast, oracle);
    saw_nth_summary = false;
    fast.add(mixed_css); oracle.add(mixed_css); compare(fast, oracle);
    check(saw_nth_summary && saw_complete_summary, "mixed traversal actually builds both indices");
    const auto css = "section>i+div{width:71px}section>i+div::before{content:'new'}";
    auto a = fast.add(css), b = oracle.add(css); compare(fast, oracle);
    fast.env.viewport_width = oracle.env.viewport_width = 1000; compare(fast, oracle);
    aether_style_runtime_clear_cache(fast.rt); aether_style_runtime_clear_cache(oracle.rt); compare(fast, oracle);
    check(aether_style_runtime_remove_stylesheet(fast.rt, a), "remove cached sheet");
    check(aether_style_runtime_remove_stylesheet(oracle.rt, b), "remove oracle sheet"); compare(fast, oracle);
    fast.add(css); oracle.add(css); compare(fast, oracle);
    // Retain the old revision while replacing the complete document, so frozen
    // lifetime tokens and old generated nodes remain observable independently.
    auto old_fast = fast.resolve(), old_oracle = oracle.resolve();
    for (auto* f : {&fast, &oracle}) {
        auto previous_doc = f->doc;
        check(aether_doc_create(&f->doc) == AETHER_OK, "replacement document");
        check(aether_doc_parse_html(f->doc, "<html><body><section id='p'><i></i><i id='x'></i></section></body></html>") == AETHER_OK, "replacement tree");
        f->node = aether_doc_get_element_by_id(f->doc, "x");
        aether_doc_destroy(previous_doc);
    }
    const auto retained_fast = observe(old_fast, false), retained_oracle = observe(old_oracle, false);
    compare_full(old_fast, old_oracle);
    check(retained_fast == retained_oracle, "retained old revisions remain equivalent after document replacement");
    compare(fast, oracle);
    check(retained_fast == observe(old_fast, false) && retained_oracle == observe(old_oracle, false), "new prepare does not mutate retained revision");
    aether_style_revision_destroy(old_fast); aether_style_revision_destroy(old_oracle);
}
struct TopologyEntry {
    aether_node_s* node;
    aether_node_s* parent;
    std::vector<aether_node_s*> children;
    std::vector<std::shared_ptr<aether_node_s>> generated;
    bool operator==(const TopologyEntry&) const = default;
};
std::vector<TopologyEntry> topology(aether_node_s* node) {
    std::vector<TopologyEntry> result{{node,node->parent,node->children,node->generated_storage}};
    for (auto* child : node->children) {
        auto nested = topology(child); result.insert(result.end(), nested.begin(), nested.end());
    }
    return result;
}
struct Saved {
    uint64_t generation, sheet_epoch, mutation_epoch, structure_epoch, style_tree_epoch;
    StyleRevisionRef revision;
    std::unordered_map<aether_node_s*, FrozenNodeRef> states;
    std::vector<TopologyEntry> tree;
    explicit Saved(Fixture& f) : generation(f.rt->generation), sheet_epoch(f.rt->sheet_epoch),
        mutation_epoch(f.doc->identity->mutation_epoch), structure_epoch(f.doc->identity->structure_epoch),
        style_tree_epoch(f.doc->identity->style_tree_epoch), revision(f.rt->revision), states(f.rt->states), tree(topology(f.doc)) {}
    void unchanged(Fixture& f) const {
        check(generation == f.rt->generation && sheet_epoch == f.rt->sheet_epoch, "failed preparation keeps runtime epochs");
        check(mutation_epoch == f.doc->identity->mutation_epoch && structure_epoch == f.doc->identity->structure_epoch &&
            style_tree_epoch == f.doc->identity->style_tree_epoch, "failed preparation keeps document epochs");
        check(revision == f.rt->revision && states == f.rt->states, "failed preparation keeps frozen records and revision");
        check(tree == topology(f.doc), "failed preparation keeps exact topology and generated ownership");
        check(!f.rt->preparing, "preparing guard released");
    }
};
void count_callback(const char*, const char*, const char*, const char*, void* data) {
    check(active_match_scopes == 0, "diagnostic replay follows scratch destruction");
    ++*static_cast<unsigned*>(data);
}
void oom_prepare(bool mixed, bool matcher_only = false) {
    const auto family = pmr_fault_test::qualify(check);
    Fixture f(false, true), oracle(true, true);
    f.add(mixed ? mixed_css : adjacent_css); oracle.add(mixed ? mixed_css : adjacent_css);
    auto old = f.resolve(), oracle_old = oracle.resolve();
    move(f, "a", "p"); move(oracle, "a", "p");
    check(aether_element_set_attribute(f.node, "style", "width:calc(1px + 2)") == AETHER_OK, "OOM diagnostic trigger");
    check(aether_element_set_attribute(oracle.node, "style", "width:calc(1px + 2)") == AETHER_OK, "oracle diagnostic trigger");
    auto expected = oracle.resolve(); Saved saved(f);
    unsigned callback_count = 0;
    aether_style_runtime_set_diagnostics_callback(f.rt, count_callback, &callback_count);
    // A successful preparation followed by abort also publishes nothing.
    uncached_oracle = false;
    aether_style_transaction_t aborted{};
    check(aether_style_runtime_prepare_revision(f.rt, f.doc, &f.env, &aborted) == AETHER_OK, "prepare for explicit abort");
    aether_style_transaction_destroy(aborted); saved.unchanged(f);
    check(callback_count == 0, "explicit abort does not deliver callbacks");
    const pmr_fault_test::Counters start;
    failed_metadata = failed_memo = failed_bulk = false;
    unsigned failures = 0; bool done = false;
    for (long allocation = 0; allocation < 50000; ++allocation) {
        aether_style_transaction_t tx = reinterpret_cast<aether_style_transaction_t>(1);
        if (matcher_only) matcher_fail_after = allocation; else fail_after = allocation;
        fault_delivered = false;
        const auto status = aether_style_runtime_prepare_revision(f.rt, f.doc, &f.env, &tx);
        fail_after = matcher_fail_after = -1;
        if (status == AETHER_OK) {
            check(!fault_delivered, "preparation does not swallow injected allocation failure");
            check(update_keys(tx) == oracle.last_updates, "retry complete legacy update order and impacts");
            aether_style_revision_t revision{};
            check(aether_style_runtime_commit_revision(f.rt, tx, &revision) == AETHER_OK, "successful retry commit");
            check(callback_count > 0, "successful commit finally delivers diagnostics");
            compare_full(revision, expected);
            check(observe(revision, false) == observe(expected, false), "retry complete values and impacts");
            check(observe(revision, true) == observe(expected, true), "retry exact change list");
            check(aether_style_revision_generation(revision) == aether_style_revision_generation(expected), "retry increments generation exactly once");
            aether_style_revision_destroy(revision); aether_style_transaction_destroy(tx); done = true; break;
        }
        check(status == AETHER_ERROR_OUT_OF_MEMORY && fault_delivered && !tx, "prepare OOM resets output");
        saved.unchanged(f); check(callback_count == 0, "failed prepare delivers no callbacks"); ++failures;
    }
    check(done && failures > 100, "prepare allocation sweep reaches frozen output and adjacent summaries");
    if (!matcher_only) pmr_fault_test::require_sweep_coverage(family, start, check);
    else {
        check(matcher_allocation_failures > 100, "targeted sweep reaches adjacent maps and bulk allocations");
        check(failed_metadata && failed_memo && failed_bulk, "targeted sweep proves metadata, memo, and bulk failures");
    }
    aether_style_revision_destroy(old); aether_style_revision_destroy(oracle_old); aether_style_revision_destroy(expected);
    std::printf("adjacent cache prepare %s/%s OOM points: %u\n", mixed ? "mixed" : "adjacent-only", matcher_only ? "matcher" : "all", failures);
}
struct CallbackContext {
    Fixture* fixture;
    unsigned calls = 0;
    uint64_t nested_generation = 0;
};
void reentrant_callback(const char*, const char*, const char*, const char*, void* data) {
    auto& c = *static_cast<CallbackContext*>(data);
    if (c.calls++) return;
    auto& f = *c.fixture;
    check(!inside_matcher && active_match_scopes == 0 && !f.rt->preparing && f.rt->revision && f.rt->generation == 1, "callback after complete publication");
    if (!f.oracle) check(saw_complete_summary, "callback follows a real bulk-cached traversal");
    aether_style_runtime_set_diagnostics_callback(f.rt, nullptr, nullptr);
    check(aether_element_set_attribute(f.node, "style", "width:17px") == AETHER_OK, "callback fixes diagnostic value");
    move(f, "a", "p");
    auto nested = f.resolve(); c.nested_generation = aether_style_revision_generation(nested);
    aether_style_revision_destroy(nested);
}
void reentrant() {
    Fixture fast(false, true), oracle(true, true); fast.add(adjacent_css); oracle.add(adjacent_css);
    for (auto* f : {&fast, &oracle}) {
        check(aether_element_set_attribute(f->node, "style", "width:calc(1px + 2)") == AETHER_OK, "runtime diagnostic trigger");
        CallbackContext c{f}; aether_style_runtime_set_diagnostics_callback(f->rt, reentrant_callback, &c);
        saw_complete_summary = false;
        auto outer = f->resolve();
        check(c.calls > 0 && c.nested_generation == 2 && f->rt->generation == 2, "callback resolves fresh nested traversal");
        check(aether_style_revision_generation(outer) == 1, "outer retains prepared revision across reentrancy");
        aether_style_revision_destroy(outer);
    }
    compare(fast, oracle);
}
} // namespace
int main() {
    differential(); reentrant();
    oom_prepare(false); oom_prepare(true); oom_prepare(false, true); oom_prepare(true, true);
    std::printf("adjacent cache runtime: %u checks passed\n", checks);
}
