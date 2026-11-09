// White-box counter/state checks; no test setters enter the installed ABI.
// Linked with the real core sources and the small-source compiler test TU.
#include "../src/style_runtime/style_runtime_core.cpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <set>

namespace {
int checks = 0;
int limit_diagnostics = 0;
void check(bool result, const char* label) {
    ++checks;
    if (!result) { std::fprintf(stderr, "FAIL source order runtime: %s\n", label); std::exit(1); }
}
void near(float actual, float expected, const char* label) {
    check(std::fabs(actual - expected) < .0001f, label);
}
void diagnostic(const char* code, const char*, const char*, const char*, void*) {
    if (std::strcmp(code, "AETHER-CSS-LIMIT") == 0) ++limit_diagnostics;
}
void gather_media(media_condition_s* node, std::set<media_condition_s*>& nodes) {
    if (!node || node == media_condition_always() || node == media_condition_never() ||
        node == media_condition_unknown() || !nodes.insert(node).second) return;
    gather_media(node->left, nodes); gather_media(node->right, nodes); gather_media(node->inner, nodes);
}
struct fixture {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    aether_node_t node = nullptr;
    fixture() {
        check(aether_doc_create(&doc) == AETHER_OK, "document");
        check(aether_doc_parse_html(doc,
            "<body><div class='p'><div id='target' class='a'></div></div></body>") == AETHER_OK, "HTML");
        node = aether_doc_get_element_by_id(doc, "target");
        check(node != nullptr, "target");
        check(aether_style_runtime_create(nullptr, nullptr, diagnostic, nullptr, &runtime) == AETHER_OK,
            "runtime");
    }
    ~fixture() {
        std::set<media_condition_s*> nodes;
        for (const auto& sheet : runtime->sheets)
            for (const auto& rule : sheet.second->rules) gather_media(rule.condition, nodes);
        aether_style_runtime_destroy(runtime); aether_doc_destroy(doc);
        for (auto* node : nodes) delete node; // Raw successful graph ownership, shallow and unique.
    }
    int64_t add(const char* css) {
        int64_t handle = -1; int32_t count = -1;
        check(aether_style_runtime_add_stylesheet(runtime, css, 2, nullptr, &handle, &count) == AETHER_OK,
            "add valid sheet");
        return handle;
    }
    const aether::style::StyleScalars& resolve() {
        aether_style_update_t* updates = nullptr;
        const auto count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_body(doc), &updates);
        aether_style_updates_free(updates, count);
        check(runtime->states.count(node) == 1, "current resolved state");
        return runtime->states.at(node)->style_view().scalars();
    }
    aether_style_map_t map() {
        aether_style_map_t result = nullptr;
        check(aether_style_runtime_create_resolved_style_map(runtime, doc, &result) == AETHER_OK,
            "resolved map");
        check(result && result->current && result->current->backing()->find(node) != nullptr, "map contains current node");
        return result;
    }
    void reject(const char* source) {
        const auto next_handle = runtime->next_sheet_handle;
        const auto next_rule = runtime->next_rule_id;
        const auto sheets = runtime->sheets.size();
        const auto states = runtime->states.size();
        auto* current = runtime->states.at(node).get();
        const auto bucket = runtime->compiled->index.by_class.at("a");
        int64_t handle = -123; int32_t count = -456;
        const int before = limit_diagnostics;
        check(aether_style_runtime_add_stylesheet(runtime, source, 2, "rejected", &handle, &count) ==
            AETHER_ERROR_INVALID_STATE, "controlled add rejection");
        check(handle == -123 && count == -456, "rejected outputs untouched");
        check(runtime->next_sheet_handle == next_handle && runtime->next_rule_id == next_rule,
            "rejected counters untouched");
        check(runtime->sheets.size() == sheets && runtime->states.size() == states &&
            runtime->states.at(node).get() == current, "rejected sheets and cached state untouched");
        check(runtime->compiled->index.by_class.at("a") == bucket, "rejected index unchanged");
        check(limit_diagnostics == before + 1, "one limit diagnostic");
    }
};
void counter_transaction() {
    fixture f;
    f.add(".a{opacity:.4}");
    near(f.resolve().opacity, .4f, "baseline opacity");
    auto* old_map = f.map();
    f.runtime->next_rule_id = INT32_MAX - 1;
    f.reject(".a{opacity:.8}@media(min-width:0px){.a{opacity:.9}}");
    near(old_map->current->backing()->find(f.node)->style_view().scalars().opacity, .4f, "snapshot survives rejection");
    const auto last = f.add(".a{opacity:.8}");
    check(f.runtime->next_rule_id == INT32_MAX, "last ID reaches exhausted sentinel");
    check(f.runtime->sheets.at(last)->rules[0].rule_id == INT32_MAX - 1 &&
        f.runtime->sheets.at(last)->rules[0].source_order == INT32_MAX - 1, "last ID and position");
    near(f.resolve().opacity, .8f, "valid call after failed call wins");
    auto* new_map = f.map();
    near(old_map->current->backing()->find(f.node)->style_view().scalars().opacity, .4f, "old snapshot remains old");
    near(new_map->current->backing()->find(f.node)->style_view().scalars().opacity, .8f, "new map has new global winner");
    f.reject("@media print{.a{opacity:.9}}");
    const auto empty = f.add("/* empty */");
    check(f.runtime->next_rule_id == INT32_MAX && f.runtime->sheets.at(empty)->rules.empty(),
        "empty sheet succeeds without rule capacity");
    near(f.resolve().opacity, .8f, "empty rebuild retains winner");
    check(aether_style_runtime_remove_stylesheet(f.runtime, last), "remove final rule");
    near(f.resolve().opacity, .4f, "removal restores survivor");
    check(f.runtime->next_rule_id == INT32_MAX, "removal does not recycle IDs");
    f.reject(".a{opacity:.9}");
    check(aether_style_runtime_update_resolved_style_map(f.runtime, new_map, &f.node, 1) == AETHER_OK,
        "refresh map from current winner");
    near(new_map->current->backing()->find(f.node)->style_view().scalars().opacity, .4f, "refreshed map restored value");
    aether_style_map_destroy(old_map); aether_style_map_destroy(new_map);
}
void handle_transaction() {
    fixture f;
    f.add(".a{opacity:.4}"); f.resolve();
    f.runtime->next_sheet_handle = INT64_MAX - 1;
    const auto last = f.add(".a{opacity:.8}");
    check(last == INT64_MAX - 1 && f.runtime->next_sheet_handle == INT64_MAX, "last handle assigned");
    near(f.resolve().opacity, .8f, "last handle sheet works");
    f.reject("");
    check(aether_style_runtime_remove_stylesheet(f.runtime, last), "remove last handle");
    near(f.resolve().opacity, .4f, "handle exhausted runtime still resolves");
    f.reject(".a{opacity:.9}");
    f.runtime->next_sheet_handle = -1;
    f.reject("");
}
void source_and_inline_rejection() {
    fixture f;
    f.add(".p{font-size:18px}.a{opacity:.4}"); f.resolve();
    const std::string oversized = "opacity:.9;margin:9px;font-size:99px;" + std::string(129, ' ');
    f.reject((".a{" + oversized + "}").c_str());

    // Existing compiler outputs must survive a rejected block, not its prefix.
    std::vector<compiled_property_declaration_s> properties(1);
    properties[0].property_id = 77;
    std::vector<custom_property_declaration_s> custom(1); custom[0].name = "--sentinel";
    std::vector<compiled_shorthand_declaration_s> shorthands(1); shorthands[0].name = "margin";
    const auto before = limit_diagnostics;
    check(style_source_compile_inline(oversized.c_str(), properties, custom, shorthands,
        diagnostic, nullptr) == AETHER_ERROR_INVALID_STATE, "inline rejects entire source");
    check(properties.size() == 1 && properties[0].property_id == 77 && custom.size() == 1 &&
        custom[0].name == "--sentinel" && shorthands.size() == 1 && shorthands[0].name == "margin",
        "rejected inline output vectors unchanged");
    check(limit_diagnostics == before + 1, "inline limit diagnostic");
    check(aether_element_set_attribute(f.node, "style", oversized.c_str()) == AETHER_OK, "set inline");
    const auto before_resolve = limit_diagnostics;
    const auto& style = f.resolve();
    near(style.opacity, .4f, "rejected inline leaves sheet declaration");
    near(style.font_size, 18.f, "rejected inline leaves inheritance");
    near(style.margin.top.value, 0.f, "rejected inline leaves initial margin");
    check(limit_diagnostics > before_resolve, "runtime emitted inline limit diagnostic");
    check(aether_element_set_attribute(f.node, "style", "opacity:.9") == AETHER_OK, "replace inline");
    near(f.resolve().opacity, .9f, "valid inline works after rejection");
}

