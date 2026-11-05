#include <aether/aether.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
struct Case { const char* id; const char* selector; const char* actual; bool expected; };
// Literal contract oracle, equivalent to the sealed 156-case research matrix.
const Case cases[] = {
    {"c000", "[data-x^=\"\"]", nullptr, false},
    {"c001", "[data-x^=\"\"]", "", false},
    {"c002", "[data-x^=\"\"]", "AbC", false},
    {"c003", "[data-x^=\"\"]", "-tail", false},
    {"c004", "[data-x$=\"\"]", nullptr, false},
    {"c005", "[data-x$=\"\"]", "", false},
    {"c006", "[data-x$=\"\"]", "AbC", false},
    {"c007", "[data-x$=\"\"]", "-tail", false},
    {"c008", "[data-x*=\"\"]", nullptr, false},
    {"c009", "[data-x*=\"\"]", "", false},
    {"c010", "[data-x*=\"\"]", "AbC", false},
    {"c011", "[data-x*=\"\"]", "-tail", false},
    {"c012", "[data-x^=\"Ab\"]", "AbC", true},
    {"c013", "[data-x$=\"bC\"]", "AbC", true},
    {"c014", "[data-x*=\"b\"]", "AbC", true},
    {"c015", "[data-x^=\"ab\"]", "AbC", false},
    {"c016", "[data-x$=\"bc\"]", "AbC", false},
    {"c017", "[data-x*=\"B\"]", "AbC", false},
    {"c018", "[data-x=\"\"]", nullptr, false},
    {"c019", "[data-x=\"\"]", "", true},
    {"c020", "[data-x=\"\"]", "AbC", false},
    {"c021", "[data-x=\"\"]", "-tail", false},
    {"c022", "[data-x~=\"\"]", nullptr, false},
    {"c023", "[data-x~=\"\"]", "", false},
    {"c024", "[data-x~=\"\"]", "AbC", false},
    {"c025", "[data-x~=\"\"]", "-tail", false},
    {"c026", "[data-x|=\"\"]", nullptr, false},
    {"c027", "[data-x|=\"\"]", "", true},
    {"c028", "[data-x|=\"\"]", "AbC", false},
    {"c029", "[data-x|=\"\"]", "-tail", true},
    {"c030", "[data-x^=\"\" i]", nullptr, false},
    {"c031", "[data-x^=\"\" i]", "", false},
    {"c032", "[data-x^=\"\" i]", "AbC", false},
    {"c033", "[data-x^=\"\" i]", "-tail", false},
    {"c034", "[data-x$=\"\" i]", nullptr, false},
    {"c035", "[data-x$=\"\" i]", "", false},
    {"c036", "[data-x$=\"\" i]", "AbC", false},
    {"c037", "[data-x$=\"\" i]", "-tail", false},
    {"c038", "[data-x*=\"\" i]", nullptr, false},
    {"c039", "[data-x*=\"\" i]", "", false},
    {"c040", "[data-x*=\"\" i]", "AbC", false},
    {"c041", "[data-x*=\"\" i]", "-tail", false},
    {"c042", "[data-x^=\"Ab\" i]", "AbC", true},
    {"c043", "[data-x$=\"bC\" i]", "AbC", true},
    {"c044", "[data-x*=\"b\" i]", "AbC", true},
    {"c045", "[data-x^=\"ab\" i]", "AbC", true},
    {"c046", "[data-x$=\"bc\" i]", "AbC", true},
    {"c047", "[data-x*=\"B\" i]", "AbC", true},
    {"c048", "[data-x=\"\" i]", nullptr, false},
    {"c049", "[data-x=\"\" i]", "", true},
    {"c050", "[data-x=\"\" i]", "AbC", false},
    {"c051", "[data-x=\"\" i]", "-tail", false},
    {"c052", "[data-x~=\"\" i]", nullptr, false},
    {"c053", "[data-x~=\"\" i]", "", false},
    {"c054", "[data-x~=\"\" i]", "AbC", false},
    {"c055", "[data-x~=\"\" i]", "-tail", false},
    {"c056", "[data-x|=\"\" i]", nullptr, false},
    {"c057", "[data-x|=\"\" i]", "", true},
    {"c058", "[data-x|=\"\" i]", "AbC", false},
    {"c059", "[data-x|=\"\" i]", "-tail", true},
    {"c060", "[data-x^=\"\" I]", nullptr, false},
    {"c061", "[data-x^=\"\" I]", "", false},
    {"c062", "[data-x^=\"\" I]", "AbC", false},
    {"c063", "[data-x^=\"\" I]", "-tail", false},
    {"c064", "[data-x$=\"\" I]", nullptr, false},
    {"c065", "[data-x$=\"\" I]", "", false},
    {"c066", "[data-x$=\"\" I]", "AbC", false},
    {"c067", "[data-x$=\"\" I]", "-tail", false},
    {"c068", "[data-x*=\"\" I]", nullptr, false},
    {"c069", "[data-x*=\"\" I]", "", false},
    {"c070", "[data-x*=\"\" I]", "AbC", false},
    {"c071", "[data-x*=\"\" I]", "-tail", false},
    {"c072", "[data-x^=\"Ab\" I]", "AbC", true},
    {"c073", "[data-x$=\"bC\" I]", "AbC", true},
    {"c074", "[data-x*=\"b\" I]", "AbC", true},
    {"c075", "[data-x^=\"ab\" I]", "AbC", true},
    {"c076", "[data-x$=\"bc\" I]", "AbC", true},
    {"c077", "[data-x*=\"B\" I]", "AbC", true},
    {"c078", "[data-x=\"\" I]", nullptr, false},
    {"c079", "[data-x=\"\" I]", "", true},
    {"c080", "[data-x=\"\" I]", "AbC", false},
    {"c081", "[data-x=\"\" I]", "-tail", false},
    {"c082", "[data-x~=\"\" I]", nullptr, false},
    {"c083", "[data-x~=\"\" I]", "", false},
    {"c084", "[data-x~=\"\" I]", "AbC", false},
    {"c085", "[data-x~=\"\" I]", "-tail", false},
    {"c086", "[data-x|=\"\" I]", nullptr, false},
    {"c087", "[data-x|=\"\" I]", "", true},
    {"c088", "[data-x|=\"\" I]", "AbC", false},
    {"c089", "[data-x|=\"\" I]", "-tail", true},
    {"c090", "[data-x^=\"\" s]", nullptr, false},
    {"c091", "[data-x^=\"\" s]", "", false},
    {"c092", "[data-x^=\"\" s]", "AbC", false},
    {"c093", "[data-x^=\"\" s]", "-tail", false},
    {"c094", "[data-x$=\"\" s]", nullptr, false},
    {"c095", "[data-x$=\"\" s]", "", false},
    {"c096", "[data-x$=\"\" s]", "AbC", false},
    {"c097", "[data-x$=\"\" s]", "-tail", false},
    {"c098", "[data-x*=\"\" s]", nullptr, false},
    {"c099", "[data-x*=\"\" s]", "", false},
    {"c100", "[data-x*=\"\" s]", "AbC", false},
    {"c101", "[data-x*=\"\" s]", "-tail", false},
    {"c102", "[data-x^=\"Ab\" s]", "AbC", true},
    {"c103", "[data-x$=\"bC\" s]", "AbC", true},
    {"c104", "[data-x*=\"b\" s]", "AbC", true},
    {"c105", "[data-x^=\"ab\" s]", "AbC", false},
    {"c106", "[data-x$=\"bc\" s]", "AbC", false},
    {"c107", "[data-x*=\"B\" s]", "AbC", false},
    {"c108", "[data-x=\"\" s]", nullptr, false},
    {"c109", "[data-x=\"\" s]", "", true},
    {"c110", "[data-x=\"\" s]", "AbC", false},
    {"c111", "[data-x=\"\" s]", "-tail", false},
    {"c112", "[data-x~=\"\" s]", nullptr, false},
    {"c113", "[data-x~=\"\" s]", "", false},
    {"c114", "[data-x~=\"\" s]", "AbC", false},
    {"c115", "[data-x~=\"\" s]", "-tail", false},
    {"c116", "[data-x|=\"\" s]", nullptr, false},
    {"c117", "[data-x|=\"\" s]", "", true},
    {"c118", "[data-x|=\"\" s]", "AbC", false},
    {"c119", "[data-x|=\"\" s]", "-tail", true},
    {"c120", "[data-x^=\"\" S]", nullptr, false},
    {"c121", "[data-x^=\"\" S]", "", false},
    {"c122", "[data-x^=\"\" S]", "AbC", false},
    {"c123", "[data-x^=\"\" S]", "-tail", false},
    {"c124", "[data-x$=\"\" S]", nullptr, false},
    {"c125", "[data-x$=\"\" S]", "", false},
    {"c126", "[data-x$=\"\" S]", "AbC", false},
    {"c127", "[data-x$=\"\" S]", "-tail", false},
    {"c128", "[data-x*=\"\" S]", nullptr, false},
    {"c129", "[data-x*=\"\" S]", "", false},
    {"c130", "[data-x*=\"\" S]", "AbC", false},
    {"c131", "[data-x*=\"\" S]", "-tail", false},
    {"c132", "[data-x^=\"Ab\" S]", "AbC", true},
    {"c133", "[data-x$=\"bC\" S]", "AbC", true},
    {"c134", "[data-x*=\"b\" S]", "AbC", true},
    {"c135", "[data-x^=\"ab\" S]", "AbC", false},
    {"c136", "[data-x$=\"bc\" S]", "AbC", false},
    {"c137", "[data-x*=\"B\" S]", "AbC", false},
    {"c138", "[data-x=\"\" S]", nullptr, false},
    {"c139", "[data-x=\"\" S]", "", true},
    {"c140", "[data-x=\"\" S]", "AbC", false},
    {"c141", "[data-x=\"\" S]", "-tail", false},
    {"c142", "[data-x~=\"\" S]", nullptr, false},
    {"c143", "[data-x~=\"\" S]", "", false},
    {"c144", "[data-x~=\"\" S]", "AbC", false},
    {"c145", "[data-x~=\"\" S]", "-tail", false},
    {"c146", "[data-x|=\"\" S]", nullptr, false},
    {"c147", "[data-x|=\"\" S]", "", true},
    {"c148", "[data-x|=\"\" S]", "AbC", false},
    {"c149", "[data-x|=\"\" S]", "-tail", true},
    {"c150", "[data-x]", nullptr, false},
    {"c151", "[data-x]", "", true},
    {"c152", "[data-x]", "AbC", true},
    {"c153", "[data-x~=\"AbC\"]", "pre AbC post", true},
    {"c154", "[data-x|=\"AbC\"]", "AbC-tail", true},
    {"c155", "[data-x|=\"AbC\"]", "AbCd", false},
};
int failures = 0;
bool check(bool ok, const char* id, const char* what) {
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL %s: %s\n", id, what); }
    return ok;
}

