#include <aether/aether.h>

#include <stdio.h>

int main(void) {
    if (aether_abi_version() != AETHER_ABI_VERSION_CURRENT) {
        fputs("installed header and runtime ABI versions differ\n", stderr);
        return 1;
    }
    if (aether_paint_command_size() != sizeof(aether_paint_command_t)) {
        fputs("installed header and runtime command sizes differ\n", stderr);
        return 1;
    }

    puts("OK: installed AetherUI::Core package is consumable");
    return 0;
}