// Reentrant add diagnostics run only after commit/rollback. Callbacks deliberately
// inspect private state as well as public resolution, and never throw or destroy
// their runtime/user_data. Guarded nested calls cannot recursively trigger work.
struct committed_callback_context {
    fixture* f;
    int64_t* outer_handle;
    int32_t* outer_count;
    int calls = 0;
    int64_t nested = -1;
    const aether::style::FrozenNodePayload* final_state = nullptr;
    aether_style_map_t outer_map = nullptr;
    aether_style_map_t final_map = nullptr;
};
void committed_callback(const char* code, const char* property, const char* value,
                        const char* message, void* data) {
    auto& c = *static_cast<committed_callback_context*>(data);
    ++c.calls;
    check(c.calls == 1, "R2 exactly one calc diagnostic");
    check(code && std::strcmp(code, "AETHER-CSS-CALC") == 0 && property &&
          std::strcmp(property, "width") == 0 && value && message && *message,
          "R2 diagnostic payload remains valid after compiler temporaries");
    auto& f = *c.f;
    check(*c.outer_handle == 1 && *c.outer_count == 4, "R3 outputs published before callback");
    check(f.runtime->sheets.size() == 1 && f.runtime->next_sheet_handle == 2 &&
          f.runtime->next_rule_id == 5, "R3 counters and sheet committed before callback");
    const auto& rules = f.runtime->sheets.at(*c.outer_handle)->rules;
    check(rules.size() == 4, "R2 complete outer includes post-diagnostic media rule");
    const int ids[] = {1, 2, 3, 4};
    const int positions[] = {1, 1, 3, 4};
    for (size_t i = 0; i < 4; ++i)
        check(rules[i].rule_id == ids[i] && rules[i].source_order == positions[i],
              "R2 outer positions globally contiguous with shared selector-list source");
    check(f.runtime->states.empty(), "R3 old cache invalidation finished before callback");
    near(f.resolve().opacity, .4f, "R3 callback sees whole committed outer");
    c.outer_map = f.map();
    c.nested = f.add(".a{opacity:.8}");
    check(c.nested == 2 && f.runtime->next_sheet_handle == 3 && f.runtime->next_rule_id == 6,
          "R2 nested uses following counters");
    const auto& nested_rule = f.runtime->sheets.at(c.nested)->rules.at(0);
    check(nested_rule.rule_id == 5 && nested_rule.source_order == 5,
          "R2 all outer IDs precede nested ID including post-diagnostic rules");
    near(f.resolve().opacity, .8f, "R3 callback resolves nested winner");
    c.final_state = f.runtime->states.at(f.node).get();
    c.final_map = f.map();
}
void reentrant_metadata_and_cache() {
    fixture f;
    f.resolve(); // Prime states to verify invalidation precedes replay.
    int64_t outer = -123; int32_t count = -456;
    committed_callback_context c{&f, &outer, &count};
    aether_style_runtime_set_diagnostics_callback(f.runtime, committed_callback, &c);
    const char* css = ".a,.b{opacity:.2}.a{width:calc(1px + 2);opacity:.3}@media(min-width:0px){.a{opacity:.4}}";
    check(aether_style_runtime_add_stylesheet(f.runtime, css, 2, "outer", &outer, &count) == AETHER_OK,
          "R2 outer succeeds after nested add callback");
    check(c.calls == 1 && outer == 1 && count == 4 && c.nested == 2,
          "R3 external return retains published outputs");
    check(f.runtime->states.at(f.node).get() == c.final_state,
          "R3 outer does not invalidate state resolved by callback");
    near(f.runtime->states.at(f.node)->style_view().scalars().opacity, .8f, "R3 cached callback winner survives return");
    near(c.outer_map->current->backing()->find(f.node)->style_view().scalars().opacity, .4f, "R3 callback outer map remains snapshot");
    near(c.final_map->current->backing()->find(f.node)->style_view().scalars().opacity, .8f, "R3 callback nested map survives return");
    std::set<media_condition_s*> removed_nodes;
    for (const auto& rule : f.runtime->sheets.at(outer)->rules) gather_media(rule.condition, removed_nodes);
    check(aether_style_runtime_remove_stylesheet(f.runtime, outer), "R2 remove outer independently");
    for (auto* node : removed_nodes) delete node; // This sheet's graph has no remaining references.
    near(f.resolve().opacity, .8f, "R2 nested survives removing outer");
    aether_style_map_destroy(c.outer_map); aether_style_map_destroy(c.final_map);
}

