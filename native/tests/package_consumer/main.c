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

    puts("OK: installed AetherUI::Core package and HTML tag names are consumable from C11");
    return 0;
}
