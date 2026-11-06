// Private compiler/index integration. All checks remain active under NDEBUG.
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

void empty_pseudo(const compiled_selector_s& selector, pseudo_selector_kind_t kind) {
    require(selector.steps.size() == 1, "empty pseudo retains its selector step");
    const auto& compound = selector.steps[0].compound;
    require(compound.pseudos.size() == 1 && compound.pseudos[0].kind == kind &&
            compound.pseudos[0].nested && compound.pseudos[0].nested->empty(),
            "empty forgiving argument is present and has no sentinel branch");
    require(compound.ids.empty() && compound.type_name.empty() && compound.attributes.empty(),
            "discarded branches leave no outer simple selectors");
}
void group_identity_and_media() {
    const char* selectors[] = {":is()", ":where()", ":not(:is())", ":not(:where())",
        ":is(#ghost:hover:unknown)", ":where(:has(#ghost:hover,:unknown))"};
    for (const char* selector : selectors) {
        for (bool media : {false,true}) {
            Rules actual, expected;
            int32_t next = 41, reference = 41;
            const std::string prefix = media ? "@media(min-width:600px){@media(max-width:900px){" : "";
            const std::string suffix = media ? "}}" : "";
            compile(prefix + selector + ",.safe" + declarations + suffix + ".after{width:7px}", actual, next);
            compile(prefix + ".reference,.safe" + declarations + suffix + ".after{width:7px}", expected, reference);
            require(actual.rules.size() == 3 && next == 44 && reference == 44,
                    "valid empty argument consumes a normal rule ID");
            const int orders[] = {41,41,43};
            for (size_t i = 0; i < actual.rules.size(); ++i) {
                const auto& rule = actual.rules[i];
                require(rule.rule_id == 41 + static_cast<int>(i) && rule.source_order == orders[i] &&
                        rule.sheet_handle == 701 && rule.layer == style_sheet_layer::theme,
                        "group IDs N/N+1/N+2 and source orders N/N/N+2");
                same_declarations(rule,expected.rules[i]);
                for (float width : {500.f,800.f,1000.f}) {
                    const bool active = !media || i == 2 || width == 800.f;
                    require(matches(rule.condition,width) == active,
                            "empty and ordinary group branches share active and inactive media");
                }
            }
            const auto& first = actual.rules[0];
            require(first.condition == actual.rules[1].condition,
                    "group shares the same media DAG pointer");
            require(first.declarations.size() == 3 && first.custom_declarations.size() == 1 &&
                    first.shorthand_declarations.size() == 1 && first.content_declarations.size() == 1,
                    "all declaration preservation comparisons are nonvacuous");
            require(first.selector.anchor.is_universal() && first.selector.specificity == specificity_s{0,0,0} &&
                    first.selector.dependencies == SELECTOR_DEP_NONE,
                    "empty argument has universal anchor, zero specificity and no discarded dependencies");
            if (std::string(selector).find(":not(") == 0) {
                const auto& pseudos = first.selector.steps[0].compound.pseudos;
                require(pseudos.size() == 1 && pseudos[0].kind == PSEUDO_NOT && pseudos[0].nested &&
                        pseudos[0].nested->size() == 1, "NOT retains its one valid empty-argument branch");
                empty_pseudo((*pseudos[0].nested)[0],
                    std::string(selector).find(":where") != std::string::npos ? PSEUDO_WHERE : PSEUDO_IS);
            } else {
                empty_pseudo(first.selector,std::string(selector).find(":where") == 0 ? PSEUDO_WHERE : PSEUDO_IS);
            }
        }
    }
}
void append_and_strict() {
    Rules actual; int32_t next = 101;
    compile("@media(min-width:600px){.before{width:5px}}", actual, next, 700, 0);
    require(actual.rules.size() == 1 && next == 102, "existing caller prefix compiled");
    auto* condition = actual.rules[0].condition;
    for (int repetition = 0; repetition < 32; ++repetition) {
        compile("@media(min-width:1px){@media(max-width:900px){"
                ":has(){width:1px}:not(){width:1px}:has(.safe,:unknown){width:1px}"
                ":not(.safe,){width:1px}:has(:where(),:unknown){width:1px}"
                ":not(:is(),){width:1px}}}", actual, next, 702, 2);
        require(actual.rules.size() == 1 && next == 102 && actual.rules[0].condition == condition,
                "strict rejected sheets consume no IDs and preserve caller media DAG");
    }
    compile("@media(min-width:600px){@media(max-width:900px){"
            ":is(),.safe" + declarations + "}}.after{width:7px}", actual, next, 703, 2);
    require(actual.rules.size() == 4 && next == 105, "append retains prefix and adds all three rules");
    const int orders[] = {102,102,104};
    for (size_t i = 1; i < actual.rules.size(); ++i) {
        require(actual.rules[i].rule_id == 101 + static_cast<int>(i) &&
                actual.rules[i].source_order == orders[i-1] && actual.rules[i].sheet_handle == 703 &&
                actual.rules[i].layer == style_sheet_layer::author, "appended rule identity");
    }
    require(actual.rules[0].rule_id == 101 && actual.rules[0].source_order == 101 &&
            actual.rules[0].sheet_handle == 700 && actual.rules[0].condition == condition &&
            actual.rules[0].selector.anchor.class_name == "before" &&
            actual.rules[0].declarations.size() == 1 &&
            actual.rules[0].declarations[0].value.typed_value == "5px",
            "append preserves previous selector, declaration, identity and condition");
    require(!matches(condition,500.f) && matches(condition,800.f), "previous media remains usable after append");
    require(actual.rules[1].condition == actual.rules[2].condition, "appended group shares DAG");
    same_declarations(actual.rules[1],actual.rules[2]);
}
void metadata_and_index() {
    Rules actual; int32_t next = 201;
    compile(":is(#ghost:hover:unknown){width:1px}"
            ":where(:has(#ghost:hover,:unknown)){width:2px}"
            ":not(:is()){width:3px}"
            ".item:not(:is(#ghost:hover:unknown)){width:4px}"
            ".safe{width:5px}"
            ":is(.safe,#ghost:is()){width:6px}"
            ":where(.safe,#ghost:is()){width:7px}", actual, next);
    require(actual.rules.size() == 7 && next == 208, "all syntactically valid selectors retained");
    for (size_t i = 0; i < 5; ++i) {
        const auto& s = actual.rules[i].selector;
        require(s.dependencies == (i < 3 ? SELECTOR_DEP_NONE : SELECTOR_DEP_SELF_CLASS) &&
                s.specificity == specificity_s{0,i < 3 ? 0 : 1,0},
                "discarded ID, state and descendant branches leave no metadata");
        require(s.anchor.id.empty() && s.anchor.type_name.empty() &&
                s.anchor.class_name == (i < 3 ? "" : i == 3 ? "item" : "safe"),
                "only outer selectors determine anchor");
    }
    empty_pseudo(actual.rules[0].selector,PSEUDO_IS);
    empty_pseudo(actual.rules[1].selector,PSEUDO_WHERE);
    for (size_t i : {size_t(5),size_t(6)}) {
        const auto& s = actual.rules[i].selector;
        require(s.anchor.is_universal() &&
                s.dependencies == (SELECTOR_DEP_SELF_CLASS | SELECTOR_DEP_SELF_ID) &&
                s.specificity == (i == 5 ? specificity_s{1,0,0} : specificity_s{0,0,0}),
                "valid impossible branch retains metadata, WHERE still zeros specificity");
        const auto& p = s.steps[0].compound.pseudos[0];
        require(p.nested && p.nested->size() == 2 && (*p.nested)[1].steps[0].compound.ids == std::vector<std::string>{"ghost"},
                "valid impossible branch is not discarded or flattened");
    }
    rule_index_s index;
    for (const auto& rule : actual.rules) rule_index_add(&index,&rule);
    require(index.by_id.empty() && index.by_type.empty() && index.by_class.size() == 2 &&
            index.universal.size() == 5 &&
            index.dependencies == (SELECTOR_DEP_SELF_CLASS | SELECTOR_DEP_SELF_ID),
            "universal and outer-class buckets, no nested-anchor promotion or discarded state dependency");
    require(index.by_class.at("item") == std::vector<const compiled_rule_s*>{&actual.rules[3]} &&
            index.by_class.at("safe") == std::vector<const compiled_rule_s*>{&actual.rules[4]},
            "class buckets contain only their outer anchored rule");
    node_style_info_s plain{"ghost","div",{}};
    std::vector<const compiled_rule_s*> candidates;
    rule_index_get_candidates(&index,&plain,candidates);
    require(candidates == index.universal, "universal candidates retained even for selectors that cannot match");
    candidates.clear();
    node_style_info_s classes{"ghost","div",{"item","safe"}};
    rule_index_get_candidates(&index,&classes,candidates);
    require(candidates.size() == 7 && std::unordered_set<const compiled_rule_s*>(candidates.begin(),candidates.end()).size() == 7,
            "class plus universal candidates retain every rule once");
    rule_index_clear(&index);
    require(index.dependencies == SELECTOR_DEP_NONE && index.universal.empty() && index.by_class.empty() &&
            index.by_id.empty() && index.by_type.empty(), "index cleanup resets every bucket and dependency");
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
        group_identity_and_media(); append_and_strict(); metadata_and_index();
        std::printf("forgiving empty list compiler: %d checks passed\n",checks);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr,"FAIL forgiving empty list compiler: %s\n",e.what());
        return 1;
    }
}
