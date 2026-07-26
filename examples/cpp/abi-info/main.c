#include <aether/aether.h>

#include <inttypes.h>
#include <stdio.h>

int main(void) {
    const uint32_t version = aether_abi_version();
    printf("AetherUI ABI %u.%u.%u\n",
        AETHER_ABI_VERSION_GET_MAJOR(version),
        AETHER_ABI_VERSION_GET_MINOR(version),
        AETHER_ABI_VERSION_GET_PATCH(version));
    printf("capabilities=0x%016" PRIx64 "\n", aether_abi_capabilities());
    printf("paint_command_size=%zu\n", aether_paint_command_size());
    return version == AETHER_ABI_VERSION_CURRENT ? 0 : 1;
}