struct failed_callback_context {
    fixture* f;
    int64_t* outer_handle;
    int32_t* outer_count;
    bool calc_first;
    int calls = 0;
    int64_t nested = -1;
    const aether::style::FrozenNodePayload* original_state;
    const aether::style::FrozenNodePayload* final_state = nullptr;
};
void failed_callback(const char* code, const char*, const char*, const char*, void* data) {
    auto& c = *static_cast<failed_callback_context*>(data);
    auto& f = *c.f;
    ++c.calls;
    const char* expected = c.calc_first && c.calls == 1 ? "AETHER-CSS-CALC" : "AETHER-CSS-LIMIT";
    check(code && std::strcmp(code, expected) == 0, "R7 failed outer diagnostic batch stays FIFO");
    check(*c.outer_handle == -123 && *c.outer_count == -456, "R7 rejected outputs still sentinel in replay");
    if (c.calls == 1) {
        check(f.runtime->sheets.size() == 1 && f.runtime->next_sheet_handle == 2 &&
              f.runtime->next_rule_id == INT32_MAX - 1, "R7 complete rollback before first callback");
        check(f.runtime->states.at(f.node).get() == c.original_state,
              "R7 failed outer has not invalidated existing cache");
        check(f.runtime->compiled->index.by_class.at("a").size() == 1,
              "R7 failed outer has published no staged index entries");
        near(f.resolve().opacity, .4f, "R7 original sheet visible during failure callback");
        c.nested = f.add(".a{opacity:.8}");
        check(c.nested == 2 && f.runtime->sheets.at(c.nested)->rules.at(0).rule_id == INT32_MAX - 1,
              "R7 nested consumes last available ID with distinct handle");
        near(f.resolve().opacity, .8f, "R7 nested winner resolved inside failure callback");
        c.final_state = f.runtime->states.at(f.node).get();
    } else {
        check(f.runtime->next_rule_id == INT32_MAX && f.runtime->sheets.count(c.nested) == 1,
              "R7 subsequent diagnostic sees independent nested commit");
        check(f.runtime->states.at(f.node).get() == c.final_state,
              "R7 later diagnostics do not reset callback cache");
    }
}
void reentrant_capacity_abort(bool calc_first) {
    fixture f;
    f.add(".a{opacity:.4}"); f.resolve();
    f.runtime->next_rule_id = INT32_MAX - 1;
    int64_t outer = -123; int32_t count = -456;
    failed_callback_context c{&f, &outer, &count, calc_first, 0, -1,
                              f.runtime->states.at(f.node).get()};
    aether_style_runtime_set_diagnostics_callback(f.runtime, failed_callback, &c);
    const char* css = calc_first
        ? ".a{width:calc(1px + 2);opacity:.6}@media(min-width:0px){.a{opacity:.9}}"
        : ".a{opacity:.6}@media(min-width:0px){.a{opacity:.9}}";
    check(aether_style_runtime_add_stylesheet(f.runtime, css, 2, "abort", &outer, &count) ==
          AETHER_ERROR_INVALID_STATE, "R7 outer capacity failure survives successful nested add");
    check(c.calls == (calc_first ? 2 : 1), "R7 exact calc/limit diagnostic count");
    check(outer == -123 && count == -456 && f.runtime->sheets.size() == 2 &&
          f.runtime->next_sheet_handle == 3 && f.runtime->next_rule_id == INT32_MAX,
          "R7 failed return never rewinds nested changes");
    check(f.runtime->states.at(f.node).get() == c.final_state, "R7 callback-resolved cache retained after error");
    near(f.runtime->states.at(f.node)->style_view().scalars().opacity, .8f, "R7 nested remains cached winner");
}

