// White-box shadow grammar/transaction checks. Deliberately no assert(): all
// checks remain active under NDEBUG. This target needs shadow_values.cpp.
#include "../src/style_runtime/shadow_values.h"
#include "../src/css/measure_state.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int checks = 0, failures = 0;
void check(bool condition, const std::string& label) {
    ++checks;
    if (!condition) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label.c_str()); }
}
void require(bool condition, const char* label) {
    if (!condition) { std::fprintf(stderr, "HARNESS FAIL: %s\n", label); std::exit(2); }
}
bool near(float a, float b) { return std::fabs(a - b) < 0.00001f; }
std::vector<unsigned char> bytes(const aether_shadow* data, size_t count) {
    if (!count) return {};
    const auto* first = reinterpret_cast<const unsigned char*>(data);
    return {first, first + count * sizeof(aether_shadow)};
}
aether_shadow sentinel(float offset) {
    aether_shadow value;
    std::memset(&value, 0, sizeof(value));
    value.offset_x = offset; value.offset_y = -9;
    value.blur_radius = 13; value.spread_radius = -4;
    value.color = {0.125f, 0.25f, 0.5f, 0.75f}; value.inset = AETHER_TRUE;
    return value;
}
struct ParsedSnapshot {
    const aether_shadow* pointer;
    size_t count, capacity, reference_capacity;
    std::vector<unsigned char> contents;
    std::vector<bool> references;
    explicit ParsedSnapshot(const parsed_shadow_list& list)
        : pointer(list.items.data()), count(list.items.size()), capacity(list.items.capacity()),
          reference_capacity(list.uses_current_color.capacity()),
          contents(bytes(list.items.data(), list.items.size())), references(list.uses_current_color) {}
    void unchanged(const parsed_shadow_list& list, const std::string& label) const {
        check(list.items.data() == pointer, label + " parser pointer");
        check(list.items.size() == count && list.items.capacity() == capacity, label + " parser size/capacity");
        check(bytes(list.items.data(), list.items.size()) == contents, label + " parser bytes");
        check(list.uses_current_color == references, label + " parser references");
        check(list.uses_current_color.capacity() == reference_capacity, label + " parser reference capacity");
    }
};

// Function-pointer allocator seam: no global new override or enormous input.
struct AllocationTrace {
    int allocations = 0, releases = 0, old_releases = 0;
    size_t requested = 0;
    bool fail = false;
    aether_shadow* old = nullptr;
    aether_shadow_list* destination = nullptr;
    std::vector<bool>* references = nullptr;
    std::vector<unsigned char> old_bytes;
    std::vector<bool> old_references;
    int32_t old_count = 0;
    std::vector<void*> freed;
};
AllocationTrace* trace = nullptr;
void* allocate_tracked(size_t size) {
    require(trace != nullptr, "allocator trace installed");
    ++trace->allocations; trace->requested = size;
    check(trace->old_releases == 0, "old storage alive during allocation");
    check(trace->destination->items == trace->old && trace->destination->count == trace->old_count,
          "destination unpublished during allocation");
    check(bytes(trace->old, trace->old_count) == trace->old_bytes, "old contents intact during allocation");
    check(*trace->references == trace->old_references, "references unpublished during allocation");
    return trace->fail ? nullptr : std::malloc(size);
}
void release_tracked(void* pointer) {
    require(trace != nullptr, "release trace installed");
    ++trace->releases;
    if (pointer == trace->old) ++trace->old_releases;
    for (auto* freed : trace->freed) {
        if (freed == pointer) { check(false, "allocation released only once"); return; }
    }
    trace->freed.push_back(pointer);
    std::free(pointer);
}
struct Destination {
    aether_shadow_list list{};
    std::vector<bool> references{true, false, true};
    AllocationTrace events;
    size_t reference_capacity;
    Destination() {
        require(trace == nullptr, "single active destination fixture");
        list.count = 3;
        list.items = static_cast<aether_shadow*>(std::malloc(sizeof(aether_shadow) * 3));
        require(list.items != nullptr, "fixture allocation");
        for (int i = 0; i != 3; ++i) list.items[i] = sentinel(float(31 + i));
        references.reserve(131);
        reference_capacity = references.capacity();
        events.old = list.items; events.old_count = list.count;
        events.old_bytes = bytes(list.items, list.count); events.old_references = references;
        events.destination = &list; events.references = &references;
        trace = &events;
    }
    ~Destination() {
        bool already_freed = false;
        for (auto* pointer : events.freed) if (pointer == list.items) already_freed = true;
        if (!already_freed) std::free(list.items);
        trace = nullptr;
    }
    shadow_storage_policy policy() const {
        shadow_storage_policy result;
        result.allocate = allocate_tracked; result.release = release_tracked;
        return result;
    }
    void unchanged(const std::string& label) const {
        check(list.items == events.old, label + " destination pointer");
        check(list.count == events.old_count, label + " destination count");
        // Avoid dereferencing a dangling pointer when testing a broken implementation.
        if (events.old_releases == 0 && list.items == events.old && list.count == events.old_count)
            check(bytes(list.items, list.count) == events.old_bytes, label + " destination bytes");
        else check(false, label + " destination bytes unavailable");
        check(references == events.old_references, label + " reference values/order/size");
        check(references.capacity() == reference_capacity, label + " reference capacity");
        check(events.releases == 0, label + " no release");
    }
};

