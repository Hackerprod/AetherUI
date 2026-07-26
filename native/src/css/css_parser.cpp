#include "css_types.h"
#include "../abi/aether_abi.h"

AETHER_API aether_status_t aether_css_parse_stylesheet(const char* source, aether_stylesheet_t* out_sheet) {
    (void)source;
    if (!out_sheet) return AETHER_ERROR_NULL_ARG;
    *out_sheet = new aether_stylesheet_s();
    return AETHER_OK;
}

AETHER_API void aether_css_stylesheet_destroy(aether_stylesheet_t sheet) {
    delete sheet;
}