struct last_id_context {
    fixture* f;
    int64_t* outer;
    int32_t* count;
    bool nested_active = false;
    int outer_calls = 0;
    int nested_calls = 0;
    int64_t empty = -1;
    const aether::style::FrozenNodePayload* final_state = nullptr;
};
void last_id_callback(const char* code, const char*, const char*, const char*, void* data) {
    auto& c = *static_cast<last_id_context*>(data);
    auto& f = *c.f;
    if (c.nested_active) {
        ++c.nested_calls;
        check(code && std::strcmp(code, "AETHER-CSS-LIMIT") == 0, "R8 nested emits limit diagnostic");
        return;
    }
    ++c.outer_calls;
    check(code && std::strcmp(code, "AETHER-CSS-CALC") == 0, "R8 outer calc diagnostic");
    check(*c.outer == 1 && *c.count == 1 && f.runtime->next_rule_id == INT32_MAX,
          "R8 last valid outer ID already committed in callback");
    check(f.runtime->sheets.at(*c.outer)->rules.at(0).rule_id == INT32_MAX - 1,
          "R8 outer owns last assignable identity");
    int64_t rejected = -123; int32_t rejected_count = -456;
    c.nested_active = true;
    check(aether_style_runtime_add_stylesheet(f.runtime, ".a{opacity:.8}", 2, "nested",
          &rejected, &rejected_count) == AETHER_ERROR_INVALID_STATE, "R8 nested nonempty rejects exhausted IDs");
    c.nested_active = false;
    check(rejected == -123 && rejected_count == -456 && f.runtime->sheets.size() == 1 &&
          f.runtime->next_sheet_handle == 2, "R8 rejected nested outputs and handle remain untouched");
    c.empty = f.add("");
    check(c.empty == 2 && f.runtime->sheets.at(c.empty)->rules.empty() &&
          f.runtime->next_rule_id == INT32_MAX, "R8 empty nested succeeds without consuming IDs");
    near(f.resolve().opacity, .4f, "R8 outer survives failed and empty nested adds");
    c.final_state = f.runtime->states.at(f.node).get();
}
void reentrant_last_id() {
    fixture f;
    f.runtime->next_rule_id = INT32_MAX - 1;
    int64_t outer = -1; int32_t count = -1;
    last_id_context c{&f, &outer, &count};
    aether_style_runtime_set_diagnostics_callback(f.runtime, last_id_callback, &c);
    check(aether_style_runtime_add_stylesheet(f.runtime, ".a{width:calc(1px + 2);opacity:.4}",
          2, "outer", &outer, &count) == AETHER_OK, "R8 outer success unaffected by nested failure");
    check(c.outer_calls == 1 && c.nested_calls == 1 && f.runtime->sheets.size() == 2 &&
          f.runtime->next_sheet_handle == 3 && f.runtime->next_rule_id == INT32_MAX,
          "R8 final counters and independent nested outcomes");
    check(f.runtime->states.at(f.node).get() == c.final_state, "R8 outer does not clear nested-resolved state");
}

