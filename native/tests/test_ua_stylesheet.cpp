#include <aether/aether.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void require(bool condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

int main() {
    const char* css = aether_style_runtime_defaults_css();
    require(css != nullptr, "the native UA defaults stylesheet must be available");
    require(std::strlen(css) > 0, "the native UA defaults stylesheet must not be empty");
    require(std::strstr(css, "button") != nullptr,
            "the native UA defaults stylesheet must contain the Chromium-parity button rules");
    require(std::strstr(css, "input") != nullptr,
            "the native UA defaults stylesheet must contain the input rules");
    std::puts("OK: UA stylesheet comes from native");
    return 0;
}