struct InvalidCase { const char* source; shadow_value_error error; };
void invalid_grammar() {
    const char* cases[] = {
        "1px red 2px", "1px currentColor 2px", "1px #123456 2px",
        "1px rgb(255,0,0) 2px", "1px 2px red 3px", "1px 2px currentColor 3px",
        "1px inset 2px", "1px 2px inset 3px", "1px 2px 3px red 4px",
        "1px 2px 3px inset 4px", "1px 2px red inset 3px", "inset 1px red 2px",
        "1px red", "1px currentColor", "1px inset", "1px red red",
        "1px red 2px, 3px 4px", "3px 4px, 1px red 2px, 5px 6px",
        "3px 4px, 1px red 2px",
        "", " ", "none, 1px 2px", "1px 2px, none", "none 1px 2px", "1px",
        "red", "1px 2px,", ",1px 2px", "1px 2px,,3px 4px", "1px 2px,   ",
        "1px 2px -3px", "1px 2px red blue", "1px 2px currentColor red",
        "1px 2px currentColor currentColor", "inset inset 1px 2px", "1px 2px 3px 4px 5px",
        "auto 2px", "1% 2px", "1px 2px 3%", "NaNpx 2px", "infinity 2px",
        "1px 2px rgb(0 128 255 / 50%)", "1px 2px rgb(1, 2)", "1px 2px rgb(1 2 3 /)", "1px 2px rgb(1, 2, 3",
        "1px 2px rgb(1, 2, 3))", "1px 2px unknown", "1px 2px;", "1px 2px !important",
        "1px 2px, 3px", "1px 2px red, 3px 4px rgb(1 2 / 3)"
    };
    for (bool box : {false, true}) for (const auto* source : cases) {
        // Text inset has its own error and is checked separately below.
        if (!box && std::strstr(source, "inset")) continue;
        const std::string label = std::string(box ? "box invalid: " : "text invalid: ") + source;
        parsed_shadow_list output{{sentinel(21), sentinel(22)}, {false, true}};
        output.items.reserve(9); output.uses_current_color.reserve(131);
        ParsedSnapshot before(output);
        const auto expected = !box && std::strcmp(source, "1px 2px 3px 4px 5px") == 0
            ? shadow_value_error::spread_not_allowed : shadow_value_error::invalid;
        check(shadow_list_parse(source, box, output) == expected, label);
        before.unchanged(output, label);
        Destination destination;
        check(shadow_list_apply(source, box, destination.list, destination.references, destination.policy()) ==
              expected, label + " apply error");
        destination.unchanged(label);
        check(destination.events.allocations == 0, label + " no allocation");
    }
    for (const auto& item : {
        InvalidCase{"inset 1px 2px", shadow_value_error::inset_not_allowed},
        InvalidCase{"1px 2px, inset 3px 4px", shadow_value_error::inset_not_allowed},
        InvalidCase{"1px 2px 3px 4px", shadow_value_error::spread_not_allowed},
        InvalidCase{"1px 2px, 3px 4px 5px 6px", shadow_value_error::spread_not_allowed}}) {
        parsed_shadow_list output{{sentinel(21)}, {false}};
        ParsedSnapshot before(output);
        check(shadow_list_parse(item.source, false, output) == item.error, item.source);
        before.unchanged(output, item.source);
        Destination destination;
        check(shadow_list_apply(item.source, false, destination.list, destination.references, destination.policy()) == item.error,
              "text-specific apply error");
        destination.unchanged(item.source);
        check(destination.events.allocations == 0, "policy rejection before allocation");
        check(shadow_list_parse(item.source, true, output) == shadow_value_error::none, "box allows inset/spread");
    }
}

