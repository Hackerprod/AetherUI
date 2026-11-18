#include <aether/aether.h>
#include <cstdio>
#ifdef AETHER_VERIFY_INSTALLED_ORIGIN
#include "installed_origin.h"
#endif

int main() {
#ifdef AETHER_VERIFY_INSTALLED_ORIGIN
    if (!aether_verify_installed_origin()) return 1;
#endif
    if (aether_abi_version() != AETHER_ABI_VERSION_CURRENT ||
        aether_paint_command_size() != sizeof(aether_paint_command_t)) {
        std::fputs("installed header/runtime ABI mismatch\n", stderr);
        return 1;
    }
    aether_doc_t doc = nullptr;
    if (aether_doc_create(&doc) != AETHER_OK || !doc) return 1;
    aether_doc_destroy(doc);
    std::puts("OK: installed public C ABI consumed from C++11");
    return 0;
}
