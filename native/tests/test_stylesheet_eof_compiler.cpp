// White-box EOF compiler contracts. No assert: checks also run under NDEBUG.
// Graph ownership belongs to this fixture after a successful compilation. Free
// unique reachable nodes iteratively, never recursively destroy a shared DAG.
#include "style_runtime_types.h"
#include "style_property_registry_internal.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

static style_property_registry_s registry;
style_property_registry_s* style_property_registry_get_global() { return &registry; }
namespace {
int checks = 0;
void require(bool ok, const char* label) {
    ++checks;
    if (!ok) throw std::runtime_error(label);
}
struct Owner {
    std::vector<compiled_rule_s> rules;
    void clear() {
        std::vector<media_condition_s*> pending;
        std::unordered_set<media_condition_s*> seen;
        for (const auto& rule : rules) pending.push_back(rule.condition);
        while (!pending.empty()) {
            auto* node = pending.back(); pending.pop_back();
            if (!node || node == media_condition_always() || node == media_condition_never() ||
                node == media_condition_unknown() || !seen.insert(node).second) continue;
            pending.push_back(node->left); pending.push_back(node->right); pending.push_back(node->inner);
        }
        rules.clear();
        for (auto* node : seen) delete node;
    }
    ~Owner() { clear(); }
};
aether_status_t compile(const std::string& css, Owner& out, int32_t& next) {
    return style_source_compile(css.c_str(), 701, 1, out.rules, next, nullptr, nullptr);
}
bool matches(media_condition_s* condition, float width) {
    aether_style_environment_t env{width,600,1,16,0,0,0};
    return media_condition_matches(condition, &env);
}
void metadata() {
    Owner owner; int32_t next = 41;
    const char* css = ".a,.b{width:1px}@media(min-width:1px){.c{width:2px}"
        "@unknown{.hidden{width:99px}}@media(max-width:900px){.d,.e{width:3px}}"
        ".f{width:4px}}.g{width:5px}@media(min-width:900px){.h,.i{width:6px";
    require(compile(css, owner, next) == AETHER_OK, "metadata EOF compile");
    require(owner.rules.size() == 9 && next == 50, "DFS literal count and counter");
    const char* selectors[] = {"a", "b", "c", "d", "e", "f", "g", "h", "i"};
    const int orders[] = {41,41,43,44,44,46,47,48,48};
    const char* widths[] = {"1px","1px","2px","3px","3px","4px","5px","6px","6px"};
    for (size_t i = 0; i < owner.rules.size(); ++i) {
        auto& r = owner.rules[i];
        require(r.rule_id == 41 + static_cast<int>(i) && r.source_order == orders[i], "IDs and group source_order");
        require(r.sheet_handle == 701 && r.layer == style_sheet_layer::theme, "sheet metadata");
        require(r.selector.steps.size() == 1 && r.selector.steps[0].compound.classes.size() == 1 && r.selector.steps[0].compound.classes[0] == selectors[i], "DFS selector order including skipped unknown block");
        require(r.declarations.size() == 1 && r.declarations[0].value.typed_value == widths[i], "EOF declaration exact source");
    }
    require(owner.rules[3].condition == owner.rules[4].condition &&
            owner.rules[7].condition == owner.rules[8].condition, "selector groups share conditions");
    require(matches(owner.rules[3].condition,800) && !matches(owner.rules[3].condition,1000), "nested condition");
    require(!matches(owner.rules[7].condition,800) && matches(owner.rules[7].condition,1000), "inactive EOF rule retained");
    auto* condition = owner.rules[7].condition;
    require(compile(".j{width:7px", owner, next) == AETHER_OK && next == 51 && owner.rules.size() == 10, "append EOF rule");
    require(owner.rules[9].rule_id == 50 && owner.rules[9].source_order == 50 &&
            owner.rules[7].condition == condition, "append preserves identities and graph");
}
struct Diagnostic {
    Owner nested;
    int count = 0;
    int32_t next = 101;
    aether_status_t nested_status = AETHER_ERROR_INVALID_STATE;
};
void diagnostic(const char* code, const char*, const char*, const char*, void* data) {
    auto& state = *static_cast<Diagnostic*>(data);
    if (!code || std::strcmp(code,"AETHER-CSS-LIMIT") != 0) return;
    ++state.count;
    state.nested_status = compile("@media(min-width:900px){.reentrant{width:77px", state.nested, state.next);
}
void rollback() {
    Owner owner; int32_t original_next = 11;
    require(compile("@media(min-width:900px){.old{width:9px}", owner, original_next) == AETHER_OK,
            "seed prior EOF rules");
    require(owner.rules.size() == 1, "seed count");
    auto* old_condition = owner.rules[0].condition;
    // The first new rule stages successfully; the EOF child selector group
    // cannot fit. Neither that staged prefix nor its IDs may be published.
    int32_t limited = INT32_MAX - 2;
    Diagnostic state;
    require(style_source_compile(".prefix{width:1px}@media(min-width:1px){@media(max-width:900px){.x,.y{width:2px",
            999,2,owner.rules,limited,diagnostic,&state) == AETHER_ERROR_INVALID_STATE, "late EOF child fails atomically");
    require(limited == INT32_MAX - 2 && owner.rules.size() == 1, "rollback preserves vector and caller counter");
    require(owner.rules[0].rule_id == 11 && owner.rules[0].source_order == 11 &&
            owner.rules[0].sheet_handle == 701 && owner.rules[0].condition == old_condition &&
            owner.rules[0].selector.steps[0].compound.classes[0] == "old", "rollback preserves caller metadata");
    require(!matches(old_condition,800) && matches(old_condition,1000), "rollback preserves caller graph");
    require(state.count == 1 && state.nested_status == AETHER_OK && state.next == 102 &&
            state.nested.rules.size() == 1 && state.nested.rules[0].rule_id == 101, "diagnostic compile is reentrant");
    require(!matches(state.nested.rules[0].condition,800) && matches(state.nested.rules[0].condition,1000), "reentrant graph survives outer rollback");
    for (int cycle = 0; cycle < 100; ++cycle) {
        Owner empty; int32_t next = INT32_MAX;
        require(compile("@media(min-width:1px){@media(max-width:900px){.empty{unknown-property:1", empty,next) == AETHER_OK &&
                empty.rules.empty() && next == INT32_MAX, "zero-rule EOF cleans unused conditions");
        require(compile("@media(min-width:1px){.a{width:1px", empty,next) == AETHER_ERROR_INVALID_STATE &&
                empty.rules.empty() && next == INT32_MAX, "EOF exhausted counter cleans condition");
    }
}
void lexical_source() {
    Owner owner; int32_t next = 1;
    require(compile(".a/*keep*/.b{width:42px",owner,next) == AETHER_OK && owner.rules.size() == 1,
            "selector comment EOF compiles");
    require(owner.rules[0].selector.steps.size() == 1 && owner.rules[0].selector.steps[0].compound.classes == std::vector<std::string>{"a","b"}, "selector comments preserve original compound identity");
    require(compile(".\\61 {width:43px",owner,next) == AETHER_OK && owner.rules.size() == 2,
            "escaped selector EOF compiles");
    require(owner.rules[1].selector.steps[0].compound.classes[0] == "a", "selector escape terminator preserves identity");
}
void depth(const std::string& mode, int count) {
    std::string css;
    for (int i = 0; i < count; ++i) css += "@media(min-width:1px){";
    css += ".a{width:42px";
    if (mode != "open") css += '}';
    if (mode == "closed") css.append(count,'}');
    else if (mode == "mixed") css.append(count / 2,'}');
    Owner owner; int32_t next = 1;
    std::printf("compiler depth %d %s: compile begin\n",count,mode.c_str()); std::fflush(stdout);
    require(compile(css,owner,next) == AETHER_OK && owner.rules.size() == 1 && next == 2, "deep rule must exist");
    require(owner.rules[0].declarations.size() == 1 && owner.rules[0].declarations[0].value.typed_value == "42px", "deep width preserved");
    std::puts("compiler compile passed; evaluate begin"); std::fflush(stdout);
    require(matches(owner.rules[0].condition,800) && !matches(owner.rules[0].condition,0), "deep real media condition evaluates");
    std::puts("compiler evaluate passed; iterative cleanup begin"); std::fflush(stdout);
    owner.clear();
    std::puts("compiler cleanup passed");
}
} // namespace
int main(int argc, char** argv) {
    registry.register_property("width",false,0,"0px",
        [](const char* source,std::string& value) { value = source; return true; },
        [](aether_computed_style_t*,const std::string&) {});
    registry.seal();
    try {
        if (argc > 1) {
            const std::string mode = argv[1];
            require(mode == "open" || mode == "closed" || mode == "mixed", "expected open, closed or mixed");
            depth(mode,10000);
        } else { metadata(); rollback(); lexical_source(); }
        std::printf("stylesheet EOF compiler: %d checks passed\n",checks); return 0;
    } catch (const std::exception& e) { std::fprintf(stderr,"FAIL stylesheet EOF compiler: %s\n",e.what()); return 1; }
}