void valid_grammar() {
    for (bool box : {false, true}) {
        parsed_shadow_list output;
        check(shadow_list_parse("rgb(255, 0, 128) -1px 2px 3px, 4px 5px currentColor, 6px 7px, "
                                "8px 9px rgba(0,128,255,50%)", box, output) == shadow_value_error::none,
              "complete mixed list with nested RGB/RGBA commas");
        require(output.items.size() == 4, "mixed list parsed");
        check(output.uses_current_color == std::vector<bool>({false, true, true, false}), "explicit/default/currentColor order");
        const auto& first = output.items[0];
        check(first.offset_x == -1 && first.offset_y == 2 && first.blur_radius == 3 && first.spread_radius == 0,
              "negative offset and length positions");
        check(near(first.color.r, 1) && near(first.color.g, 0) && near(first.color.b, 128.f/255) && near(first.color.a, 1),
              "legacy RGB color");
        check(output.items[1].offset_x == 4 && output.items[2].offset_x == 6 && output.items[3].offset_x == 8,
              "component order");
        check(output.items[1].blur_radius == 0 && output.items[1].spread_radius == 0 && !output.items[1].inset,
              "omitted lengths and inset defaults");
        check(near(output.items[3].color.g, 128.f/255) && near(output.items[3].color.b, 1) && near(output.items[3].color.a, .5f),
              "independent percentage alpha");
        check(shadow_list_parse("0 0 transparent", box, output) == shadow_value_error::none, "unitless zeros/transparent");
        require(output.items.size() == 1, "transparent parsed");
        check(!output.uses_current_color[0] && output.items[0].color.a == 0, "transparent is explicit color");
        check(shadow_list_parse(" \tNoNe\n", box, output) == shadow_value_error::none, "trimmed case insensitive none");
        check(output.items.empty() && output.uses_current_color.empty(), "none replaces parsed output");
    }
    parsed_shadow_list box;
    check(shadow_list_parse("InSeT #ff0000 -1px -2px 0 -4px", true, box) == shadow_value_error::none,
          "box negative spread and mixed keyword/color placement");
    require(box.items.size() == 1, "box inset parsed");
    check(box.items[0].inset && box.items[0].spread_radius == -4 && !box.uses_current_color[0], "box fields");
}

void ordered_groups() {
    // Each complete length group remains adjacent in all six box permutations.
    for (const std::string color : {"red", "currentColor"}) {
        for (int count : {2, 3, 4}) {
            const std::string lengths = "-1px 2px" + std::string(count >= 3 ? " 3px" : "") +
                                        (count == 4 ? " -4px" : "");
            for (const std::string source : {color + " inset " + lengths, "inset " + color + " " + lengths,
                 color + " " + lengths + " inset", "inset " + lengths + " " + color,
                 lengths + " " + color + " inset", lengths + " inset " + color}) {
                parsed_shadow_list output;
                require(shadow_list_parse(source, true, output) == shadow_value_error::none, "box permutation parses");
                require(output.items.size() == 1 && output.uses_current_color.size() == 1, "box permutation size");
                const auto& v = output.items[0];
                check(v.offset_x == -1 && v.offset_y == 2 && v.blur_radius == (count >= 3 ? 3 : 0) &&
                      v.spread_radius == (count == 4 ? -4 : 0) && v.inset, source + " fields");
                check(output.uses_current_color[0] == (color == "currentColor"), source + " symbol");
                if (color == "red") check(v.color.r == 1 && v.color.g == 0 && v.color.b == 0 && v.color.a == 1, source + " color");
            }
            if (count == 4) continue;
            for (const std::string source : {color + " " + lengths, lengths + " " + color}) {
                parsed_shadow_list output;
                require(shadow_list_parse(source, false, output) == shadow_value_error::none, "text permutation parses");
                require(output.items.size() == 1 && output.uses_current_color.size() == 1, "text permutation size");
                const auto& v = output.items[0];
                check(v.offset_x == -1 && v.offset_y == 2 && v.blur_radius == (count == 3 ? 3 : 0) &&
                      v.spread_radius == 0 && !v.inset, source + " text fields");
                check(output.uses_current_color[0] == (color == "currentColor"), source + " text symbol");
                if (color == "red") check(v.color.r == 1 && v.color.g == 0 && v.color.b == 0 && v.color.a == 1, source + " text color");
            }
        }
    }
    for (bool box : {false, true}) {
        parsed_shadow_list output;
        const measure_inputs_s measures{23, 800, 600, 17, 31};
        require(shadow_list_parse("1em 2rem calc(2px - 5px) currentColor, rgb(255, 0, 0) calc(1px + 2px) 0",
                                 box, output, {}, &measures) == shadow_value_error::none, "atomic functions and state reset");
        require(output.items.size() == 2 && output.uses_current_color.size() == 2, "contextual list size");
        check(output.items[0].offset_x == 23 && output.items[0].offset_y == 62 && output.items[0].blur_radius == 0,
              "contextual units and preserved negative calc blur clamp");
        check(output.items[1].offset_x == 3 && output.items[1].offset_y == 0 && output.items[1].color.r == 1 &&
              output.items[1].color.g == 0 && output.items[1].color.b == 0 && output.items[1].color.a == 1,
              "second component independent order and atomic calc/RGB");
        check(output.uses_current_color == std::vector<bool>({true, false}), "reset preserves distinct symbols");
    }
}