struct handle_failure_context {
    fixture* f;
    int64_t prior;
    int64_t* output;
    int32_t* count;
    int calls = 0;
    const aether::style::FrozenNodePayload* final_state = nullptr;
};
void handle_failure_callback(const char* code, const char*, const char*, const char*, void* data) {
    auto& c = *static_cast<handle_failure_context*>(data);
    auto& f = *c.f;
    ++c.calls;
    check(c.calls == 1 && code && std::strcmp(code, "AETHER-CSS-LIMIT") == 0,
          "R9 handle guard uses single buffered limit diagnostic");
    check(*c.output == -123 && *c.count == -456 && f.runtime->next_sheet_handle == INT64_MAX &&
          f.runtime->next_rule_id == 2 && f.runtime->sheets.size() == 1,
          "R9 handle-limit replay sees stable rejected operation");
    near(f.resolve().opacity, .4f, "R9 callback can read previous sheet");
    check(aether_style_runtime_remove_stylesheet(f.runtime, c.prior), "R9 callback removes prior sheet");
    near(f.resolve().opacity, 1.f, "R9 callback observes removal and resolves initial value");
    c.final_state = f.runtime->states.at(f.node).get();
}
void reentrant_handle_failure() {
    fixture f;
    const auto prior = f.add(".a{opacity:.4}"); f.resolve();
    f.runtime->next_sheet_handle = INT64_MAX;
    int64_t output = -123; int32_t count = -456;
    handle_failure_context c{&f, prior, &output, &count};
    aether_style_runtime_set_diagnostics_callback(f.runtime, handle_failure_callback, &c);
    check(aether_style_runtime_add_stylesheet(f.runtime, ".a{opacity:.9}", 2, "rejected",
          &output, &count) == AETHER_ERROR_INVALID_STATE, "R9 precompile failure returns original error");
    check(c.calls == 1 && output == -123 && count == -456 && f.runtime->sheets.empty() &&
          f.runtime->next_sheet_handle == INT64_MAX && f.runtime->next_rule_id == 2,
          "R9 callback removal persists with counters unchanged");
    check(f.runtime->states.at(f.node).get() == c.final_state, "R9 callback final cache survives failed return");
}

