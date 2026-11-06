#include <aether/aether.h>

#include <stdio.h>
#include <string.h>

int main(void) {
    if (aether_abi_version() != AETHER_ABI_VERSION_CURRENT) {
        fputs("installed header and runtime ABI versions differ\n", stderr);
        return 1;
    }
    if (aether_paint_command_size() != sizeof(aether_paint_command_t)) {
        fputs("installed header and runtime command sizes differ\n", stderr);
        return 1;
    }

    {
        aether_doc_t doc = NULL;
        aether_node_t root, body, input;
        char* name;
        int ok;
        if (aether_doc_create(&doc) != AETHER_OK || !doc) return 1;
        ok = aether_doc_parse_html(doc, "<DIV id=first></DIV><HTML id=root><BODY><INPUT id=AbC></BODY></HTML>") == AETHER_OK;
        root = aether_doc_get_document_element(doc);
        body = aether_doc_get_body(doc);
        input = aether_doc_get_element_by_id(doc, "AbC");
        name = aether_element_get_tag_name(input);
        ok = ok && root && root == aether_doc_get_element_by_id(doc, "root") && body && input &&
             aether_node_get_parent(body) == root && aether_node_get_parent(input) == body &&
             name && strcmp(name, "input") == 0 && aether_element_is_focusable(input) == AETHER_TRUE &&
             !aether_doc_get_element_by_id(doc, "abc");
        aether_string_free(name);
        aether_doc_destroy(doc);
        if (!ok) { fputs("installed HTML tag-name contract failed\n", stderr); return 1; }
    }

    {
        aether_doc_t doc = NULL;
        char* value;
        int ok;
        if (aether_doc_create(&doc) != AETHER_OK || !doc) return 1;
        ok = aether_doc_parse_html(doc, "<div ID=TaRget id=Discard DATA-X=First data-x=Last></div>") == AETHER_OK;
        aether_node_t node = aether_doc_get_document_element(doc);
        value = aether_element_get_attribute(node, "DaTa-X");
        ok = ok && value && strcmp(value, "First") == 0 && aether_element_get_attribute_count(node) == 2 &&
            aether_doc_get_element_by_id(doc, "TaRget") == node && !aether_doc_get_element_by_id(doc, "target") &&
            !aether_doc_get_element_by_id(doc, "Discard");
        aether_string_free(value);
        if (aether_element_set_attribute(node, "Id", "New") != AETHER_OK) ok = 0;
        ok = ok && aether_doc_get_element_by_id(doc, "New") == node &&
            !aether_doc_get_element_by_id(doc, "TaRget") && aether_element_has_attribute(node, "ID") &&
            aether_element_get_attribute_count(node) == 2;
        aether_doc_destroy(doc);
        if (!ok) { fputs("installed ASCII attribute identity failed\n", stderr); return 1; }
    }

    puts("OK: installed AetherUI::Core package, HTML tag and attribute names are consumable from C11");
    return 0;
}