void apply_transactions() {
    for (bool box : {false, true}) {
        {
            Destination destination;
            destination.events.fail = true;
            check(shadow_list_apply("1px 2px red, 3px 4px currentColor", box, destination.list,
                                    destination.references, destination.policy()) == shadow_value_error::allocation,
                  "injected allocation failure");
            destination.unchanged("allocation failure");
            check(destination.events.allocations == 1, "one failed allocation attempt");
        }
        for (bool missing_allocator : {false, true}) {
            Destination destination;
            auto policy = destination.policy();
            if (missing_allocator) policy.allocate = nullptr; else policy.release = nullptr;
            check(shadow_list_apply("1px 2px", box, destination.list, destination.references, policy) == shadow_value_error::allocation,
                  "missing storage callback rejected");
            destination.unchanged("missing callback");
            check(destination.events.allocations == 0, "missing callback does not allocate");
        }
        {
            Destination destination;
            check(shadow_list_apply("1px 2px red, 3px 4px currentColor, 5px 6px, 7px 8px blue", box,
                                    destination.list, destination.references, destination.policy()) == shadow_value_error::none,
                  "successful replacement");
            check(destination.events.allocations == 1 && destination.events.requested == 4 * sizeof(aether_shadow), "exact replacement allocation");
            check(destination.events.releases == 1 && destination.events.old_releases == 1, "success releases old once");
            check(destination.list.items != destination.events.old && destination.list.count == 4, "publishes replacement pointer/count");
            check(destination.references == std::vector<bool>({false, true, true, false}), "success replaces reference list");
            require(destination.list.items && destination.list.count == 4, "replacement readable");
            check(destination.list.items[0].offset_x == 1 && destination.list.items[3].offset_y == 8,
                  "replacement component order");
            check(near(destination.list.items[0].color.r, 1) && near(destination.list.items[3].color.b, 1), "replacement colors");
        }
        {
            Destination destination;
            destination.events.fail = true;
            check(shadow_list_apply(" none ", box, destination.list, destination.references, destination.policy()) == shadow_value_error::none,
                  "none succeeds without allocator");
            check(destination.list.items == nullptr && destination.list.count == 0 && destination.references.empty(), "canonical empty commit");
            check(destination.events.releases == 1 && destination.events.old_releases == 1, "none frees old exactly once");
            check(destination.events.allocations == 0, "none does not allocate");
            check(shadow_list_apply("none", box, destination.list, destination.references, destination.policy()) == shadow_value_error::none,
                  "repeat none succeeds");
            check(destination.events.releases == 1 && destination.events.allocations == 0, "repeat none no allocation/release");
        }
    }
}