struct source_failure_context {
    fixture* f;
    int64_t* output;
    int32_t* count;
    int calls = 0;
    int64_t nested = -1;
    const aether::style::FrozenNodePayload* final_state = nullptr;
};
void source_failure_callback(const char* code, const char*, const char*, const char*, void* data) {
    auto& c = *static_cast<source_failure_context*>(data);
    auto& f = *c.f;
    ++c.calls;
    check(c.calls == 1 && code && std::strcmp(code, "AETHER-CSS-LIMIT") == 0,
          "R10 source limit diagnostic replay once");
    check(*c.output == -123 && *c.count == -456 && f.runtime->sheets.empty() &&
          f.runtime->next_sheet_handle == 1 && f.runtime->next_rule_id == 1,
          "R10 oversize rejected without prefix publication before callback");
    c.nested = f.add(".a{opacity:.8}");
    check(c.nested == 1 && f.runtime->sheets.at(c.nested)->rules.at(0).rule_id == 1,
          "R10 nested valid source uses unconsumed handle and ID");
    near(f.resolve().opacity, .8f, "R10 nested valid source resolves");
    c.final_state = f.runtime->states.at(f.node).get();
}
void reentrant_source_failure() {
    fixture f;
    int64_t output = -123; int32_t count = -456;
    source_failure_context c{&f, &output, &count};
    aether_style_runtime_set_diagnostics_callback(f.runtime, source_failure_callback, &c);
    const std::string oversized = ".a{opacity:.4}" + std::string(129, ' ');
    check(aether_style_runtime_add_stylesheet(f.runtime, oversized.c_str(), 2, "oversized",
          &output, &count) == AETHER_ERROR_INVALID_STATE, "R10 oversized outer status survives nested success");
    check(c.calls == 1 && output == -123 && count == -456 && f.runtime->sheets.size() == 1 &&
          f.runtime->next_rule_id == 2 && f.runtime->next_sheet_handle == 2,
          "R10 nested changes preserved after source rejection returns");
    check(f.runtime->states.at(f.node).get() == c.final_state, "R10 nested callback cache not cleared");
}

