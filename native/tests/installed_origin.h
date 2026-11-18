#ifndef AETHER_INSTALLED_ORIGIN_H
#define AETHER_INSTALLED_ORIGIN_H
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* POSIX-only instrumentation, enabled by the local package gate. Check the
 * actual ABI symbol's loaded object in each consumer process, not just RPATH. */
static int aether_verify_installed_origin(void) {
    Dl_info info;
    char *actual = NULL, *expected = NULL;
    int ok = 0;
    if (dladdr((const void *)&aether_abi_version, &info) != 0 && info.dli_fname) {
        actual = realpath(info.dli_fname, NULL);
        expected = realpath(AETHER_EXPECTED_LIBRARY, NULL);
        ok = actual && expected && strcmp(actual, expected) == 0;
    }
    fprintf(ok ? stdout : stderr, "loaded AetherUI: %s; expected: %s\n",
        actual ? actual : "unresolved", expected ? expected : AETHER_EXPECTED_LIBRARY);
    free(actual);
    free(expected);
    return ok;
}
#endif