bool attribute_is(aether_node_t node, const char* name, const char* expected) {
    char* value = aether_element_get_attribute(node, name);
    const bool equal = expected ? value && std::strcmp(value, expected) == 0 : value == nullptr;
    aether_string_free(value);
    return equal;
}

void run_case(const Case& c, const std::string& selector, int expected_rules, bool composition = false) {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    aether_style_update_t* updates = nullptr;
    int32_t count = 0;
    auto cleanup = [&] {
        if (updates) aether_style_updates_free(updates, count);
        if (runtime) aether_style_runtime_destroy(runtime);
        if (doc) aether_doc_destroy(doc);
    };
    std::string markup = std::string("<body><div id=\"") + c.id + "\"";
    if (c.actual) markup += std::string(" data-x=\"") + c.actual + "\"";
    if (composition) markup += " data-ok=\"yes\"";
    markup += "></div></body>";
    if (!check(aether_doc_create(&doc) == AETHER_OK, c.id, "document create")) { cleanup(); return; }
    if (!check(aether_doc_parse_html(doc, markup.c_str()) == AETHER_OK, c.id, "HTML parse")) { cleanup(); return; }
    const auto target = aether_doc_get_element_by_id(doc, c.id);
    if (!check(target != nullptr, c.id, "target exists")) { cleanup(); return; }
    check(attribute_is(target, "id", c.id), c.id, "actual ID");
    check(attribute_is(target, "data-x", c.actual), c.id, "actual data-x including absence");
    if (composition) check(attribute_is(target, "data-ok", "yes"), c.id, "actual data-ok");
    aether_style_runtime_options_t options{};
    const aether_style_environment_t environment{640.f, 480.f, 1.f, 16.f, 0, 0, 0};
    if (!check(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime) == AETHER_OK,
               c.id, "runtime create")) { cleanup(); return; }
    int64_t sheet = 0;
    int32_t rules = -1;
    check(aether_style_runtime_add_stylesheet(runtime, "div{width:11px}", 0, "base.css", &sheet, &rules) == AETHER_OK,
          c.id, "base stylesheet status");
    check(rules == 1, c.id, "one compiled base rule");
    const auto css = selector + "{width:37px}";
    rules = -1;
    check(aether_style_runtime_add_stylesheet(runtime, css.c_str(), 0, "attribute.css", &sheet, &rules) == AETHER_OK,
          c.id, "attribute stylesheet status");
    check(rules == expected_rules, c.id, "compiled selector rule count");
    count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_body(doc), &updates);
    check(count > 0 && updates != nullptr, c.id, "initial style updates");
    bool found = false;
    for (int32_t i = 0; updates && i < count; ++i) {
        aether_node_t node = nullptr;
        aether_computed_style_handle_t handle = nullptr;
        const auto status = aether_style_runtime_get_update(updates[i], &node, &handle, nullptr);
        check(status == AETHER_OK && handle, c.id, "read update");
        if (status == AETHER_OK && handle && node == target) {
            aether_computed_style_t style{};
            if (check(aether_computed_style_copy(handle, &style) == AETHER_OK, c.id, "copy computed style")) {
                found = true;
                const float width = style.width.value;
                check(std::fabs(width - (c.expected ? 37.f : 11.f)) < .001f, c.id, "expected computed width");
                check((std::fabs(width - 37.f) < .001f) == c.expected, c.id, "expected match");
                aether_computed_style_free(&style);
            }
        }
        if (handle) aether_computed_style_handle_destroy(handle);
    }
    check(found, c.id, "target received computed style");
    cleanup();
}
} // namespace

int main() {
    static_assert(sizeof(cases) / sizeof(cases[0]) == 156);
    for (const auto& c : cases) run_case(c, std::string("#") + c.id + c.selector, 1);
    const int matrix_failures = failures;
    int guards = 0;
    for (const char* op : {"^=", "$=", "*="}) {
        for (const char* flag : {"", " i", " I", " s", " S"}) {
            const auto attr = std::string("[data-x") + op + "\"\"" + flag + "]";
            const Case guard{"guard", "", "AbC", true};
            run_case(guard, "#guard:not(" + attr + ")", 1, true);
            run_case(guard, "#guard" + attr + ", #guard[data-ok=\"yes\"]", 2, true);
            guards += 2;
        }
    }
    std::printf("MATRIX=156 MATRIX_FAILURES=%d COMPOSITION=%d COMPOSITION_FAILURES=%d TOTAL=186 FAILURES=%d\n",
                matrix_failures, guards, failures - matrix_failures, failures);
    return failures == 0 ? 0 : 1;
}
