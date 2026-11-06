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

const std::string declarations = "{width:1px;--Size:37px;width:var(--Size)!important;"
    "image:url('a,b)');box:var(--Size);content:'x,y)'}";

void publication_and_append() {
    const std::pair<const char*,const char*> groups[] = {
        {".123,.a,.b", ".a,.b"}, {".a,#123,.b", ".a,.b"},
        {".a,.b,[x=-1]", ".a,.b"}, {".-->a,#-->a,.safe", "#-->a,.safe"},
        {".123::before,.a::before,.b::after", ".a::before,.b::after"},
        {".-->a::before,#-->a::before,.safe::before", "#-->a::before,.safe::before"}
    };
    for (const auto& group : groups) for (bool media : {false,true}) for (bool append : {false,true}) {
        Rules actual, expected; int32_t next = 41, ref = 41;
        if (append) {
            compile("@media(min-width:200px){.prefix{width:9px}}",actual,next,700,0);
            compile("@media(min-width:200px){.prefix{width:9px}}",expected,ref,700,0);
        }
        const size_t offset = actual.rules.size();
        auto* prior = append ? actual.rules[0].condition : nullptr;
        const int32_t start = next;
        const std::string prefix = media ? "@media(min-width:600px){@media(max-width:900px){" : "";
        const std::string suffix = media ? "}}" : "";
        compile(prefix + group.first + declarations + "#123{width:4px}" + suffix + ".c{width:6px}",actual,next);
        compile(prefix + group.second + declarations + suffix + ".c{width:6px}",expected,ref);
        require(actual.rules.size() == offset + 3 && next == start + 3 && next == ref,
                std::string(group.first) + ": rejected branches consume no IDs");
        for (size_t i = 0; i < actual.rules.size(); ++i) {
            const auto& a = actual.rules[i]; const auto& b = expected.rules[i];
            same(a.selector,b.selector,group.first);
            same_declarations(a,b);
            require(a.rule_id == b.rule_id && a.source_order == b.source_order &&
                    a.sheet_handle == b.sheet_handle && a.layer == b.layer,"canonical publication identity");
        }
        for (size_t i = 0; i < 3; ++i) {
            const auto& a = actual.rules[offset+i];
            require(a.rule_id == start + static_cast<int32_t>(i) &&
                    a.source_order == start + (i == 2 ? 2 : 0),"contiguous IDs and group source_order");
            for (float width : {500.f,800.f,1000.f})
                require(matches(a.condition,width) == (!media || i == 2 || width == 800.f),"nested media and following DFS rule");
        }
        const auto& first = actual.rules[offset];
        require(first.condition == actual.rules[offset+1].condition,"survivors share media condition");
        require(first.declarations.size() == 3 && first.custom_declarations.size() == 1 &&
                first.shorthand_declarations.size() == 1 && first.content_declarations.size() == 1,
                "property/important/custom/shorthand/content parity is nonvacuous");
        if (append) require(actual.rules[0].condition == prior && matches(prior,800.f) &&
                !matches(prior,100.f) && actual.rules[0].rule_id == 41,"append preserves original media graph");
    }
}

void logical_and_generated() {
    const std::pair<const char*,const char*> cases[] = {
        {":is(#123:hover,.safe)",":is(.safe)"},
        {":is(#ghost:hover.123,.safe)",":is(.safe)"},
        {":where(.ancestor > #ghost:hover[data-x=123],.safe)",":where(.safe)"},
        {":is(.1\\,#ghost:hover,.safe)",":is(.safe)"},
        {":is(.-->a,.safe)",":is(.safe)"},
        {":where(#ghost:hover.-->a,.safe)",":where(.safe)"},
        {":is(.123)",":is()"}, {":where(.123)",":where()"},
        {".outer:is(.123)",".outer:is()"},
        {":is(:not(.safe,.123),.fallback)",":is(.fallback)"},
        {":where(:has(.safe,.123),.fallback)",":where(.fallback)"},
        {":is(:not(.-->a,.safe),.fallback)",":is(.fallback)"},
        {":where(:has(.-->a,.safe),.fallback)",":where(.fallback)"},
        {":not(:is(.123))",":not(:is())"}, {":has(:is(.123))",":has(:is())"},
        {":is(#ghost:is(.123),.safe)",":is(#ghost:is(),.safe)"},
        {":is(#-->a,.safe)",":is(#-->a,.safe)"},
        {R"(:is(#\31 23:hover,.safe))",R"(:is(#\31 23:hover,.safe))"},
        {".item:is(.123)::before",".item:is()::before"},
        {".item:not(:is(.123))::before",".item:not(:is())::before"},
        {":is(#ghost:hover.123::before,.safe)::after",":is(.safe)::after"},
        {":is(.-->a::before,.safe)::after",":is(.safe)::after"},
        {":has(:is(.a::before))",":has(:is())"}
    };
    for (const auto& entry : cases) {
        Rules actual, expected; int32_t next = 17, ref = 17;
        compile(std::string(entry.first) + declarations + ".after{width:2px}",actual,next);
        compile(std::string(entry.second) + declarations + ".after{width:2px}",expected,ref);
        require(actual.rules.size() == 2 && next == 19 && ref == 19,std::string(entry.first) + ": valid logical rule published");
        for (size_t i = 0; i < 2; ++i) {
            same(actual.rules[i].selector,expected.rules[i].selector,entry.first);
            same_declarations(actual.rules[i],expected.rules[i]);
        }
    }
    for (const char* rejected : {".123", "#123", "-1", "[123]", "[x=123]", ".-->a",
            ":not(.safe,.123)", ":has(.safe,.123)", ":not(.-->a,.safe)", ":has(.-->a,.safe)",
            ".123::before", ".-->a::before", ":has(.a::before)", "", ",", ".123,#123"}) {
        Rules out; int32_t next = 23;
        compile(".prefix{width:1px}",out,next);
        compile(std::string(rejected) + declarations,out,next);
        require(out.rules.size() == 1 && next == 24,std::string(rejected) + ": no publication or counter consumption");
        compile(".after::before{content:'ok'}",out,next);
        require(out.rules.size() == 2 && next == 25 && out.rules.back().selector.pseudo_element == aether_pseudo_element_t::before,
                "rejected selector does not contaminate following generated rule");
    }
}