struct payload_context { int calls = 0; };
void payload_callback(const char* code, const char* property, const char* value,
                      const char* message, void* data) {
    auto& c = *static_cast<payload_context*>(data);
    ++c.calls;
    if (c.calls == 1) {
        check(code == nullptr && property && *property == '\0' && value &&
              std::strcmp(value, "temporary-value") == 0 && message == nullptr,
              "R12 payload preserves null versus empty and copied value");
    } else if (c.calls == 2) {
        check(code && std::strcmp(code, "temporary-code") == 0 &&
              property == nullptr && value && *value == '\0' && message &&
              std::strcmp(message, "temporary-message") == 0,
              "R12 FIFO payload owns all temporary fields without null conflation");
    } else {
        check(c.calls == 3 && code && *code == '\0' && property &&
              std::strcmp(property, "temporary-property") == 0 && value == nullptr &&
              message && *message == '\0', "R12 property copy and null value remain distinct from empty fields");
    }
}
void diagnostic_payload_ownership() {
    payload_context context;
    stylesheet_diagnostics_s buffer(payload_callback, &context);
    auto collector = buffer.collector();
    check(collector != nullptr, "R12 enabled buffer exposes private collector");
    {
        std::string value = "temporary-value";
        std::string code = "temporary-code";
        std::string message = "temporary-message";
        std::string property = "temporary-property";
        collector(nullptr, "", value.c_str(), nullptr, &buffer);
        collector(code.c_str(), nullptr, "", message.c_str(), &buffer);
        collector("", property.c_str(), nullptr, "", &buffer);
        property.assign("mutated");
        value.assign("mutated"); code.assign("mutated"); message.assign("mutated");
    }
    check(context.calls == 0, "R12 collector never invokes consumer during accumulation");
    buffer.replay();
    check(context.calls == 3, "R12 explicit synchronous replay delivers all messages");
    stylesheet_diagnostics_s disabled(nullptr, &context);
    check(disabled.collector() == nullptr, "R12 null callback installs no collector");
    disabled.replay();
    check(context.calls == 3, "R12 null callback replay inert");
    {
        stylesheet_diagnostics_s discarded(payload_callback, &context);
        discarded.collector()("unused", nullptr, nullptr, nullptr, &discarded);
    }
    check(context.calls == 3, "R12 buffer destruction never invokes callback");
}

void passive_callback(const char*, const char*, const char*, const char*, void* data) {
    ++*static_cast<int*>(data);
}
void null_and_passive_counters() {
    for (bool enabled : {false, true}) {
        fixture f;
        int calls = 0;
        aether_style_runtime_set_diagnostics_callback(f.runtime, enabled ? passive_callback : nullptr, &calls);
        const auto first = f.add(".a{width:calc(1px + 2);opacity:.4}");
        const auto second = f.add(".a{opacity:.8}");
        check(first == 1 && second == 2 && f.runtime->next_sheet_handle == 3 &&
              f.runtime->next_rule_id == 3, "R13 null and passive callbacks have literal identical counters");
        check(f.runtime->sheets.at(first)->rules.at(0).rule_id == 1 &&
              f.runtime->sheets.at(second)->rules.at(0).rule_id == 2,
              "R13 null and passive callbacks preserve unique global IDs");
        check(calls == (enabled ? 1 : 0), "R13 only passive callback receives diagnostic");
        near(f.resolve().opacity, .8f, "R13 null and passive paths keep cascade result");
    }
}

}
static void retained_compiled_inputs() {
    fixture f;
    const auto handle=f.add(".a:hover { opacity: .4 }");
    const auto pinned=f.runtime->compiled;
    check(pinned->sheets.size()==1 && pinned->state_programs.size()==1,"compiled input owns AST and state program");
    const auto* rule=pinned->index.by_class.at("a").front();
    const auto id=rule->rule_id;
    check(aether_style_runtime_remove_stylesheet(f.runtime,handle)==AETHER_TRUE,"remove pinned sheet");
    check(f.runtime->compiled!=pinned && f.runtime->compiled->sheets.empty(),"new index published separately");
    check(rule->rule_id==id && pinned->sheets[0]->rules[0].rule_id==id,"removed AST stays alive under old pin");
    check(pinned->state_programs[0].state_mask&AETHER_STATE_HOVER,"pinned dependency program survives removal");
}
int main() {
    retained_compiled_inputs();
    counter_transaction(); handle_transaction(); source_and_inline_rejection();
    reentrant_metadata_and_cache();
    reentrant_capacity_abort(false); reentrant_capacity_abort(true);
    reentrant_last_id(); reentrant_handle_failure(); reentrant_source_failure();
    diagnostic_payload_ownership(); null_and_passive_counters();
    std::printf("stylesheet source order runtime: %d checks passed\n", checks);
}
