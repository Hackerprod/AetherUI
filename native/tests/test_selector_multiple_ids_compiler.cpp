// Multiple-ID compiler/index integration. All checks remain active under NDEBUG.
#include "style_runtime_types.h"
#include "style_property_registry_internal.h"
#include <cstdio>
#include <stdexcept>
#include <unordered_set>

static style_property_registry_s registry;
style_property_registry_s* style_property_registry_get_global() { return &registry; }
namespace {
int checks = 0;
void require(bool ok, const char* label) {
    ++checks;
    if (!ok) throw std::runtime_error(label);
}
struct Rules {
    std::vector<compiled_rule_s> rules;
    ~Rules() {
        // Media DAG nodes may be shared by selector groups and nested branches.
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
};
void compile(const std::string& css, Rules& out, int32_t& next, int64_t sheet = 701, int layer = 1) {
    require(style_source_compile(css.c_str(), sheet, layer, out.rules, next, nullptr, nullptr) == AETHER_OK,
            "stylesheet compilation status");
}
bool matches(media_condition_s* condition, float width) {
    aether_style_environment_t env{width,600,1,16,0,0,0};
    return media_condition_matches(condition, &env);
}
void same_declarations(const compiled_rule_s& a, const compiled_rule_s& b) {
    require(a.declarations.size() == b.declarations.size(), "property count preserved");
    for (size_t i = 0; i < a.declarations.size(); ++i) {
        const auto& x = a.declarations[i]; const auto& y = b.declarations[i];
        require(x.property_id == y.property_id && x.important == y.important &&
                x.declaration_order == y.declaration_order && x.value.mode == y.value.mode &&
                x.value.typed_value == y.value.typed_value && x.value.deferred_source == y.value.deferred_source,
                "property values, importance and order preserved");
    }
    require(a.custom_declarations.size() == b.custom_declarations.size(), "custom count preserved");
    for (size_t i = 0; i < a.custom_declarations.size(); ++i) {
        const auto& x = a.custom_declarations[i]; const auto& y = b.custom_declarations[i];
        require(x.name == y.name && x.source == y.source && x.important == y.important &&
                x.declaration_order == y.declaration_order, "custom tokens preserved");
    }
    require(a.shorthand_declarations.size() == b.shorthand_declarations.size(), "shorthand count preserved");
    for (size_t i = 0; i < a.shorthand_declarations.size(); ++i) {
        const auto& x = a.shorthand_declarations[i]; const auto& y = b.shorthand_declarations[i];
        require(x.name == y.name && x.source == y.source && x.important == y.important &&
                x.declaration_order == y.declaration_order, "deferred shorthand preserved");
    }
    require(a.content_declarations.size() == b.content_declarations.size(), "content count preserved");
    for (size_t i = 0; i < a.content_declarations.size(); ++i) {
        const auto& x = a.content_declarations[i]; const auto& y = b.content_declarations[i];
        require(x.source == y.source && x.important == y.important && x.declaration_order == y.declaration_order,
                "content string preserved");
    }
}


const std::string declarations = "{width:1px;--Size:37px;width:var(--Size)!important;"
    "image:url('a,b)');box:var(--Size);content:'x,y)'}";

void publication_and_metadata() {
    Rules actual, control;
    int32_t next = 41, reference = 41;
    compile("@media(min-width:600px){@media(max-width:900px){"
            "#a.hot#b#a,.ready" + declarations + "}}#a#a{width:7px}", actual, next);
    compile("@media(min-width:600px){@media(max-width:900px){"
            ".reference,.ready" + declarations + "}}.after{width:7px}", control, reference);
    require(actual.rules.size() == 3 && next == 44 && reference == 44,
            "impossible selector is one published rule, not zero or one per ID");
    const int orders[] = {41,41,43};
    for (size_t i = 0; i < actual.rules.size(); ++i) {
        const auto& rule = actual.rules[i];
        require(rule.rule_id == 41 + static_cast<int>(i) && rule.source_order == orders[i] &&
                rule.sheet_handle == 701 && rule.layer == style_sheet_layer::theme,
                "literal identity and shared group source order");
        same_declarations(rule,control.rules[i]);
        for (float width : {500.f,800.f,1000.f})
            require(matches(rule.condition,width) == (i == 2 || width == 800.f),
                    "nested conditions survive publication of impossible branch");
    }
    require(actual.rules[0].condition == actual.rules[1].condition,
            "group branches share the media DAG");
    require(actual.rules[0].declarations.size() == 3 && actual.rules[0].custom_declarations.size() == 1 &&
            actual.rules[0].shorthand_declarations.size() == 1 && actual.rules[0].content_declarations.size() == 1,
            "declaration parity is nonvacuous");
    const auto& first = actual.rules[0].selector;
    require(first.steps.size() == 1 && first.steps[0].compound.ids == std::vector<std::string>{"a","b","a"},
            "compiler preserves ordered ID occurrences including repeats and contradictions");
    require(first.anchor.id == "a" && first.specificity == specificity_s{3,1,0} &&
            first.dependencies == (SELECTOR_DEP_SELF_ID | SELECTOR_DEP_SELF_CLASS),
            "impossible branch retains specificity, direct anchor and dependencies");
    require(actual.rules[2].selector.steps[0].compound.ids == std::vector<std::string>{"a","a"},
            "duplicate IDs are not deduplicated");
    auto* previous = actual.rules[0].condition;
    compile("#b#a,.after{width:9px}",actual,next,902,2);
    require(actual.rules.size() == 5 && next == 46 && actual.rules[3].rule_id == 44 &&
            actual.rules[4].rule_id == 45 && actual.rules[3].source_order == 44 &&
            actual.rules[4].source_order == 44 && actual.rules[3].sheet_handle == 902 &&
            actual.rules[3].layer == style_sheet_layer::author, "append preserves global IDs across sheets");
    require(actual.rules[0].condition == previous && matches(previous,800.f) && !matches(previous,500.f) &&
            actual.rules[0].rule_id == 41, "append leaves prior publication intact");
}

void logical_metadata_and_recovery() {
    Rules out; int32_t next = 101;
    compile(":is(#a#b:hover,.ready){width:1px}"
            ":where(#a#b:hover,.ready){width:2px}"
            ":not(#a#b){width:3px}"
            ":has(#leaf#leaf){width:4px}"
            ":is(#a#b:unknown,#a#a,.ready){width:5px}"
            ":where(#a#b:unknown,#a#a,.ready){width:6px}"
            ":not(#a#b:unknown,.ready){width:7px}"
            ":has(#a#b:unknown,.ready){width:8px}"
            "#a#b:unknown{width:9px}.after{width:10px}",out,next);
    require(out.rules.size() == 7 && next == 108,
            "valid impossible branches retained, genuinely invalid strict selectors discarded");
    for (size_t i = 0; i < 6; ++i) {
        const auto& selector = out.rules[i].selector;
        require(selector.anchor.is_universal(), "nested IDs never promoted to subject anchor");
        const auto& pseudos = selector.steps[0].compound.pseudos;
        require(pseudos.size() == 1 && pseudos[0].nested &&
                pseudos[0].nested->size() == ((i == 2 || i == 3) ? 1u : 2u),
                "exact logical branch cardinality including forgiving recovery");
        const auto& ids = (*pseudos[0].nested)[0].steps[0].compound.ids;
        require(ids == (i == 3 ? std::vector<std::string>{"leaf","leaf"} :
                        i >= 4 ? std::vector<std::string>{"a","a"} : std::vector<std::string>{"a","b"}),
                "nested AST retains whole ordered vector");
        const specificity_s expected = (i == 1 || i == 5) ? specificity_s{0,0,0} :
                                      i == 0 ? specificity_s{2,1,0} : specificity_s{2,0,0};
        require(selector.specificity == expected, "logical specificity uses all valid branches, WHERE zeros it");
        require((selector.dependencies & SELECTOR_DEP_SELF_ID) != 0,
                "impossible and WHERE branches retain ID dependency");
        require(((selector.dependencies & SELECTOR_DEP_SELF_STATE) != 0) == (i < 2),
                "valid impossible state retained; discarded invalid branch contributes no state");
    }
    require((out.rules[3].selector.dependencies & SELECTOR_DEP_DESCENDANT) != 0,
            "HAS descendant dependency retained");
    require(out.rules.back().selector.anchor.class_name == "after" && out.rules.back().rule_id == 107,
            "strict recovery continues without holes");
}

void anchors_and_candidates() {
    Rules out; int32_t next = 201;
    const char* selectors[] = {"#a#a", "#a#b", "#b#a", "#a.hot#a", ":is(#a#a,.ready)",
        ":where(#a#b,.ready)", ":not(#a#b)", ":has(#leaf#leaf)",
        "#ancestor#ancestor .ready", "#ancestor#ancestor DIV", "#ancestor#ancestor #a#a",
        ".ready:is(#a#a)", "DIV:not(#a#b)", "#ancestor#ancestor + .ready"};
    for (const char* selector : selectors) compile(std::string(selector) + "{width:1px}",out,next);
    require(out.rules.size() == 14 && next == 215, "one rule per selector despite ID multiplicity");
    rule_index_s index;
    for (const auto& rule : out.rules) rule_index_add(&index,&rule);
    require(index.by_id.size() == 2 && index.by_id.at("a").size() == 4 && index.by_id.at("b").size() == 1 &&
            index.by_class.size() == 1 && index.by_class.at("ready").size() == 3 &&
            index.by_type.size() == 1 && index.by_type.at("div").size() == 2 && index.universal.size() == 4,
            "single direct-rightmost bucket per rule; no logical or ancestor ID promotion");
    require(index.by_id.at("b") == std::vector<const compiled_rule_s*>{&out.rules[2]} &&
            out.rules[1].selector.anchor.id == "a" && out.rules[2].selector.anchor.id == "b",
            "first direct ID determines bucket for both contradictory permutations");
    require((index.dependencies & (SELECTOR_DEP_SELF_ID | SELECTOR_DEP_ANCESTOR |
            SELECTOR_DEP_SIBLING | SELECTOR_DEP_DESCENDANT)) ==
            (SELECTOR_DEP_SELF_ID | SELECTOR_DEP_ANCESTOR | SELECTOR_DEP_SIBLING | SELECTOR_DEP_DESCENDANT),
            "index aggregates nested and relational dependencies");
    auto candidates = [&](const char* id, const char* type, std::vector<std::string> classes,
                          const std::vector<int32_t>& expected) {
        node_style_info_s node{id,type,std::move(classes)};
        std::vector<const compiled_rule_s*> found;
        rule_index_get_candidates(&index,&node,found);
        std::vector<int32_t> ids;
        for (auto* rule : found) ids.push_back(rule->rule_id);
        std::sort(ids.begin(),ids.end());
        require(ids == expected, "exact candidate set with no duplicate retrievals");
    };
    candidates("a","DiV",{"ready","ready","hot"},{201,202,204,205,206,207,208,209,210,211,212,213,214});
    candidates("b","span",{},{203,205,206,207,208});
    candidates("ancestor","span",{},{205,206,207,208});
    candidates("leaf","span",{},{205,206,207,208});
    candidates("","span",{"ready","ready"},{205,206,207,208,209,212,214});
    rule_index_clear(&index);
    require(index.by_id.empty() && index.by_class.empty() && index.by_type.empty() &&
            index.universal.empty() && index.dependencies == SELECTOR_DEP_NONE, "clear resets buckets and metadata");
}

void capacity_and_rollback() {
    Rules out; int32_t next = 11;
    compile("@media(min-width:600px){#seed#seed{width:5px}}",out,next);
    auto* prior = out.rules[0].condition;
    for (int cycle = 0; cycle < 32; ++cycle) {
        for (const char* css : {
            "#a#b,.ready{width:8px}",
            "@media(min-width:600px){#a#b,.ready{width:8px}}",
            "#a#a{width:1px}@media(max-width:900px){#a#b{width:8px}}"}) {
            int32_t limited = INT32_MAX - 1;
            require(style_source_compile(css,902,2,out.rules,limited,nullptr,nullptr) == AETHER_ERROR_INVALID_STATE,
                    "impossible selector consumes capacity and group overflow fails");
            require(limited == INT32_MAX - 1 && out.rules.size() == 1 && out.rules[0].rule_id == 11 &&
                    out.rules[0].sheet_handle == 701 && out.rules[0].condition == prior &&
                    out.rules[0].selector.steps[0].compound.ids == std::vector<std::string>{"seed","seed"} &&
                    out.rules[0].declarations[0].value.typed_value == "5px",
                    "abort rolls back whole source, retaining caller AST, metadata, declarations and counter");
            require(matches(prior,800.f) && !matches(prior,500.f), "caller media graph survives rollback");
        }
    }
    int32_t last = INT32_MAX - 1;
    compile("#a#b{width:8px}",out,last,902,2);
    require(last == INT32_MAX && out.rules.size() == 2 && out.rules.back().rule_id == INT32_MAX - 1 &&
            out.rules.back().source_order == INT32_MAX - 1, "impossible rule uses last available identity");
    require(style_source_compile("#a#a{width:1px}",902,2,out.rules,last,nullptr,nullptr) == AETHER_ERROR_INVALID_STATE &&
            last == INT32_MAX && out.rules.size() == 2, "exhausted append preserves output");
    for (const char* empty : {"", "#a#b:unknown{width:1px}", "#a#b{unknown-property:1}"}) {
        compile(empty,out,last);
        require(last == INT32_MAX && out.rules.size() == 2, "truly empty source consumes no exhausted capacity");
    }
}
} // namespace
int main() {
    for (const auto* name : {"width","image","top","right","bottom","left"})
        registry.register_property(name,false,0,"0",
            [](const char* source,std::string& value) { value = source; return true; },
            [](aether_computed_style_t*,const std::string&) {});
    registry.register_shorthand("box",[](const char* source,style_shorthand_writer_s& writer) {
        for (const auto* name : {"top","right","bottom","left"}) writer.set(name,source);
        return true;
    },{"top","right","bottom","left"});
    registry.seal();
    try {
        publication_and_metadata(); logical_metadata_and_recovery(); anchors_and_candidates(); capacity_and_rollback();
        std::printf("selector multiple IDs compiler: %d checks passed\n",checks);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr,"FAIL selector multiple IDs compiler: %s\n",error.what());
        return 1;
    }
}
