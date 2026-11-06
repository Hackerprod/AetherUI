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

    {
        aether_doc_t doc = NULL;
        aether_node_t host, target, inside, after, raw, tail;
        char* value;
        int ok;
        if (aether_doc_create(&doc) != AETHER_OK || !doc) return 1;
        ok = aether_doc_parse_html(doc,
            "<body><section id=host><div id=target>A</missing x='><b id=phantom>'>"
            "<p id=inside>inside</p></DIV x=\"a>b\" id=phantom><p id=after>after</p>"
            "<style id=raw>A&amp;<b id=phantom></StYlE x='a>b'><p id=tail>tail</p></section></body>") == AETHER_OK;
        host = aether_doc_get_element_by_id(doc, "host");
        target = aether_doc_get_element_by_id(doc, "target");
        inside = aether_doc_get_element_by_id(doc, "inside");
        after = aether_doc_get_element_by_id(doc, "after");
        raw = aether_doc_get_element_by_id(doc, "raw");
        tail = aether_doc_get_element_by_id(doc, "tail");
        value = aether_node_get_text_content(aether_node_get_child(raw, 0));
        ok = ok && host && target && inside && after && raw && tail &&
            aether_node_get_parent(inside) == target && aether_node_get_parent(after) == host &&
            aether_node_get_parent(raw) == host && aether_node_get_parent(tail) == host &&
            aether_node_get_child_count(host) == 4 && aether_node_get_child_count(target) == 2 &&
            aether_element_get_attribute_count(target) == 1 && !aether_doc_get_element_by_id(doc, "phantom") &&
            value && strcmp(value, "A&amp;<b id=phantom>") == 0;
        aether_string_free(value);
        ok = ok && aether_doc_parse_html(doc,
            "<body><div id=kept>A</div x='><b id=phantom>") == AETHER_OK;
        target = aether_doc_get_element_by_id(doc, "kept");
        ok = ok && target && aether_node_get_child_count(target) == 1 &&
            !aether_doc_get_element_by_id(doc, "phantom") && !aether_doc_get_element_by_id(doc, "host");
        aether_doc_destroy(doc);
        if (!ok) { fputs("installed HTML end-tag suffix contract failed\n", stderr); return 1; }
    }

    puts("OK: installed AetherUI::Core package, HTML names and end-tag suffixes are consumable from C11");
    return 0;
}
