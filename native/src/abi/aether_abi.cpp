#include "aether_abi.h"

extern "C" {

AETHER_API uint32_t aether_abi_version(void) {
    return AETHER_ABI_VERSION_CURRENT;
}

AETHER_API aether_capabilities_t aether_abi_capabilities(void) {
    return AETHER_CAPABILITY_DOM |
           AETHER_CAPABILITY_STYLE_RUNTIME |
           AETHER_CAPABILITY_LAYOUT |
           AETHER_CAPABILITY_DISPLAY_LIST;
}

AETHER_API size_t aether_paint_command_size(void) {
    return sizeof(aether_paint_command_t);
}

AETHER_API const char* aether_status_message(aether_status_t status) {
    switch (status) {
        case AETHER_OK:                   return "OK";
        case AETHER_ERROR_NULL_ARG:       return "Null argument";
        case AETHER_ERROR_INVALID_STATE:  return "Invalid state";
        case AETHER_ERROR_OUT_OF_MEMORY:  return "Out of memory";
        case AETHER_ERROR_DISPOSED:       return "Object disposed";
        case AETHER_ERROR_PARSE_FAILED:   return "Parse failed";
        default:                          return "Unknown error";
    }
}

AETHER_API void aether_string_free(char* str) {
    free(str);
}

AETHER_API void aether_linear_gradient_free(aether_linear_gradient* g) {
    if (g && g->stops) {
        free(g->stops);
        g->stops = nullptr;
        g->stop_count = 0;
    }
}

} // extern "C"
