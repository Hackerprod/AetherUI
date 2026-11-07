// Private compiler/index integration. All checks remain active under NDEBUG.
#include "style_runtime_types.h"
#include "style_property_registry_internal.h"
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <unordered_set>

static style_property_registry_s registry;
style_property_registry_s* style_property_registry_get_global() { return &registry; }
namespace {
int checks = 0;
void require(bool ok, const std::string& label) {
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

// Sources deliberately differ between rejected input and its canonical control.
// Everything else in the recursive AST must agree.
void same(const compiled_selector_s& a, const compiled_selector_s& b, const std::string& path) {
#define FIELD(x) require(a.x == b.x, path + "." #x)
    FIELD(specificity); FIELD(dependencies); FIELD(pseudo_element);
    FIELD(anchor.id); FIELD(anchor.class_name); FIELD(anchor.type_name);
    FIELD(steps.size());
    for (size_t i = 0; i < std::min(a.steps.size(), b.steps.size()); ++i) {
        const auto& x = a.steps[i]; const auto& y = b.steps[i];
        const auto p = path + ".steps[" + std::to_string(i) + "]";
        require(x.relation_to_next == y.relation_to_next, p + ".combinator");
        const auto& c = x.compound; const auto& d = y.compound;
        require(c.type_name == d.type_name, p + ".type");
        require(c.ids == d.ids, p + ".ids");
        require(c.classes == d.classes, p + ".classes/order");
        require(c.attributes.size() == d.attributes.size(), p + ".attributes.size");
        for (size_t j = 0; j < std::min(c.attributes.size(), d.attributes.size()); ++j) {
            const auto& u = c.attributes[j]; const auto& v = d.attributes[j];
            const auto q = p + ".attributes[" + std::to_string(j) + "]";
            require(u.name == v.name, q + ".name"); require(u.op == v.op, q + ".op");
            require(u.value == v.value, q + ".value"); require(u.has_value == v.has_value, q + ".has_value");
            require(u.case_mode == v.case_mode && u.html_default_ascii_insensitive == v.html_default_ascii_insensitive, q + ".case_policy");
        }
        require(c.pseudos.size() == d.pseudos.size(), p + ".pseudos.size");
        for (size_t j = 0; j < std::min(c.pseudos.size(), d.pseudos.size()); ++j) {
            const auto& u = c.pseudos[j]; const auto& v = d.pseudos[j];
            const auto q = p + ".pseudos[" + std::to_string(j) + "]";
            require(u.kind == v.kind, q + ".kind"); require(u.state == v.state, q + ".state");
            require(u.nth.step == v.nth.step, q + ".nth.step");
            require(u.nth.offset == v.nth.offset, q + ".nth.offset");
            require(bool(u.nested) == bool(v.nested), q + ".nested presence");
            if (u.nested && v.nested) {
                require(u.nested->size() == v.nested->size(), q + ".nested.size");
                for (size_t k = 0; k < std::min(u.nested->size(), v.nested->size()); ++k)
                    same((*u.nested)[k], (*v.nested)[k], q + ".nested[" + std::to_string(k) + "]");
            }
        }
    }
#undef FIELD
}

void has_invariant(const compiled_selector_s& selector, bool inside = false) {
    if (inside) require(selector.pseudo_element == aether_pseudo_element_t::none,
                        "no published pseudo-element anywhere below HAS");
    for (const auto& step : selector.steps)
        for (const auto& pseudo : step.compound.pseudos)
            if (pseudo.nested)
                for (const auto& nested : *pseudo.nested)
                    has_invariant(nested, inside || pseudo.kind == PSEUDO_HAS);
}
const std::string body = "{width:1px;--Size:37px;width:var(--Size)!important;"
    "image:url('a,b)');box:var(--Size);content:'x,y)'}";

void recovered_rules() {
    const std::pair<const char*,const char*> cases[] = {
        {"#poison:hover:has(::before),.safe", ".safe"},
        {".safe,#poison:has(::after),.other", ".safe,.other"},
        {".safe,.other,#poison:has(.ready,::before)", ".safe,.other"},
        {".card:has(:is(#never:hover::before,.ready))", ".card:has(:is(.ready))"},
        {".card:has(:where(#never:hover::after,.ready))", ".card:has(:where(.ready))"},
        {".card:is(:has(#never:hover::before),.safe)", ".card:is(.safe)"},
        {".card:where(:has(#never:hover::after),.safe)", ".card:where(.safe)"},
        {".card:has(:is(#never:hover::before))", ".card:has(:is())"},
        {".card:has(:where(::after))", ".card:has(:where())"},
        {".card:has(:not(:is(::before)))", ".card:has(:not(:is()))"},
        {".card:has(:is(::before),.ready)", ".card:has(:is(),.ready)"},
        {".card:is(:has(::before))", ".card:is()"},
        {".card:has(:is(:not(::before),.ready))", ".card:has(:is(.ready))"},
        {".card:has(:is(:has(::before),.ready))", ".card:has(:is(.ready))"},
        {".card:has(:is(:has(.ready)::before,.ready))", ".card:has(:is(.ready))"},
        {".card:has(:is(#never:is(::before),.ready))", ".card:has(:is(#never:is(),.ready))"},
        {".card:has(:where(#never:is(::after),.ready))", ".card:has(:where(#never:is(),.ready))"},
        {".card:has(:where(#never:hover[data-x]:first-child:is(::before),.ready))",
         ".card:has(:where(#never:hover[data-x]:first-child:is(),.ready))"},
        {".card:has(:is(#bad[data-x]:hover:first-child > .leaf::before,.ready))", ".card:has(:is(.ready))"},
        {".card:has(:where(#bad[data-x]:hover:first-child + .leaf::after,.ready))", ".card:has(:where(.ready))"},
        {".card:has(:is(#bad[data-x]:hover:first-child ~ .leaf::before,.ready))", ".card:has(:is(.ready))"},
        {".card:is(:has(::before),::after)", ".card:is(::after)"},
        {".card:has(.ready)::before", ".card:has(.ready)::before"},
        {".card:has(.ready)::after", ".card:has(.ready)::after"},
        {".card:has(.ready):is(::before)", ".card:has(.ready):is(::before)"},
        {".card:has(.mid:has(.ready))", ".card:has(.mid:has(.ready))"},
        {".card:HAS(:is(::b\\65 fore,.ready))", ".card:has(:is(.ready))"},
        {".card:has(:where(:/**/:/**/after,.ready))", ".card:has(:where(.ready))"},
        {".card:has([data-x=\"a,::before)\"])", ".card:has([data-x=\"a,::before)\"])"},
        {".card:has(.literal\\:\\:before)", ".card:has(.literal\\:\\:before)"},
        {".card:has(.ready/* ::before , ) */)", ".card:has(.ready)"}
    };
    for (const auto& test : cases) {
        for (bool media : {false,true}) {
            Rules actual, expected; int32_t a = 41, e = 41;
            const std::string prefix = media ? "@media(min-width:600px){@media(max-width:900px){" : "";
            const std::string suffix = media ? "}}" : "";
            compile(prefix + test.first + body + suffix + ".after{width:7px}", actual, a);
            compile(prefix + test.second + body + suffix + ".after{width:7px}", expected, e);
            require(actual.rules.size() == expected.rules.size() && actual.rules.size() >= 2 && a == e,
                    std::string("published rules and IDs: ") + test.first);
            for (size_t i = 0; i < actual.rules.size(); ++i) {
                const auto& x = actual.rules[i]; const auto& y = expected.rules[i];
                require(x.rule_id == 41 + static_cast<int>(i) && x.rule_id == y.rule_id &&
                        x.source_order == (i + 1 == actual.rules.size() ? x.rule_id : 41) &&
                        x.source_order == y.source_order && x.sheet_handle == 701 &&
                        x.layer == style_sheet_layer::theme, "rule identity and shared group order");
                same(x.selector,y.selector,test.first);
                has_invariant(x.selector);
                same_declarations(x,y);
                for (float width : {500.f,800.f,1000.f}) {
                    const bool active = !media || i + 1 == actual.rules.size() || width == 800.f;
                    require(matches(x.condition,width) == active && matches(y.condition,width) == active,
                            "nested media active and inactive conditions");
                }
                if (i + 1 < actual.rules.size()) require(x.condition == actual.rules[0].condition,
                                                         "group shares media DAG");
            }
            const auto& first = actual.rules[0];
            require(first.declarations.size() == 3 && first.custom_declarations.size() == 1 &&
                    first.shorthand_declarations.size() == 1 && first.content_declarations.size() == 1,
                    "declaration comparisons exercise every category");
        }
    }
}

void atomic_rejection() {
    const char* rejected[] = {
        "#poison:has(::before)", "#poison:has(::after)",
        "#poison:has(::before,.ready)", "#poison:has(.ready,::after,.other)",
        "#poison:has(.ready,.other,::before)", "#poison:has(:not(::after))",
        "#poison:has(:not(.ready,::before))", "#poison:not(:has(::after))",
        "#poison:has(:has(.ready)::before)"
    };
    std::string bad;
    for (const auto* selector : rejected) bad += selector + body;
    Rules actual, expected; int32_t a = 101, e = 101;
    const std::string seed = "@media(min-width:600px){@media(max-width:900px){"
        "#kept[data-x]:hover:first-child > .kept:has(.ready),.other" + body + "}}";
    const std::string interleaved = bad + "@media(min-width:600px){" + bad +
        "@media(max-width:900px){" + bad +
        "#kept[data-x]:hover:first-child > .kept:has(.ready),.other" + body + bad + "}" + bad + "}" + bad;
    compile(interleaved,actual,a,700,0); compile(seed,expected,e,700,0);
    require(a == 103 && actual.rules.size() == 2, "rich caller prefix");
    auto* condition = actual.rules[0].condition;
    for (int repetition = 0; repetition < 32; ++repetition) {
        compile("@media(min-width:1px){" + bad + "@media(max-width:900px){" + bad + "}}",actual,a,702,2);
        require(a == 103 && actual.rules.size() == 2 && actual.rules[0].condition == condition &&
                actual.rules[1].condition == condition, "rejected sheet preserves prefix and condition ownership");
        for (size_t i = 0; i < actual.rules.size(); ++i) {
            const auto& x = actual.rules[i]; const auto& y = expected.rules[i];
            same(x.selector,y.selector,"prefix after rejected sheet"); same_declarations(x,y);
            require(x.rule_id == y.rule_id && x.source_order == y.source_order &&
                    x.sheet_handle == 700 && x.layer == y.layer, "prefix identity is unchanged");
            for (float width : {500.f,800.f,1000.f})
                require(matches(x.condition,width) == (width == 800.f), "prefix media remains usable");
        }
    }
    compile(bad + ".safe,.last" + body + bad,actual,a,703,2);
    require(a == 105 && actual.rules.size() == 4, "interleaved invalid blocks consume no IDs");
    for (size_t i = 2; i < 4; ++i) {
        const auto& rule = actual.rules[i];
        require(rule.rule_id == 101 + static_cast<int>(i) && rule.source_order == 103 &&
                rule.sheet_handle == 703 && rule.layer == style_sheet_layer::author,
                "append preserves exact IDs, layer and group order");
        require(rule.selector.anchor.class_name == (i == 2 ? "safe" : "last") &&
                rule.selector.specificity == specificity_s{0,1,0} &&
                rule.selector.dependencies == SELECTOR_DEP_SELF_CLASS, "no discarded append metadata");
    }
}

void manual_capabilities() {
    rule_index_s index;
    auto unknown = [&] {
        require(index.may_target_pseudo(aether_pseudo_element_t::before) &&
                index.may_target_pseudo(aether_pseudo_element_t::after) &&
                index.may_target_pseudo(static_cast<aether_pseudo_element_t>(99)),
                "manual index never proves absence");
    };
    unknown(); rule_index_clear(&index); unknown();
    compiled_style_sheet_s sheet;
    sheet.rules.emplace_back();
    rule_index_add(&index, &sheet.rules.front()); unknown();
    rule_index_rebuild(&index, {&sheet}); unknown();
    sheet.rules.front().selector.pseudo_element = aether_pseudo_element_t::before; unknown();
    sheet.rules.front().selector.pseudo_element = aether_pseudo_element_t::after; unknown();
    sheet.rules.front().selector.pseudo_element = static_cast<aether_pseudo_element_t>(99); unknown();
    rule_index_rebuild(&index, {}); unknown();
    auto copy = index; std::swap(index, copy); unknown();
}

void metadata_and_index() {
    struct Case { const char* selector; specificity_s specificity; unsigned dependencies; };
    const unsigned cls = SELECTOR_DEP_SELF_CLASS, desc = SELECTOR_DEP_DESCENDANT, id = SELECTOR_DEP_SELF_ID;
    const Case cases[] = {
        {"#poison:has(::before),.safe",{0,1,0},cls},
        {".card:is(:has(#never:hover::before),.safe)",{0,2,0},cls},
        {".card:where(:has(#never:hover::after),.safe)",{0,1,0},cls},
        {".card:has(:is(#never:hover::before,.ready))",{0,2,0},cls|desc},
        {".card:has(:where(#never:hover::after,.ready))",{0,1,0},cls|desc},
        {".card:has(:is(#never:hover::before))",{0,1,0},cls|desc},
        {".card:has(:is(#never:is(::before),.ready))",{1,1,0},cls|id|desc},
        {".card:has(:where(#never:is(::after),.ready))",{0,1,0},cls|id|desc},
        {".card:has(:where(#never:hover[data-x]:first-child:is(::before),.ready))",{0,1,0},
         cls|id|desc|SELECTOR_DEP_SELF_STATE|SELECTOR_DEP_SELF_ATTRIBUTE|SELECTOR_DEP_STRUCTURE}
    };
    Rules actual; int32_t next = 201;
    unsigned dependencies = 0;
    for (const auto& test : cases) {
        compile(std::string(test.selector) + body,actual,next);
        require(actual.rules.size() == static_cast<size_t>(next - 201), "one rule per accepted input");
        const auto& s = actual.rules.back().selector;
        require(s.specificity == test.specificity && s.dependencies == test.dependencies,
                std::string("exact metadata: ") + test.selector);
        require(s.anchor.id.empty() && s.anchor.type_name.empty() &&
                s.anchor.class_name == (actual.rules.size() == 1 ? "safe" : "card"), "outer anchors only");
        has_invariant(s); dependencies |= test.dependencies;
    }
    require(actual.rules.size() == 9 && next == 210, "impossible valid selectors publish rules and IDs");
    const auto& empty_has = actual.rules[5].selector.steps[0].compound.pseudos[0];
    require(empty_has.kind == PSEUDO_HAS && empty_has.nested && empty_has.nested->size() == 1,
            "impossible HAS keeps its nested argument");
    const auto& empty_is = (*empty_has.nested)[0].steps[0].compound.pseudos[0];
    require(empty_is.kind == PSEUDO_IS && empty_is.nested && empty_is.nested->empty(),
            "rejected-only IS is present-empty, not removed or replaced with universal");
    rule_index_s index;
    for (const auto& rule : actual.rules) rule_index_add(&index,&rule);
    require(index.by_id.empty() && index.by_type.empty() && index.universal.empty() &&
            index.by_class.size() == 2 && index.dependencies == dependencies,
            "index retains valid impossible dependencies without promoting nested anchors");
    require(index.by_class.at("safe") == std::vector<const compiled_rule_s*>{&actual.rules[0]} &&
            index.by_class.at("card").size() == 8, "index buckets contain only published rules");
    std::vector<const compiled_rule_s*> candidates;
    node_style_info_s poison{"poison","div",{}};
    rule_index_get_candidates(&index,&poison,candidates);
    require(candidates.empty(), "rejected outer ID retrieves no candidate");
    node_style_info_s card{"never","div",{"card","safe"}};
    rule_index_get_candidates(&index,&card,candidates);
    require(candidates.size() == 9 && std::unordered_set<const compiled_rule_s*>(candidates.begin(),candidates.end()).size() == 9,
            "every retained rule is retrieved once including impossible selectors");
    rule_index_clear(&index);
    require(index.dependencies == SELECTOR_DEP_NONE && index.by_class.empty() && index.by_id.empty() &&
            index.by_type.empty() && index.universal.empty(), "index cleanup");
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
        recovered_rules(); atomic_rejection(); metadata_and_index(); manual_capabilities();
        std::printf("has pseudo-element compiler: %d checks passed\n",checks);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr,"FAIL has pseudo-element compiler: %s\n",e.what());
        return 1;
    }
}