void count_guards() {
    for (bool box : {false, true}) for (bool bytes_limit : {false, true}) {
        Destination destination;
        auto policy = destination.policy();
        if (bytes_limit) policy.max_bytes = 2 * sizeof(aether_shadow) - 1; else policy.max_count = 1;
        parsed_shadow_list output{{sentinel(99)}, {false}};
        ParsedSnapshot before(output);
        check(shadow_list_parse("1px 2px, 3px 4px", box, output, policy) == shadow_value_error::too_many,
              bytes_limit ? "byte/multiplication guard" : "count representation guard");
        before.unchanged(output, "count guard");
        check(shadow_list_apply("1px 2px, 3px 4px", box, destination.list, destination.references, policy) == shadow_value_error::too_many,
              "apply count guard");
        destination.unchanged("count guard");
        check(destination.events.allocations == 0, "count guard before allocation");
        check(shadow_list_parse("1px 2px", box, output, policy) == shadow_value_error::none, "one item within limits");
        if (bytes_limit) policy.max_bytes = 2 * sizeof(aether_shadow); else policy.max_count = 2;
        check(shadow_list_parse("1px 2px, 3px 4px", box, output, policy) == shadow_value_error::none, "exact inclusive limit accepted");
        if (bytes_limit) policy.max_bytes = sizeof(aether_shadow) - 1; else policy.max_count = 0;
        check(shadow_list_parse("1px 2px", box, output, policy) == shadow_value_error::too_many, "zero effective capacity rejects one item");
        check(shadow_list_parse("none", box, output, policy) == shadow_value_error::none, "none allowed at zero capacity");
        check(output.items.empty() && output.uses_current_color.empty(), "zero capacity none clears output");
    }
}

void rejected_blur(const std::string& source, bool box, const measure_inputs_s* measures) {
    const std::string label = std::string(box ? "box blur: " : "text blur: ") + source;
    parsed_shadow_list output{{sentinel(21), sentinel(22)}, {false, true}};
    output.items.reserve(9); output.uses_current_color.reserve(131);
    ParsedSnapshot before(output);
    check(shadow_list_parse(source, box, output, {}, measures) == shadow_value_error::invalid, label);
    before.unchanged(output, label);
    Destination destination;
    check(shadow_list_apply(source, box, destination.list, destination.references,
                            destination.policy(), measures) == shadow_value_error::invalid, label + " apply");
    destination.unchanged(label);
    check(destination.events.allocations == 0 && destination.events.requested == 0,
          label + " rejects before allocation");
}

