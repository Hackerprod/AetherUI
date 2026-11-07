#include <aether/aether.h>
#include "snapshot_test_support.h"
#include <aether/aether_contextual_style.h>
#include "../src/css/computed_style_handle.h"
#include "../src/style_runtime/style_property_registry_internal.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <type_traits>

void builtin_properties_register(style_property_registry_s*);
namespace {
long fail_after = -1;
size_t allocations = 0;
bool count_allocations = false;
bool fault_delivered = false;
unsigned checks = 0;
}
void* operator new(std::size_t n) {
    if (count_allocations) ++allocations;
    if (fail_after >= 0 && fail_after-- == 0) { fault_delivered = true; throw std::bad_alloc(); }
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { try { return ::operator new(n); } catch (...) { return nullptr; } }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { try { return ::operator new[](n); } catch (...) { return nullptr; } }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

namespace {
void need(bool condition, const char* message) {
    ++checks;
    if (!condition) { fail_after = -1; std::fprintf(stderr, "FAIL scratch: %s\n", message); std::exit(1); }
}
void ok(aether_status_t result, const char* message) { need(result == AETHER_OK, message); }
bool length_equal(aether_css_length a, aether_css_length b) {
    return a.value == b.value && a.unit == b.unit && a.linear_percent == b.linear_percent &&
        a.linear_em == b.linear_em && a.linear_rem == b.linear_rem && a.linear_vw == b.linear_vw && a.linear_vh == b.linear_vh;
}
void equal_measures(measure_read_view_s a, measure_read_view_s b, const measure_state_s& keys,
                    const std::string& active) {
    need(a.parent_font_size() == b.parent_font_size() && a.self_font_size() == b.self_font_size(), "font metadata equal");
    need(a.line_height().kind == b.line_height().kind && a.line_height().value == b.line_height().value, "typed line-height equal");
    need(length_equal(a.row_gap(), b.row_gap()) && length_equal(a.column_gap(), b.column_gap()), "gap metadata equal");
    const auto ai = a.inputs(), bi = b.inputs();
    need(ai.self_font_size == bi.self_font_size && ai.initial_font_size == bi.initial_font_size &&
        ai.root_font_size == bi.root_font_size && ai.viewport_width == bi.viewport_width && ai.viewport_height == bi.viewport_height,
        "unit input snapshot equal");
    const auto ap = a.background_positions(), bp = b.background_positions();
    const auto as = a.background_sizes(), bs = b.background_sizes();
    need(ap.size() == bp.size() && as.size() == bs.size(), "independent list sizes equal");
    for (size_t i = 0; i < ap.size(); ++i)
        need(length_equal(ap[i].x, bp[i].x) && length_equal(ap[i].y, bp[i].y), "positions equal");
    for (size_t i = 0; i < as.size(); ++i)
        need(as[i].kind == bs[i].kind && length_equal(as[i].x, bs[i].x) && length_equal(as[i].y, bs[i].y), "sizes equal");
    for (const auto& [key, flags] : keys.property_dependencies) {
        (void)flags;
        need(a.property_dependencies(key) == b.property_dependencies(key), "unrelated dependency retained");
    }
    need(a.property_dependencies(active) == b.property_dependencies(active), "selected dependency equal");
}
measure_state_s snapshot_measures(const measure_state_s& source) {
    auto copy = source;
    if (source.document) copy.document = std::make_shared<const document_measure_context_s>(*source.document);
    return copy;
}
void equal_state(const measure_state_s& a, const measure_state_s& b) {
    need(a.property_dependencies == b.property_dependencies, "exact dependency inventory and flags equal");
    need(a.dependencies == b.dependencies, "dependency aggregate equal");
    need(bool(a.document) == bool(b.document), "document presence equal");
    if (a.document) need(a.document->identity == b.document->identity &&
        a.document->generation == b.document->generation, "document identity and generation equal");
    equal_measures(measure_read_view_s(a), measure_read_view_s(b), a, std::string("width"));
}
struct Flat {
    aether_computed_style_t value{};
    Flat() : value(aether_computed_style_default()) {}
    explicit Flat(const aether_computed_style_t& source) {
        aether_computed_style_handle_s borrowed{}; borrowed.style = source;
        ok(aether_computed_style_copy(&borrowed, &value), "copy independent flat");
    }
    ~Flat() { aether_computed_style_free(&value); }
    Flat(const Flat&) = delete;
};
void equal_flat(const aether_computed_style_t& a, const aether_computed_style_t& b) {
    need(snapshot_test::style_equal(a, b), "complete flat field and payload equality");
}
measure_state_s rich_measures(size_t count = 64) {
    measure_state_s result;
    auto doc = std::make_shared<document_measure_context_s>();
    doc->viewport_width = 800; doc->viewport_height = 600; doc->initial_font_size = 10; doc->root_font_size = 32;
    result.document = std::move(doc); result.parent_font_size = 20; result.self_font_size = 40;
    result.background_positions.resize(3); result.background_sizes.resize(2);
    for (size_t i = 0; i < result.background_positions.size(); ++i) {
        result.background_positions[i].x.value = float(11 + i); result.background_positions[i].y.value = float(21 + i);
    }
    for (auto& size : result.background_sizes) {
        size.kind = AETHER_BACKGROUND_SIZE_EXPLICIT;
        size.x = {42, AETHER_CSS_UNIT_PX, 0, 0, 0, 0, 0}; size.y = {43, AETHER_CSS_UNIT_PX, 0, 0, 0, 0, 0};
    }
    for (size_t i = 0; i < count; ++i)
        measure_set_property_dependencies(result, "unrelated-property-with-nonzero-dependency-" + std::to_string(i), MEASURE_ROOT_FONT | MEASURE_VIEWPORT);
    measure_recompute_dependencies(result);
    return result;
}
property_apply_result_s apply(const style_property_definition_s& definition, Flat& flat,
                              color_resolution_state_s& color, measure_apply_state_s& measures,
                              const std::string& source) {
    measures.set_property_dependencies(measure_source_dependencies(source));
    property_apply_context_s context{&flat.value, &color, &measures};
    if (definition.apply_with_context_checked) return definition.apply_with_context_checked(context, source);
    if (definition.apply_with_context) definition.apply_with_context(context, source);
    else if (definition.apply_checked) return definition.apply_checked(&flat.value, source);
    else if (definition.apply) definition.apply(&flat.value, source);
    return {};
}
const style_property_definition_s& property(style_property_registry_s& registry, const char* name) {
    auto* p = registry.try_get(name); need(p != nullptr, "property registered"); return *p;
}
void seed_images(style_property_registry_s& registry, Flat& flat) {
    const auto& images = property(registry, "background-image");
    need(images.apply_checked(&flat.value, "url(one),url(two),linear-gradient(red,blue)").success, "seed three image layers");
}
void equivalent_applicators(style_property_registry_s& registry) {
    struct Case { const char* name; const char* source; };
    for (const auto& item : {Case{"font-size","2em"}, {"font-size","50%"}, {"font-size","calc(50% + 1rem + 2vw)"},
            {"font-size","0"}, {"font-size","larger"}, {"font-size","smaller"}, {"font-size","medium"},
            {"line-height","normal"}, {"line-height","2"}, {"line-height","2em"}, {"line-height","200%"},
            {"row-gap","calc(0% + 1em)"}, {"column-gap","2rem"}, {"width","calc(50% - 50% + 30px)"},
            {"height","0%"}, {"padding-left","2em"}, {"border-left-width","1rem"},
            {"background-position","1em 2rem, 10vw 20vh"}, {"background-size","2em 1rem, cover, 0% auto"},
            {"text-shadow","1em 1rem currentColor, 2vw 3vh red"}, {"box-shadow","inset 1em 2rem 3px currentColor"}}) {
        const auto& definition = property(registry, item.name);
        const std::string source = item.source;
        auto base = rich_measures();
        Flat original; seed_images(registry, original); original.value.font_size = 13;
        color_resolution_state_s real_color;
        Flat next(original.value);
        measure_apply_state_s selected(base, definition.name);
        need(apply(definition, next, real_color, selected, source).success, "winner applied to NEXT");
        const auto saved = snapshot_measures(base);
        auto full_copy = base; // Deliberate control only: the former probe algorithm.
        Flat control(original.value), scratch_flat(original.value);
        color_resolution_state_s control_color, scratch_color;
        measure_apply_state_s complete(full_copy, definition.name);
        measure_property_delta_s delta;
        measure_apply_state_s scratch(base, delta, definition.name);
        const auto control_status = apply(definition, control, control_color, complete, source);
        const auto scratch_status = apply(definition, scratch_flat, scratch_color, scratch, source);
        need(control_status.success == scratch_status.success, "application result equivalent");
        equal_flat(control.value, scratch_flat.value);
        equal_measures(complete.read(), scratch.read(), base, definition.name);
        equal_state(saved, base);
        need(control_color.text_shadow_uses_current_color == scratch_color.text_shadow_uses_current_color &&
             control_color.box_shadow_uses_current_color == scratch_color.box_shadow_uses_current_color, "symbolic shadow flags equivalent");
    }
}
void inheritance(style_property_registry_s& registry) {
    for (const char* name : {"background-image", "background-position", "background-size", "font-size", "line-height", "row-gap", "column-gap", "width", "transform", "text-shadow"}) {
        const auto& definition = property(registry, name);
        need(bool(definition.inherit_context), "contextual inheritance present");
        auto base = rich_measures(), parent = rich_measures(0);
        parent.self_font_size = 20; parent.line_height = {measure_line_height_kind_e::absolute_css_px, 60};
        parent.background_positions.resize(5); parent.background_sizes.resize(1);
        parent.background_sizes[0].x.value = 87;
        measure_set_property_dependencies(parent, definition.name, MEASURE_SELF_FONT | MEASURE_PERCENTAGE);
        Flat parent_flat, original; seed_images(registry, original);
        // Parent intentionally has no painted images; its longhand lists still inherit.
        parent_flat.value.font_size = 20; parent_flat.value.width = {50, AETHER_CSS_UNIT_PERCENT, 0, 0, 0, 0, 0};
        const auto saved = snapshot_measures(base); auto full_copy = base;
        Flat control(original.value), scratch_flat(original.value);
        color_resolution_state_s pc, cc, sc;
        measure_apply_state_s complete(full_copy, definition.name);
        measure_property_delta_s delta; measure_apply_state_s scratch(base, delta, definition.name);
        property_apply_context_s a{&control.value, &cc, &complete, &pc}, b{&scratch_flat.value, &sc, &scratch, &pc};
        definition.inherit_context(a, parent_flat.value, &parent);
        definition.inherit_context(b, parent_flat.value, &parent);
        equal_flat(control.value, scratch_flat.value);
        equal_measures(complete.read(), scratch.read(), base, definition.name);
        equal_state(saved, base);
        need(scratch.property_dependencies() == (MEASURE_PARENT | MEASURE_PERCENTAGE), "inherit retains syntactic percentage only");
        if (std::strcmp(name,"background-position") == 0) {
            need(scratch.read().background_positions().size() == 5, "unpainted parent positions inherited");
            need(scratch.read().background_positions().data() != parent.background_positions.data(), "inherited positions owned");
        }
        if (std::strcmp(name,"background-size") == 0) {
            need(scratch_flat.value.background_layers.items[2].size_x.value == 87, "new size read before projection");
            need(scratch.read().background_sizes().data() != parent.background_sizes.data(), "inherited sizes owned");
        }
        if (std::strcmp(name,"background-image") == 0) need(scratch_flat.value.background_layers.count == 0, "image absence inherited without erasing metadata");
    }
}
void zero_allocation_scratch() {
    static_assert(std::is_trivially_copyable_v<measure_inputs_s>);
    static_assert(std::is_same_v<decltype(std::declval<measure_read_view_s>().background_positions()), std::span<const measure_background_position_s>>);
    const std::string property_name = "a-long-selected-property-name-with-flags";
    for (size_t size : {size_t(1), size_t(64), size_t(1024)}) {
        auto base = rich_measures(size); measure_set_property_dependencies(base, property_name, MEASURE_ROOT_FONT);
        base.background_positions.resize(size); base.background_sizes.resize(size);
        const auto saved = snapshot_measures(base);
        allocations = 0; count_allocations = true; fail_after = 0; fault_delivered = false;
        for (unsigned i = 0; i < 1000; ++i) {
            measure_property_delta_s delta; measure_apply_state_s scratch(base, delta, property_name);
            need(scratch.property_dependencies() == MEASURE_ROOT_FONT, "read dependency without copy");
            scratch.set_property_dependencies(0); need(scratch.property_dependencies() == 0, "local zero is not absent delta");
            scratch.set_self_font_size(0); scratch.set_line_height({measure_line_height_kind_e::number, 2});
            scratch.set_row_gap({0, AETHER_CSS_UNIT_PERCENT, 0, 0, 0, 0, 0});
            need(scratch.read().self_font_size() == 0 && scratch.read().line_height().value == 2, "scalar read after write");
            need(scratch.read().background_positions().data() == base.background_positions.data() &&
                 scratch.read().background_sizes().data() == base.background_sizes.data(), "unchanged lists borrowed const");
            const auto units = scratch.read().inputs_with_self_font(scratch.read().parent_font_size());
            need(measure_compute_length({2, AETHER_CSS_UNIT_EM, 0, 0, 0, 0, 0}, units).value == 40, "parent font units without metadata copy");
        }
        fail_after = -1; count_allocations = false;
        need(!fault_delivered && allocations == 0, "scratch constructor scalar writes reads and destructor allocate zero");
        equal_state(saved, base);
        std::printf("scratch base=%zu dependencies/list items: 1000 probes, %zu new allocations\n", size, allocations);
    }
}
void owned_delta_aliases() {
    auto base = rich_measures();
    const auto saved = snapshot_measures(base);
    const std::string key = "background-position";
    {
        measure_property_delta_s delta;
        measure_apply_state_s scratch(base, delta, key);
        scratch.set_background_positions(base.background_positions);
        need(scratch.read().background_positions().data() != base.background_positions.data(), "local position output independent of NEXT");
        need(scratch.read().background_sizes().data() == base.background_sizes.data(), "unwritten size list borrowed");
        delta.background_positions->front().x.value = 999;
        need(base.background_positions.front().x.value != 999, "mutation of owned position output cannot change NEXT");
        scratch.set_background_sizes(base.background_sizes);
        need(scratch.read().background_sizes().data() != base.background_sizes.data(), "local size output independent of NEXT");
        delta.background_sizes->front().x.value = 998;
        need(base.background_sizes.front().x.value != 998, "mutation of owned size output cannot change NEXT");
    }
    equal_state(saved, base);
    auto document = std::make_shared<document_measure_context_s>(*base.document);
    document->root_font_size = 0;
    base.document = document; base.parent_font_size = 0; base.self_font_size = 0;
    const auto inputs = measure_read_view_s(base).inputs();
    need(measure_compute_length({2, AETHER_CSS_UNIT_EM, 0, 0, 0, 0, 0}, inputs).value == 0, "zero self font stays valid");
    need(measure_compute_length({2, AETHER_CSS_UNIT_REM, 0, 0, 0, 0, 0}, inputs).value == 0, "zero root font stays valid");
}
void sparse_dependencies() {
    auto state = rich_measures(0); const std::string key = "font-size";
    measure_set_property_dependencies(state, key, MEASURE_PARENT | MEASURE_PERCENTAGE);
    measure_recompute_dependencies(state); need(state.dependencies == (MEASURE_PARENT | MEASURE_PERCENTAGE), "aggregate inherited flags");
    measure_set_property_dependencies(state, key, 0); measure_recompute_dependencies(state);
    need(state.property_dependencies.empty() && state.dependencies == 0, "absolute replacement clears key and stale aggregate");
    for (const char* source : {"0%", "calc(0% + 30px)", "calc(50% - 50% + 30px)"}) {
        measure_set_property_dependencies(state, key, measure_source_dependencies(source));
        need(measure_property_dependencies(state, key) & MEASURE_PERCENTAGE, "syntactic zero/cancelled percentage retained");
    }
}
void local_faults(style_property_registry_s& registry) {
    for (const char* name : {"background-position", "background-size"}) {
        const auto& definition = property(registry, name);
        const std::string source = std::strcmp(name,"background-position") == 0 ? "1em 2rem, 3vw 4vh, 50% 0%" : "1em 2rem, 3vw 4vh, 50% 0%";
        auto base = rich_measures(); const auto saved = snapshot_measures(base);
        Flat original; seed_images(registry, original);
        bool succeeded = false; unsigned failed = 0;
        for (long index = 0; index < 512; ++index) {
            Flat candidate(original.value); color_resolution_state_s colors;
            measure_property_delta_s delta; measure_apply_state_s scratch(base, delta, definition.name);
            fault_delivered = false; fail_after = index;
            try {
                const auto result = apply(definition, candidate, colors, scratch, source);
                fail_after = -1;
                need(result.success && !fault_delivered, "valid scratch list never silently drops injected failure");
                succeeded = true;
            } catch (const std::bad_alloc&) { fail_after = -1; need(fault_delivered, "local failure delivered"); ++failed; }
            equal_state(saved, base);
            if (succeeded) break;
        }
        need(succeeded && failed > 0, "local list scratch sweep reaches failures and clean retry");
        std::printf("%s local scratch new sweep: %u failures then successful retry\n", name, failed);
    }
    // An exception AFTER a local line-height write must not mutate the base.
    const auto& definition = property(registry, "line-height");
    auto base = rich_measures(); base.self_font_size = 2000; const auto saved = snapshot_measures(base);
    Flat candidate; color_resolution_state_s colors;
    measure_property_delta_s delta; measure_apply_state_s scratch(base, delta, definition.name);
    bool overflow = false;
    try { apply(definition, candidate, colors, scratch, "calc(3e38)"); } catch (const std::overflow_error&) { overflow = true; }
    need(overflow && delta.line_height.has_value(), "line-height overflow checks newly written local value");
    equal_state(saved, base);
}
struct Runtime {
    aether_doc_t doc{}; aether_style_runtime_t runtime{}; aether_node_t child{};
    Runtime() {
        ok(aether_doc_create(&doc), "runtime doc");
        ok(aether_doc_parse_html(doc, "<html><body><div id='p'><div id='c'></div></div></body></html>"), "runtime html");
        child = aether_doc_get_element_by_id(doc,"c");
        aether_style_environment_t env{800,600,1,16,0,0,0};
        ok(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&runtime), "runtime create");
        int64_t sheet{};
        ok(aether_style_runtime_add_stylesheet(runtime, "html{font-size:32px}#p{font-size:20px;background-position:1em 2rem;background-size:3em 4rem}#c{font-size:2em;width:1rem;height:calc(50% - 50% + 30px);background-image:url(a),url(b);background-position:inherit;background-size:inherit;line-height:2}", 0, "scratch", &sheet, nullptr), "runtime sheet");
    }
    ~Runtime() { aether_style_runtime_destroy(runtime); aether_doc_destroy(doc); }
    aether_style_revision_t resolve() {
        aether_style_transaction_t tx{}; ok(aether_style_runtime_prepare_revision(runtime,aether_doc_get_document_element(doc),nullptr,&tx), "runtime prepare");
        aether_style_revision_t rev{}; ok(aether_style_runtime_commit_revision(runtime,tx,&rev), "runtime commit");
        aether_style_transaction_destroy(tx); return rev;
    }
    aether_computed_style_handle_t lookup(aether_style_revision_t rev) {
        for (int i = 0; i < aether_style_revision_style_count(rev); ++i) {
            aether_node_t node{}; aether_computed_style_handle_t style{};
            ok(aether_style_revision_get_style(rev,i,&node,&style,nullptr), "revision entry");
            if (node == child) return style;
            aether_computed_style_handle_destroy(style);
        }
        need(false,"child retained"); return nullptr;
    }
};
void runtime_faults_and_metadata() {
    Runtime f; auto g0 = f.resolve(); auto old = f.lookup(g0);
    const auto old_measures = snapshot_measures(*old->measures);
    Flat old_flat(old->style);
    const char* changed = "font-size:3em;width:32px;line-height:150%;background-position:2em 3rem,4vw 5vh;background-size:5em 6rem,50% auto;box-shadow:1em 1rem currentColor";
    ok(aether_element_set_attribute(f.child,"style",changed), "change scratch-rich inputs");
    Runtime clean;
    auto clean_g0 = clean.resolve(); aether_style_revision_destroy(clean_g0);
    ok(aether_element_set_attribute(clean.child,"style",changed), "change clean comparison inputs");
    auto clean_g1 = clean.resolve(); auto clean_style = clean.lookup(clean_g1);
    const auto generation = aether_style_revision_generation(g0);
    bool succeeded = false; unsigned failures = 0;
    for (long index = 0; index < 20000; ++index) {
        aether_style_transaction_t tx = reinterpret_cast<aether_style_transaction_t>(1);
        fail_after = index; fault_delivered = false;
        const auto status = aether_style_runtime_prepare_revision(f.runtime,aether_doc_get_document_element(f.doc),nullptr,&tx);
        fail_after = -1;
        if (status == AETHER_OK) {
            need(!fault_delivered, "successful prepare did not swallow injected new failure");
            aether_style_revision_t g1{}; ok(aether_style_runtime_commit_revision(f.runtime,tx,&g1), "retry commit");
            aether_style_transaction_destroy(tx);
            auto current = f.lookup(g1);
            equal_flat(clean_style->style, current->style);
            equal_measures(measure_read_view_s(*clean_style->measures), measure_read_view_s(*current->measures), *clean_style->measures, std::string("width"));
            need(clean_style->measures->property_dependencies == current->measures->property_dependencies &&
                clean_style->measures->dependencies == current->measures->dependencies, "retry metadata equals independent clean computation");
            need(current->style.font_size == 60 && measure_used_line_height(current->measures.get(), current->style) == 90, "retry retains self font and typed line height");
            need(current->style.background_layers.items[0].position_x.value == 120 && current->style.background_layers.items[1].position_x.value == 32, "probe background reads NEXT self and viewport");
            need(current->style.background_layers.items[0].size_x.value == 300, "probe independent size survives");
            need(measure_property_dependencies(*current->measures,"width") == 0, "numeric no-op rem to px clears dependency");
            for (const auto& entry : current->measures->property_dependencies) need(entry.second != 0, "published dependency map canonical sparse");
            need(measure_has_percentage(current->measures.get(),"height",current->style.height), "unmodified cancelled percentage persists");
            aether_computed_style_handle_destroy(current); aether_style_revision_destroy(g1); succeeded = true; break;
        }
        ++failures;
        need(fault_delivered && status == AETHER_ERROR_OUT_OF_MEMORY && tx == nullptr, "prepare OOM propagates without partial output");
        aether_style_revision_t current{}; ok(aether_style_runtime_retain_revision(f.runtime,&current), "retain after failure");
        need(aether_style_revision_generation(current) == generation, "failure preserves generation");
        auto style = f.lookup(current);
        equal_flat(old_flat.value, old->style);
        equal_flat(old_flat.value, style->style);
        equal_state(old_measures, *old->measures);
        equal_state(old_measures, *style->measures);
        aether_computed_style_handle_destroy(style); aether_style_revision_destroy(current);
    }
    need(succeeded && failures > 0, "runtime sweep completed");
    equal_state(old_measures, *old->measures);
    equal_flat(old_flat.value, old->style);
    need(old->style.font_size == 40 && old->style.background_layers.items[0].position_x.value == 20, "retained OLD remains intact after retry");
    std::printf("scratch-rich runtime new sweep: %u failures then successful retry\n", failures);
    aether_computed_style_handle_destroy(old); aether_style_revision_destroy(g0);
    aether_computed_style_handle_destroy(clean_style); aether_style_revision_destroy(clean_g1);
}
}
int main() {
    style_property_registry_s registry; builtin_properties_register(&registry); registry.seal();
    zero_allocation_scratch(); owned_delta_aliases(); sparse_dependencies(); equivalent_applicators(registry); inheritance(registry);
    local_faults(registry); runtime_faults_and_metadata();
    std::printf("measure property scratch: %u checks passed\n", checks);
}
