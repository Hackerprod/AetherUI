#include "style_runtime_types.h"
#include <cstdio>
#include <string>
#include <vector>

// Private parser coverage for EOF spans that cannot be represented by a complete
// stylesheet rule, plus AST invariants. This target exports no testing ABI.
namespace {
int checks = 0, failures = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value) { ++failures; std::fprintf(stderr, "FAIL %s\n", message); }
}
bool parse(const char* source, compiled_selector_s& parsed) {
    parsed = compiled_selector_s{};
    return selector_parse(source, parsed);
}
void name(const std::string& source, const std::string& expected, bool type = false) {
    compiled_selector_s parsed;
    const bool valid = parse(source.c_str(), parsed);
    check(valid, "valid name");
    if (!valid) return;
    check(parsed.steps.size() == 1, "one compound");
    const auto& compound = parsed.steps[0].compound;
    check(type ? compound.type_name == expected : compound.ids == std::vector<std::string>{expected},
          "decoded name");
    check((type ? parsed.anchor.type_name : parsed.anchor.id) == expected, "same decoded anchor");
    check(parsed.specificity == (type ? specificity_s{0,0,1} : specificity_s{1,0,0}), "name specificity");
}
void invalid(const std::string& source) {
    compiled_selector_s parsed;
    check(!parse(source.c_str(), parsed), "invalid selector");
}
}
int main() {
    name("#name\\", "name\xef\xbf\xbd");
    name("#\\", "\xef\xbf\xbd");
    name("name\\", "name\xef\xbf\xbd", true);
    name("\\", "\xef\xbf\xbd", true);
    name("#\\0", "\xef\xbf\xbd");
    name("#a\\ ", "a ");
    name("#a\\\t", "a\t");
    name("#\\000061b", "ab");
    name("#\\61b", "\xd8\x9b");
    name("#\\10ffff0", "\xf4\x8f\xbf\xbf" "0");
    name("#\\10000000", "\xf4\x80\x80\x80" "00");
    name("#\\7f ", "\x7f");
    name(std::string("#\\")+'\x7f', "\x7f");
    name("\\64 IV", "div", true);
    // Unescaped numeric spellings previously accepted by the parser stay valid.
    name("#123", "123");
    invalid(std::string("#a")+'\x7f');
    invalid(std::string("#a")+'\xc0'+'\xaf');
    invalid(std::string("#a")+'\xe2');
    invalid(std::string("#\\")+'\xf4'+'\x90'+'\x80'+'\x80');
    invalid(std::string("#\\")+'\xed'+'\xa0'+'\x80');
    invalid("[data-x=\"unterminated\\");
    invalid("[data-x=\"unterminated]");
    invalid("#a\\61\vb");
    invalid(".a)");
    invalid(":is(.a]");
    invalid(":is([data-x=a).safe)");
    invalid(":not(.bad],.safe)");

    for (const char* nl : {"\n", "\r", "\f", "\r\n"}) {
        invalid(std::string("#a\\")+nl);
        invalid(std::string("#a\\")+nl+"b");
        invalid(std::string("[data-x=\"a")+nl+"b\"]");
        invalid(std::string(":not([data-x=\"bad")+nl+"],.safe)");
        for (const char* pseudo : {"is", "where"}) {
            compiled_selector_s parsed;
            auto source = std::string(".outer:")+pseudo+"(#bad:hover\\"+nl+"x,.safe)";
            bool valid = parse(source.c_str(),parsed);
            check(valid, "local invalid escape recovery");
            if (valid) {
                check(parsed.specificity == specificity_s{0,std::string(pseudo)=="where"?1:2,0}, "rejected ID specificity isolated");
                check(parsed.dependencies == SELECTOR_DEP_SELF_CLASS, "rejected state/ID dependencies isolated");
            }
            source = std::string(":is(.outer:")+pseudo+"([data-x=\"bad"+nl+"],.safe))";
            check(parse(source.c_str(),parsed), "nested bad string recovery");
        }
        const std::string invalid_escape = std::string(".outer:has(#bad:hover\\")+nl+"x,.safe)";
        const std::string invalid_string = std::string(".outer:has([data-x=\"bad")+nl+"],.safe)";
        for (const auto& bad : {invalid_escape, invalid_string}) {
            invalid(bad);
            for (const std::string outer : {"is", "where"}) {
                compiled_selector_s empty;
                const auto empty_source = ":"+outer+"("+bad+")";
                const bool empty_valid = parse(empty_source.c_str(),empty);
                check(empty_valid, "outer forgiving empty after strict has rejection");
                if (empty_valid) {
                    check(empty.specificity == specificity_s{0,0,0} && empty.dependencies == SELECTOR_DEP_NONE,
                          "empty forgiving list discards bad string/escape metadata");
                    const bool shape = empty.steps.size()==1 && empty.steps[0].compound.pseudos.size()==1;
                    check(shape, "outer empty compound shape");
                    if (shape) {
                        const auto& pseudo = empty.steps[0].compound.pseudos[0];
                        check(pseudo.kind == (outer=="is" ? PSEUDO_IS : PSEUDO_WHERE) &&
                              pseudo.nested && pseudo.nested->empty(), "present empty forgiving AST");
                    }
                }
                compiled_selector_s recovered;
                const auto source = ":"+outer+"("+bad+",.fallback)";
                const bool valid = parse(source.c_str(), recovered);
                check(valid, "outer forgiving fallback after strict has rejection");
                if (!valid) continue;
                check(recovered.specificity == specificity_s{0,outer=="where"?0:1,0},
                      "rejected has specificity isolated");
                check(recovered.dependencies == SELECTOR_DEP_SELF_CLASS,
                      "rejected has ID/state/descendant dependencies isolated");
                check(recovered.steps.size()==1 && recovered.steps[0].compound.pseudos.size()==1,
                      "outer fallback compound shape");
                if (recovered.steps.size()!=1 || recovered.steps[0].compound.pseudos.size()!=1) continue;
                const auto& nested = recovered.steps[0].compound.pseudos[0].nested;
                check(nested && nested->size()==1, "exactly one surviving fallback");
                if (!nested || nested->size()!=1) continue;
                const auto& fallback = (*nested)[0];
                check(fallback.anchor.class_name=="fallback" && fallback.anchor.id.empty(),
                      "fallback anchor has no rejected ID");
                check(fallback.steps.size()==1 && fallback.steps[0].compound.pseudos.empty(),
                      "rejected has leaves no nested pseudo");
            }
        }
    }
    compiled_selector_s parsed;
    check(parse(R"(.p-0\.5)",parsed), "escaped class");
    check(parsed.specificity==specificity_s{0,1,0}, "escaped dot is one class");
    check(parsed.anchor.class_name=="p-0.5", "decoded class anchor");
    check(parse(R"(.a\,b::\62 efore)",parsed), "escaped pseudo-element");
    check(parsed.pseudo_element==aether_pseudo_element_t::before, "before pseudo-element identity");
    check(parsed.specificity==specificity_s{0,1,1}, "pseudo-element specificity");
    check(parse(R"(:\69 s(.a\,b):\68 over)",parsed), "escaped pseudo names");
    check(parsed.dependencies==(SELECTOR_DEP_SELF_CLASS|SELECTOR_DEP_SELF_STATE), "decoded state dependency");
    check(parse(R"(:is(.bad],.safe))",parsed), "mismatched close stays local");
    check(parse(R"([data-x=""])",parsed), "empty attribute string");
    check(parsed.steps[0].compound.attributes[0].value.empty(), "empty string preserved");
    std::vector<compiled_selector_s> list;
    check(selector_parse_list(R"(.a\,b,.safe)",list), "escaped then actual comma");
    check(list.size()==2, "only actual comma splits");
    list.clear();
    check(selector_parse_list(R"(.a\\,token)",list), "even backslash parity");
    check(list.size()==2, "literal backslash then actual comma");
    list.clear();
    check(selector_parse_list(R"(.bad),.safe)",list), "top-level close branch recovery");
    check(list.size()==1 && list[0].anchor.class_name=="safe", "invalid close not silently accepted");
    std::printf("SELECTOR_ESCAPE_PARSER CHECKS=%d FAILURES=%d\n",checks,failures);
    return failures?1:0;
}
