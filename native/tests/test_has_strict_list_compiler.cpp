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
void balanced_recovery() {
    const std::string body = "{width:1px;--Size:37px;width:var(--Size)!important;"
        "image:url('a,b)');box:var(--Size);content:'x,y)'}";
    const std::string bad = "#poison:hover:has(.ready,:unknown)" + body;
    const std::string group = ".safe,.other" + body;
    const std::string after = ".after{width:7px}";
    const std::string inside = ".inside" + body;
    Rules expected, actual; int32_t e = 41, a = 41;
    compile(group + "@media(min-width:600px){@media(max-width:900px){" + inside + "}}" + after, expected, e);
    compile(bad + group + bad + "@media(min-width:600px){" + bad +
            "@media(max-width:900px){" + bad + inside + bad + "}" + bad + "}" + after + bad, actual, a);
    require(actual.rules.size() == 4 && expected.rules.size() == 4 && a == 45 && e == 45,
            "invalid balanced blocks consume no rule IDs");
    const char* classes[] = {"safe","other","inside","after"};
    const int orders[] = {41,41,43,44};
    for (size_t i = 0; i < actual.rules.size(); ++i) {
        const auto& x = actual.rules[i]; const auto& y = expected.rules[i];
        require(x.rule_id == 41 + static_cast<int>(i) && x.source_order == orders[i] &&
                x.sheet_handle == 701 && x.layer == style_sheet_layer::theme, "exact rule metadata");
        require(x.selector.steps.size() == 1 && x.selector.steps[0].compound.classes ==
                std::vector<std::string>{classes[i]}, "following valid selector retained");
        require(x.selector.anchor.id.empty() && x.selector.anchor.class_name == classes[i] &&
                x.selector.anchor.type_name.empty(), "rejected anchor cannot contaminate following rule");
        require(x.selector.specificity == specificity_s{0,1,0} &&
                x.selector.dependencies == SELECTOR_DEP_SELF_CLASS, "rejected metadata cannot contaminate following rule");
        same_declarations(x,y);
        for (float width : {500.f,800.f,1000.f}) {
            const bool active = i != 2 || width == 800.f;
            require(matches(x.condition,width) == active && matches(y.condition,width) == active,
                    "nested active and inactive media preserved");
        }
    }
    require(actual.rules[0].declarations.size() == 3 && actual.rules[0].custom_declarations.size() == 1 &&
            actual.rules[0].shorthand_declarations.size() == 1 && actual.rules[0].content_declarations.size() == 1,
            "declaration comparison has nonempty categories");
    require(actual.rules[0].condition == actual.rules[1].condition, "group media sharing preserved");

    auto* old_condition = actual.rules[2].condition;
    for (int i = 0; i < 64; ++i) {
        compile("@media(min-width:1px){@media(max-width:900px){" + bad + "}}", actual, a, 702, 2);
        require(a == 45 && actual.rules.size() == 4 && actual.rules[2].condition == old_condition,
                "zero-rule sheet preserves caller rules, counter and media");
    }
    compile(".last{width:9px}", actual, a, 703, 2);
    require(a == 46 && actual.rules.size() == 5 && actual.rules.back().rule_id == 45 &&
            actual.rules.back().source_order == 45 && actual.rules.back().sheet_handle == 703 &&
            actual.rules.back().layer == style_sheet_layer::author, "later sheet identities preserved");
}
void recovery_and_index() {
    Rules actual; int32_t next = 101;
    compile("#poison:hover:has(.ready,:unknown),.safe{width:1px}"
            ".card:is(:has(#missing:hover,:unknown),.safe){width:2px}"
            ".card:where(:has(#missing:hover,:unknown),.safe){width:3px}", actual, next);
    require(actual.rules.size() == 3 && next == 104, "top-level and nested forgiving recovery");
    const int specificity_classes[] = {1,2,1};
    for (size_t i = 0; i < actual.rules.size(); ++i) {
        const auto& s = actual.rules[i].selector;
        require(s.specificity == specificity_s{0,specificity_classes[i],0} &&
                s.dependencies == SELECTOR_DEP_SELF_CLASS, "no ID, state or descendant metadata after recovery");
        require(s.anchor.id.empty() && s.anchor.class_name == (i == 0 ? "safe" : "card"),
                "recovery publishes only valid anchor");
        if (i != 0) {
            const auto& ps = s.steps[0].compound.pseudos;
            require(ps.size() == 1 && ps[0].kind == (i == 1 ? PSEUDO_IS : PSEUDO_WHERE) &&
                    ps[0].nested && ps[0].nested->size() == 1, "invalid has AST not published");
            const auto& leaf = (*ps[0].nested)[0];
            require(leaf.steps.size() == 1 && leaf.steps[0].compound.pseudos.empty() &&
                    leaf.steps[0].compound.classes == std::vector<std::string>{"safe"}, "exact surviving AST branch");
        }
    }
    rule_index_s index;
    for (const auto& rule : actual.rules) rule_index_add(&index,&rule);
    require(index.by_id().empty() && index.by_type().empty() && index.universal().empty() &&
            index.by_class().size() == 2 && index.dependencies == SELECTOR_DEP_SELF_CLASS,
            "rejected has cannot add index buckets or dependencies");
    node_style_info_s poison{"poison","div",{}};
    std::vector<const compiled_rule_s*> candidates;
    rule_index_get_candidates(&index,&poison,candidates);
    require(candidates.empty(), "rejected ID retrieves no candidate");
    node_style_info_s safe{"","div",{"safe","card"}};
    rule_index_get_candidates(&index,&safe,candidates);
    require(candidates.size() == 3, "valid anchors retain all candidates");
    rule_index_clear(&index);
    require(index.dependencies == SELECTOR_DEP_NONE && index.by_class().empty(), "index cleanup");
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
        balanced_recovery(); recovery_and_index();
        std::printf("has strict list compiler: %d checks passed\n",checks);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr,"FAIL has strict list compiler: %s\n",e.what());
        return 1;
    }
}