void literal_blur_sign() {
    const measure_inputs_s zero{0, 0, 0, 0, 0};
    const measure_inputs_s font23{23, 800, 600, 17, 31};
    const measure_inputs_s negative_font{-23, 800, 600, 17, 31};
    struct Rejected { const char* source; const measure_inputs_s* measures; };
    // Representative units and contexts, not their Cartesian product. A valid
    // first shadow must not publish when the second blur fails.
    const Rejected rejected[] = {
        {"1px 2px -1e-46px currentColor", nullptr},
        {"1px 2px -1e-46 currentColor", nullptr},
        {"1px 2px -1e-46em", &zero},
        {"1px 2px -1e-46rem", &font23},
        {"1px 2px -1e-46vw", &zero},
        {"1px 2px -1e-46vh", &font23},
        {"1px 2px red, 3px 4px -1e-46px currentColor", nullptr},
        {"1px 2px currentColor, 3px 4px -1e-46em red", &zero},
        {"1px 2px -1e-400px", nullptr},
        {"1px 2px 1e400px", nullptr},
        {"1px 2px -1e-46pxjunk", &font23},
        {"1px 2px 2em", &negative_font},
        {"1px 2px 3e38em", nullptr},
        {"1px 2px calc(0)", nullptr},
        {"1px 2px calc(2)", &font23},
        {"1px 2px calc(1 - 1)", &zero}
    };
    struct Accepted {
        const char* source;
        const measure_inputs_s* measures;
        float x, y, blur, spread;
    };
    const Accepted accepted[] = {
        {"1px 2px -0px currentColor", nullptr, 1, 2, 0, 0},
        {"1px 2px 1e-46px currentColor", nullptr, 1, 2, 0, 0},
        {"1px 2px -0em currentColor", &font23, 1, 2, 0, 0},
        {"1px 2px 1e-46rem currentColor", &font23, 1, 2, 0, 0},
        {"1px 2px 2em currentColor", &font23, 1, 2, 46, 0},
        {"1px 2px 2em currentColor", &zero, 1, 2, 0, 0},
        // The private parser intentionally retains legacy Number-as-px.
        {"1 2 2 currentColor", nullptr, 1, 2, 2, 0},
        {"1 2 1e-46 currentColor", &font23, 1, 2, 0, 0},
        {"1 2 -0 currentColor", nullptr, 1, 2, 0, 0},
        {"-2px -1e-46px 2px currentColor", nullptr, -2, 0, 2, 0},
        {"-1e-46em -2px 2px currentColor", &font23, 0, -2, 2, 0},
        {"1px 2px calc(2px - 5px) currentColor", nullptr, 1, 2, 0, 0},
        {"1px 2px calc(2px - 5px) currentColor", &font23, 1, 2, 0, 0},
        {"1px 2px calc(2em) currentColor", &negative_font, 1, 2, 0, 0}
    };
    for (bool box : {false, true}) {
        for (const auto& item : rejected) rejected_blur(item.source, box, item.measures);
        for (const auto& item : accepted) {
            parsed_shadow_list output;
            require(shadow_list_parse(item.source, box, output, {}, item.measures) == shadow_value_error::none,
                    "accepted blur parse");
            require(output.items.size() == 1, "accepted blur parse count");
            const auto fields = [&](const aether_shadow& s) {
                return s.offset_x == item.x && s.offset_y == item.y && s.blur_radius == item.blur &&
                       s.spread_radius == item.spread && !s.inset;
            };
            check(fields(output.items[0]), std::string(item.source) + " exact parsed fields");
            check(output.uses_current_color == std::vector<bool>{true}, "accepted blur parsed currentColor");
            Destination destination;
            require(shadow_list_apply(item.source, box, destination.list, destination.references,
                                      destination.policy(), item.measures) == shadow_value_error::none,
                    "accepted blur apply");
            require(destination.list.items && destination.list.count == 1, "accepted blur apply count");
            check(fields(destination.list.items[0]), std::string(item.source) + " exact applied fields");
            check(destination.references == std::vector<bool>{true}, "accepted blur applied currentColor");
            check(destination.events.allocations == 1 && destination.events.requested == sizeof(aether_shadow) &&
                  destination.events.releases == 1 && destination.events.old_releases == 1,
                  "accepted blur replacement storage");
        }
        // Contextual conversion already throws on a non-finite result. Preserve
        // that contract rather than pretending it is the literal invalid code.
        const measure_inputs_s nonfinite{std::numeric_limits<float>::infinity(), 800, 600, 17, 31};
        parsed_shadow_list output{{sentinel(21), sentinel(22)}, {true, false}};
        ParsedSnapshot before(output);
        bool parse_threw = false;
        try {
            const auto result = shadow_list_parse("1px 2px 2em", box, output, {}, &nonfinite);
            check(false, "non-finite parse must throw, returned " + std::to_string(int(result)));
        } catch (const std::overflow_error&) { parse_threw = true; }
        check(parse_threw, "non-finite contextual parse guard");
        before.unchanged(output, "non-finite contextual parse");
        Destination destination;
        bool apply_threw = false;
        try {
            const auto result = shadow_list_apply("1px 2px 2em", box, destination.list, destination.references,
                                                  destination.policy(), &nonfinite);
            check(false, "non-finite apply must throw, returned " + std::to_string(int(result)));
        } catch (const std::overflow_error&) { apply_threw = true; }
        check(apply_threw, "non-finite contextual apply guard");
        destination.unchanged("non-finite contextual apply");
        check(destination.events.allocations == 0, "non-finite context before allocation");
    }
    for (const auto* spread : {"-2px", "-1e-46px"}) {
        const std::string source = std::string("-2px 3px 4px ") + spread + " currentColor";
        parsed_shadow_list output;
        require(shadow_list_parse(source, true, output, {}, &font23) == shadow_value_error::none,
                "negative spread parse");
        require(output.items.size() == 1, "negative spread count");
        check(output.items[0].offset_x == -2 && output.items[0].spread_radius ==
              (std::strcmp(spread, "-2px") == 0 ? -2 : 0), "negative spread exact fields");
        Destination destination;
        require(shadow_list_apply(source, true, destination.list, destination.references,
                                  destination.policy(), &font23) == shadow_value_error::none,
                "negative spread apply");
        require(destination.list.items && destination.list.count == 1, "negative spread applied count");
        check(destination.list.items[0].offset_x == -2 && destination.list.items[0].spread_radius ==
              (std::strcmp(spread, "-2px") == 0 ? -2 : 0), "negative spread applied exact fields");
        check(destination.references == std::vector<bool>{true}, "negative spread keeps currentColor");
    }
}
}

int main() {
    invalid_grammar();
    valid_grammar();
    ordered_groups();
    apply_transactions();
    count_guards();
    literal_blur_sign();
    check(std::strcmp(shadow_value_diagnostic_code(true), "AETHER-CSS-BOX-SHADOW") == 0, "box diagnostic code");
    check(std::strcmp(shadow_value_diagnostic_code(false), "AETHER-CSS-TEXT-SHADOW") == 0, "text diagnostic code");
    std::printf("Shadow values: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
