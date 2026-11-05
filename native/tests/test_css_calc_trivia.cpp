#include <aether/aether.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>

static void check(const char* source, bool valid, float expected = 0.f) {
    aether_css_length value{};
    const bool parsed = aether_css_length_try_parse(source, &value) == AETHER_TRUE;
    if (parsed != valid || (parsed && std::fabs(value.value - expected) > .001f) ||
        (!parsed && value.unit != AETHER_CSS_UNIT_AUTO)) {
        std::fprintf(stderr, "FAIL calc trivia: %s (accepted=%d, value=%g)\n", source, parsed, value.value);
        std::exit(1);
    }
}

int main() {
    // Serialized forms of the var() examples in design sections 7.2 and 8.3.
    check("calc(2 * 1px)", true, 2.f);
    check("calc(1px +/**/2*1px)", false);
    check("calc(1px + 2*1px)", true, 3.f);
    check("calc(1px /**/+/**/2*1px)", false);
    check("calc(1px /**/+ /**/2*1px)", true, 3.f);
    check("calc(1px/**/+ 2px)", false);
    check("calc(3px /**/-/**/2px)", false);
    check("calc(3px/**/- 2px)", false);
    check("calc(3px /**/- /**/2px)", true, 1.f);
    check("calc(1px/* \t\r\n\f */+ 2px)", false);
    check("calc(1px +/* \t\r\n\f */2px)", false);
    check("calc(1px\t/**/+/**/\t2px)", true, 3.f);
    check("calc(1px\n/**/+/**/\n2px)", true, 3.f);
    check("calc(1px\r/**/+/**/\r2px)", true, 3.f);
    check("calc(1px\f/**/+/**/\f2px)", true, 3.f);
    check("calc(1px\r\n/**/+/**/\r\n2px)", true, 3.f);
    check("calc(2/**/ */**/1px)", true, 2.f);
    check("calc(6px/**/" "/" "/**/2)", true, 3.f);
    check("calc((/**/1px /**/+ /**/2px/**/)/**/ */**/2)", true, 6.f);
    check("/**/calc(/**/2px/**/)/**/", true, 2.f);
    check("calc(1/**/px)", false);
    check("1/**/px", false);
    check("2/**/%", false);
    check("calc/**/(10px)", false);
    check("calc /**/(10px)", false);
    check("calc(1px)/*", false);
    check("calc(1px /*)", false);
    check("calc(1px + /*)", false);
    check("calc(1px / /*)", false);
    check("calc(1px/**/2px)", false);
    check("calc(1px/**/+/**/2px)", false);
    check("calc(3px/**/-/**/2px)", false);
    return 0;
}
