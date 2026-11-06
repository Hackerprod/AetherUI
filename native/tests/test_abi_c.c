#include <aether/aether.h>

#include <stdio.h>
#include <string.h>

static int fail(const char* message) {
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

int main(void) {
    const uint32_t version = aether_abi_version();
    const aether_capabilities_t required_capabilities =
        AETHER_CAPABILITY_DOM |
        AETHER_CAPABILITY_STYLE_RUNTIME |
        AETHER_CAPABILITY_LAYOUT |
        AETHER_CAPABILITY_DISPLAY_LIST |
        AETHER_CAPABILITY_RENDER_PLAN |
        AETHER_CAPABILITY_INPUT_HIT_TEST;

    if (version != AETHER_ABI_VERSION_CURRENT) {
        return fail("runtime ABI version differs from the C header");
    }
    if (AETHER_ABI_VERSION_GET_MAJOR(version) != AETHER_ABI_VERSION_MAJOR ||
        AETHER_ABI_VERSION_GET_MINOR(version) != AETHER_ABI_VERSION_MINOR ||
        AETHER_ABI_VERSION_GET_PATCH(version) != AETHER_ABI_VERSION_PATCH) {
        return fail("ABI version packing is incorrect in C");
    }
    if ((aether_abi_capabilities() & required_capabilities) != required_capabilities) {
        return fail("runtime capability mask is incomplete");
    }
    {
        aether_input_hit_part_t part = AETHER_INPUT_HIT_CONTENT;
        aether_node_t owner = (aether_node_t)(uintptr_t)1;
        if (aether_layout_result_hit_test_input(NULL, NULL, 0.f, 0.f, NULL, NULL,
                                               &part, &owner) != AETHER_ERROR_NULL_ARG ||
            part != AETHER_INPUT_HIT_NONE || owner != NULL) {
            return fail("C input-hit API must validate and initialize outputs");
        }
    }
    if (aether_paint_command_size() != sizeof(aether_paint_command_t)) {
        return fail("runtime paint-command size differs from the C header");
    }
    if (aether_computed_style_size() != sizeof(aether_computed_style_t)) {
        return fail("runtime computed-style size differs from the C header");
    }

    {
        aether_doc_t doc = NULL;
        if (aether_doc_create(&doc) != AETHER_OK ||
            aether_doc_parse_html(doc, "<div ID=TaRget id=Discard DATA-X=First data-x=Last></div>") != AETHER_OK) {
            aether_doc_destroy(doc);
            return fail("C attribute parse failed");
        }
        aether_node_t node = aether_doc_get_document_element(doc);
        char* value = aether_element_get_attribute(node, "DaTa-X");
        int valid = value && strcmp(value, "First") == 0 &&
            aether_element_get_attribute_count(node) == 2 &&
            aether_doc_get_element_by_id(doc, "TaRget") == node &&
            aether_doc_get_element_by_id(doc, "target") == NULL &&
            aether_doc_get_element_by_id(doc, "Discard") == NULL;
        aether_string_free(value);
        if (aether_element_set_attribute(node, "Id", "New") != AETHER_OK) valid = 0;
        if (aether_doc_get_element_by_id(doc, "TaRget") != NULL ||
            aether_doc_get_element_by_id(doc, "New") != node ||
            !aether_element_has_attribute(node, "ID") ||
            aether_element_get_attribute_count(node) != 2) valid = 0;
        aether_doc_destroy(doc);
        if (!valid) return fail("C ASCII attribute identity/first-wins/setter regression");
    }

    puts("OK: C ABI smoke test passed");
    return 0;
}
