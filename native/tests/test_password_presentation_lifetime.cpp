#include "../src/layout/layout_internal.h"
#include "../src/layout/form_controls.h"
#include <aether/aether_contextual_style.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <new>
#include <string>
#include <vector>

// This executable links the authentic core OBJECT pool, NOT the Core DSO.
// Injection is armed only around the single operation named by each sweep.
static long fail_at = -1;
static long allocation_count = 0;
static bool injected = false;
void* operator new(std::size_t size) {
    if (fail_at >= 0 && allocation_count++ == fail_at) {
        injected = true;
        throw std::bad_alloc();
    }
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    try { return ::operator new(n); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
    try { return ::operator new[](n); } catch (...) { return nullptr; }
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

namespace {
// Both are literal independent fixtures. No production decoder builds expected.
constexpr char value64[] =
    "SyntheticValue00SyntheticValue00SyntheticValue00SyntheticValue00";
constexpr char mask64[] =
    "••••••••••••••••••••••••••••••••"
    "••••••••••••••••••••••••••••••••";
static_assert(sizeof(value64) == 65, "fixture must contain 64 ASCII scalars");
static_assert(sizeof(mask64) == 193, "64 literal U+2022 scalars occupy 192 bytes");
constexpr long sweep_cap = 8192;
struct Failed {};
// Stable check IDs are documented in PRIVATE-INVENTORY.md. Repetitions retain
// their full multiplicity in counters; bitmap records logical failing IDs.
struct Report {
    const char* id;
    unsigned long long checks = 0, failures = 0, bitmap = 0;
    long attempts = 0, ordinal = -1, oom = 0, recoveries = 0;
    int status = AETHER_OK;
    bool completed = false;
    void check(bool condition, unsigned id_) {
        ++checks;
        if (!condition) {
            ++failures;
            bitmap |= (1ULL << id_);
            throw Failed{};
        }
    }
    void print() const {
        std::printf("%s checks=%llu fail=%llu bits=%016llx attempts=%ld ordinal=%ld injected_oom=%ld recovery=%ld status=%d complete=%d unvisited=%ld\n",
                    id, checks, failures, bitmap, attempts, ordinal, oom,
                    recoveries, status, completed ? 1 : 0,
                    completed ? 0L : sweep_cap - attempts);
    }
};
struct Armed {
    explicit Armed(long ordinal) {
        allocation_count = 0;
        injected = false;
        fail_at = ordinal;
    }
    ~Armed() { fail_at = -1; }
};
struct Measures {
    unsigned long long calls = 0, wrong = 0;
    const char* expected = mask64;
};
aether_text_metrics_t measure(const char* s, const aether_computed_style_t*, float, void* user) {
    auto& m = *static_cast<Measures*>(user);
    ++m.calls;
    if (!s || std::strcmp(s, m.expected) != 0) ++m.wrong;
    return {37.f, 19.f, 14.f};
}
using namespace aether::style;
struct NodeSaved {
    aether_node_s* node;
    aether_node_s* parent;
    aether_node_type_t type;
    std::string text;
    std::vector<aether_node_s*> children;
    std::map<std::string, std::string, std::less<>> attributes;
    uint64_t identity;
};
void save_dom(aether_node_s* node, std::vector<NodeSaved>& saved) {
    saved.push_back({node, node->parent, node->type, node->text, node->children,
                     node->attributes, aether_node_identity(node)});
    for (auto* child : node->children) save_dom(child, saved);
}
bool same_rect(const aether_rectf& a, const aether_rectf& b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}
struct BoxSaved {
    aether_layout_box_s* box;
    aether_rectf border, margin, content;
    std::vector<aether_layout_box_s*> children;
    std::string displayed;
    bool placeholder;
};
struct FrameSaved {
    aether_layout_result_t frame;
    std::vector<BoxSaved> boxes;
    decltype(aether_layout_result_s::primary_boxes) primary;
    decltype(aether_layout_result_s::paint_order) paint_order;
    decltype(aether_layout_result_s::assets) assets;
    explicit FrameSaved(aether_layout_result_t f)
        : frame(f), primary(f->primary_boxes), paint_order(f->paint_order), assets(f->assets) {
        for (auto* b : f->allocated_boxes)
            boxes.push_back({b, b->border_box, b->margin_box, b->content_box,
                             b->children, b->control_display_text, b->control_shows_placeholder});
    }
    void intact(Report& r) const {
        r.check(frame->primary_boxes == primary && frame->paint_order == paint_order &&
                frame->assets == assets && frame->allocated_boxes.size() == boxes.size(), 10);
        for (const auto& b : boxes) {
            r.check(same_rect(b.box->border_box, b.border) && same_rect(b.box->margin_box, b.margin) &&
                    same_rect(b.box->content_box, b.content) && b.box->children == b.children, 11);
            r.check(b.box->control_display_text == b.displayed &&
                    b.box->control_shows_placeholder == b.placeholder, 12);
        }
    }
};
struct Fixture {
    Report& r;
    Measures measured;
    aether_doc_t doc = nullptr;
    aether_node_t input = nullptr;
    aether_style_runtime_t runtime = nullptr;
    aether_style_map_t map = nullptr;
    aether_layout_engine_t engine = nullptr;
    aether_display_list_builder_t painter = nullptr;
    aether_layout_result_t source = nullptr, clone = nullptr, candidate = nullptr;
    aether_display_list_t old_list = nullptr, candidate_list = nullptr;
    MapVersionRef base;
    std::vector<NodeSaved> dom;
    uint64_t structure = 0, mutation = 0, style_epoch = 0;
    size_t base_count = 0;
    int old_count = 0;
    bool placeholder = false;
    const char* expected = mask64;
    explicit Fixture(Report& report) : r(report) {}
    ~Fixture() {
        if (candidate_list && candidate_list != old_list) aether_display_list_destroy(candidate_list);
        if (old_list) aether_display_list_destroy(old_list);
        if (candidate && candidate != source && candidate != clone) aether_layout_result_destroy(candidate);
        if (clone) aether_layout_result_destroy(clone);
        if (source) aether_layout_result_destroy(source);
        if (painter) aether_display_list_builder_destroy(painter);
        if (engine) aether_layout_destroy(engine);
        if (map) aether_style_map_destroy(map);
        if (runtime) aether_style_runtime_destroy(runtime);
        if (doc) aether_doc_destroy(doc);
    }
    void ok(aether_status_t s) { r.check(s == AETHER_OK, 0); }
    void init(bool contextual, bool empty_value = false) {
        placeholder = empty_value;
        expected = empty_value ? "TYPE HERE" : mask64;
        measured.expected = expected;
        ok(aether_doc_create(&doc));
        const std::string html = std::string("<html><body><input id='p' type='PaSsWoRd' value='") +
            (empty_value ? "" : value64) + "' placeholder='Type here'></body></html>";
        ok(aether_doc_parse_html(doc, html.c_str()));
        input = aether_doc_get_element_by_id(doc, "p");
        r.check(input != nullptr, 1);
        aether_style_environment_t env{400, 200, 1, 16, 0, 0, 0};
        ok(aether_style_runtime_create(nullptr, &env, nullptr, nullptr, &runtime));
        int64_t sheet = 0;
        ok(aether_style_runtime_add_stylesheet(runtime, aether_style_runtime_defaults_css(),
                                              0, "ua", &sheet, nullptr));
        ok(aether_style_runtime_add_stylesheet(runtime,
            "#p{width:180px;height:40px;text-transform:uppercase;text-shadow:1px 1px red,2px 2px blue;}",
            1, "password-lifetime", &sheet, nullptr));
        if (contextual) {
            aether_style_transaction_t tx = nullptr;
            ok(aether_style_runtime_prepare_revision(runtime, aether_doc_get_document_element(doc), &env, &tx));
            aether_style_revision_t rev = nullptr;
            const auto s = aether_style_runtime_commit_revision(runtime, tx, &rev);
            aether_style_transaction_destroy(tx);
            ok(s);
            const auto map_status = aether_style_revision_create_map(rev, &map);
            aether_style_revision_destroy(rev);
            ok(map_status);
        } else {
            aether_style_update_t* updates = nullptr;
            const int count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_document_element(doc), &updates);
            r.check(count >= 0, 2);
            aether_style_updates_free(updates, count);
            ok(aether_style_runtime_create_resolved_style_map(runtime, doc, &map));
        }
        r.check(map && map->current && map->current->mode() ==
                (contextual ? MapMode::contextual : MapMode::legacy_complete), 3);
        base = map->current;
        base_count = base->backing()->size();
        save_dom(doc, dom);
        structure = doc->identity->structure_epoch;
        mutation = doc->identity->mutation_epoch;
        style_epoch = doc->identity->style_tree_epoch;
        ok(aether_layout_create(measure, &measured, &engine));
        ok(aether_display_list_builder_create(measure, &measured, &painter));
        ok(aether_layout_layout(engine, doc, map, 400, 200, nullptr, nullptr, &source));
        verify_frame(source);
        ok(aether_layout_clone_snapshot(source, &clone));
        verify_frame(clone);
        r.check(source != clone && source->assets == clone->assets, 4);
        ok(aether_display_list_build(painter, source, input, &old_list));
        old_count = aether_display_list_get_count(old_list);
        verify_list(old_list);
        r.check(measured.calls > 0 && measured.wrong == 0, 5);
    }
    void verify_frame(aether_layout_result_t f) {
        r.check(f && f->assets && f->assets->base == base && !f->build_overlay, 6);
        const auto found = f->primary_boxes.find(input);
        r.check(found != f->primary_boxes.end() && found->second, 7);
        r.check(found->second->control_display_text == expected &&
                found->second->control_shows_placeholder == placeholder, 8);
    }
    void verify_list(aether_display_list_t list) {
        r.check(list != nullptr, 13);
        int texts = 0, shadows = 0;
        const int count = aether_display_list_get_count(list);
        r.check(count > 0, 14);
        for (int i = 0; i < count; ++i) {
            aether_paint_command_t cmd{};
            ok(aether_display_list_get_command(list, i, &cmd));
            bool correct = true;
            if (cmd.tag == AETHER_PAINT_DRAW_TEXT) {
                ++texts;
                correct = cmd.data.draw_text.text && std::strcmp(cmd.data.draw_text.text, expected) == 0;
            } else if (cmd.tag == AETHER_PAINT_DRAW_TEXT_SHADOW) {
                ++shadows;
                correct = cmd.data.draw_text_shadow.text && std::strcmp(cmd.data.draw_text_shadow.text, expected) == 0;
            }
            aether_paint_command_free(&cmd);
            r.check(correct, 15);
        }
        r.check(texts == 1 && shadows == 2, 16);
    }
    void intact() {
        r.check(doc->identity->structure_epoch == structure && doc->identity->mutation_epoch == mutation &&
                doc->identity->style_tree_epoch == style_epoch, 17);
        for (const auto& saved : dom) {
            r.check(saved.node->parent == saved.parent && saved.node->type == saved.type &&
                    saved.node->text == saved.text && saved.node->children == saved.children &&
                    saved.node->attributes == saved.attributes && saved.node->identity->alive &&
                    aether_node_identity(saved.node) == saved.identity, 18);
        }
        char* value = aether_element_get_attribute(input, "value");
        const bool value_ok = value && std::strcmp(value, placeholder ? "" : value64) == 0;
        aether_string_free(value);
        r.check(value_ok, 19);
        r.check(map->current == base && base->backing()->size() == base_count, 20);
        verify_frame(source);
        verify_frame(clone);
        r.check(aether_display_list_get_count(old_list) == old_count, 21);
        verify_list(old_list);
        r.check(measured.wrong == 0, 22);
    }
};
enum class Operation { layout, clone, contextual_paint, legacy_paint };
aether_status_t invoke(Fixture& f, Operation op) {
    if (op == Operation::layout)
        return aether_layout_layout(f.engine, f.doc, f.map, 400, 200, nullptr, nullptr, &f.candidate);
    if (op == Operation::clone)
        return aether_layout_clone_snapshot(f.source, &f.candidate);
    return aether_display_list_build(f.painter, f.source, f.input, &f.candidate_list);
}
void release_candidate(Fixture& f) {
    if (f.candidate && f.candidate != f.source && f.candidate != f.clone)
        aether_layout_result_destroy(f.candidate);
    f.candidate = nullptr;
    if (f.candidate_list && f.candidate_list != f.old_list)
        aether_display_list_destroy(f.candidate_list);
    f.candidate_list = nullptr;
}
void verify_candidate(Fixture& f, Operation op) {
    if (op == Operation::layout || op == Operation::clone) {
        f.r.check(f.candidate && f.candidate != f.source && f.candidate != f.clone, 23);
        f.verify_frame(f.candidate);
        f.ok(aether_display_list_build(f.painter, f.candidate, f.input, &f.candidate_list));
    } else {
        f.r.check(f.candidate_list && f.candidate_list != f.old_list, 24);
    }
    f.verify_list(f.candidate_list);
    f.r.check(f.measured.calls > 0 && f.measured.wrong == 0, 25);
}
bool sweep(Operation op, const char* name) {
    Report r{name};
    try {
        for (long ordinal = 0; ordinal < sweep_cap; ++ordinal) {
            r.ordinal = ordinal;
            ++r.attempts;
            // Fresh bounded fixture each attempt; nothing retained across ordinals.
            Fixture f(r);
            f.init(op != Operation::legacy_paint);
            const FrameSaved source_saved(f.source), clone_saved(f.clone);
            f.candidate = f.source;
            f.candidate_list = f.old_list;
            aether_status_t status;
            {
                Armed arm(ordinal);
                status = invoke(f, op);
            }
            r.status = static_cast<int>(status);
            const bool did_inject = injected;
            f.intact();
            source_saved.intact(r);
            clone_saved.intact(r);
            if (status == AETHER_OK) {
                r.check(!did_inject, 26);
                // The irrelevant output sentinel is not owned by the candidate.
                if (op == Operation::layout || op == Operation::clone) f.candidate_list = nullptr;
                else f.candidate = nullptr;
                verify_candidate(f, op);
                release_candidate(f);
                r.completed = true;
                break;
            }
            r.check(did_inject && status == AETHER_ERROR_OUT_OF_MEMORY, 27);
            r.check(f.candidate == f.source && f.candidate_list == f.old_list, 28);
            ++r.oom;
            f.candidate = nullptr;
            f.candidate_list = nullptr;
            // Every injected failure gets an ordinary successful retry using
            // the very same engine, builder, map, document and old artifacts.
            r.check(invoke(f, op) == AETHER_OK, 29);
            verify_candidate(f, op);
            ++r.recoveries;
            release_candidate(f);
            f.intact();
            source_saved.intact(r);
            clone_saved.intact(r);
        }
        r.check(r.completed, 30);
        r.check(r.oom > 0 && r.recoveries == r.oom, 31);
    } catch (const Failed&) {
        // Stable failing check ID/counter survives; remaining sweep is unrun.
    } catch (...) {
        fail_at = -1;
        ++r.failures;
        r.bitmap |= 1ULL << 32;
    }
    r.print();
    return r.completed && r.failures == 0;
}
bool private_contracts() {
    Report r{"private-contracts"};
    try {
        const auto maximum = std::numeric_limits<std::size_t>::max();
        r.check(aether_password_mask_byte_count(0, 0) == 0, 33);
        r.check(aether_password_mask_byte_count(0, 2) == 0, 34);
        r.check(aether_password_mask_byte_count(1, 3) == 3, 35);
        r.check(aether_password_mask_byte_count(2, 6) == 6, 36);
        r.check(aether_password_mask_byte_count(1, maximum) == 3, 37);
        const auto oom = [](std::size_t units, std::size_t limit) {
            try { (void)aether_password_mask_byte_count(units, limit); }
            catch (const std::bad_alloc&) { return true; }
            return false;
        };
        r.check(oom(1, 0) && oom(1, 2), 38);
        r.check(oom(2, 3) && oom(maximum, maximum), 39);
        aether_node_s input;
        input.type = AETHER_NODE_ELEMENT;
        input.tag_name = "input";
        input.attributes["type"] = "password";
        input.attributes["value"] = std::string("A\0B", 3);
        input.attributes["placeholder"] = "Type here";
        aether_computed_style_t style{};
        style.text_transform = AETHER_TEXT_TRANSFORM_UPPERCASE;
        r.check(aether_opaque_control_display_text(&input, style) == "•••", 40);
        r.check(input.attributes.at("value") == std::string("A\0B", 3), 41);
        r.check(aether_opaque_control_display_text(&input, StyleScalars::copy_from(style)) == "•••", 42);
        input.attributes["value"].clear();
        r.check(aether_opaque_control_display_text(&input, style) == "TYPE HERE", 43);
        input.attributes.erase("value");
        r.check(aether_opaque_control_display_text(&input, style) == "TYPE HERE", 44);
        input.attributes.erase("placeholder");
        r.check(aether_opaque_control_display_text(&input, style).empty(), 45);
        for (bool contextual : {false, true}) {
            Fixture f(r);
            f.init(contextual, true);
            f.intact();
        }
        r.completed = true;
    } catch (const Failed&) {
    } catch (...) {
        ++r.failures;
        r.bitmap |= 1ULL << 32;
    }
    std::printf("%s checks=%llu fail=%llu bits=%016llx complete=%d\n",
                r.id, r.checks, r.failures, r.bitmap, r.completed ? 1 : 0);
    return r.completed && r.failures == 0;
}
} // namespace
int main() {
    const bool contracts = private_contracts();
    const bool layout = sweep(Operation::layout, "layout");
    const bool clone = sweep(Operation::clone, "clone");
    const bool contextual = sweep(Operation::contextual_paint, "contextual-paint");
    const bool legacy = sweep(Operation::legacy_paint, "legacy-paint");
    const bool pass = contracts && layout && clone && contextual && legacy;
    std::printf("password-lifetime %s contracts=%d sweeps_complete=%d/4 cap=%ld NDEBUG-safe\n",
                pass ? "PASS" : "FAIL", contracts ? 1 : 0,
                static_cast<int>(layout) + clone + contextual + legacy, sweep_cap);
    return pass ? 0 : 1;
}
