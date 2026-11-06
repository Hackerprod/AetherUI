// Linux link-wrapper fault injection. Only this isolated executable fails locale
// creation; the library, process locale, and other tests remain unaffected.
#include <aether/aether.h>
#include <cstdio>
#include <initializer_list>
#include <locale.h>

static unsigned calls = 0;
extern "C" locale_t __wrap_newlocale(int, const char*, locale_t) {
    ++calls;
    return nullptr;
}
int main() {
    for (const char* input : {"rgb(1,2,3)", "rgb(10%,20%,30%)", "rgba(1,2,3,.5)"}) {
        aether_color_rgba color{7, 8, 9, 10};
        if (aether_color_rgba_try_parse(input, &color) || color.r || color.g || color.b || color.a) {
            std::fprintf(stderr, "FAIL: locale creation failure must reject %s transparently\n", input);
            return 1;
        }
    }
    aether_color_rgba color{};
    if (calls != 3 || !aether_color_rgba_try_parse("red", &color) || color.r != 1 || color.a != 1 ||
        !aether_color_rgba_try_parse("#ff0000", &color) || color.r != 1 || color.a != 1 || calls != 3) {
        std::fprintf(stderr, "FAIL: fault injection count or unrelated color parsing\n"); return 1;
    }
    std::puts("rgb_locale_failure: null locale rejected, unrelated colors unchanged");
}