void anchors_and_index() {
    Rules out; int32_t next = 201;
    compile(".123,#123,[x=123],.-->a{width:1px}"
            ":is(#ghost:hover.123,.safe){width:1px}"
            ":where(.ancestor > #ghost:hover[data-x=123],.safe){width:1px}"
            ":is(.123){width:1px}.item:is(.123){width:1px}"
            ".safe{width:1px}#-->a{width:1px}",out,next);
    require(out.rules.size() == 6 && next == 207,"index input has exactly six legitimate publications");
    for (size_t i = 0; i < 5; ++i) {
        const auto& s = out.rules[i].selector;
        require(s.dependencies == (i == 2 ? SELECTOR_DEP_NONE : SELECTOR_DEP_SELF_CLASS),"discarded ID/state/ancestor/attribute dependencies absent");
        require(s.anchor.id.empty() && s.anchor.type_name.empty() &&
                s.anchor.class_name == (i < 3 ? "" : i == 3 ? "item" : "safe"),"only direct subject anchors survive");
    }
    require(out.rules[5].selector.anchor.type_name == "a" && out.rules[5].selector.anchor.id.empty(),"hash CDC ID remains ancestor, not subject anchor");
    rule_index_s index;
    for (const auto& rule : out.rules) rule_index_add(&index,&rule);
    require(index.by_id.empty() && index.by_class.size() == 2 && index.by_type.size() == 1 && index.universal.size() == 3,
            "no numeric, ghost, or CDC class bucket");
    require((index.dependencies & SELECTOR_DEP_SELF_STATE) == 0,"discarded hover never reaches index");
    require(index.by_class.at("item") == std::vector<const compiled_rule_s*>{&out.rules[3]} &&
            index.by_class.at("safe") == std::vector<const compiled_rule_s*>{&out.rules[4]} &&
            index.by_type.at("a") == std::vector<const compiled_rule_s*>{&out.rules[5]},"exact survivor buckets");
    node_style_info_s node{"123","span",{"123","--"}};
    std::vector<const compiled_rule_s*> candidates;
    rule_index_get_candidates(&index,&node,candidates);
    require(candidates == index.universal,"numeric DOM values do not retrieve phantom rules");
    node_style_info_s valid{"","a",{"safe","item"}};
    candidates.clear(); rule_index_get_candidates(&index,&valid,candidates);
    require(candidates.size() == 6 && std::unordered_set<const compiled_rule_s*>(candidates.begin(),candidates.end()).size() == 6,
            "all legitimate rules retrieved once including impossible logical rule");
    rule_index_clear(&index);
    require(index.dependencies == SELECTOR_DEP_NONE && index.by_id.empty() && index.by_class.empty() &&
            index.by_type.empty() && index.universal.empty(),"index cleanup");
}

void capacity() {
    Rules out; int32_t next = INT32_MAX - 1;
    compile(".123,.safe,#123{width:1px}",out,next);
    require(out.rules.size() == 1 && next == INT32_MAX,"only surviving branch consumes final identity");
    for (const char* css : {".123{width:1px}",".-->a{width:1px}",":not(.safe,.123){width:1px}",""}) {
        compile(css,out,next);
        require(out.rules.size() == 1 && next == INT32_MAX,"invalid/empty stylesheet fits exhausted counter");
    }
    require(style_source_compile(":is(.123){width:1px}",701,1,out.rules,next,nullptr,nullptr) == AETHER_ERROR_INVALID_STATE &&
            out.rules.size() == 1 && next == INT32_MAX,"valid impossible pseudo consumes capacity and rolls back on exhaustion");
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
        publication_and_append(); logical_and_generated(); anchors_and_index(); capacity();
        std::printf("selector identifier start compiler: %d checks passed\n",checks);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr,"FAIL selector identifier start compiler: %s\n",e.what());
        return 1;
    }
}
