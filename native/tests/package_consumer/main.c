#include <aether/aether.h>

#include <stdio.h>
#include <string.h>

/* Installed public headers only; calls and checks remain active under NDEBUG. */
static int installed_attribute_values(void) {
    aether_doc_t doc = NULL;
    aether_style_runtime_t runtime = NULL;
    aether_node_t node = NULL;
    aether_style_runtime_options_t options = {0};
    aether_style_environment_t environment = {800, 600, 1, 16, 0, 0, 0};
    int64_t sheet = 0;
    int32_t rules = 0;
    int ok = 1, have_style = 0;
    float width = 0, height = 0, opacity = 0, left = 0;
    const char* values[] = {"I", "i", "OTHER", "I"};
    if (aether_doc_create(&doc) != AETHER_OK || !doc) return 0;
    if (aether_doc_parse_html(doc, "<body><DIV id='I' TYPE='I' data-x='VALUE'></DIV></body>") != AETHER_OK) {
        aether_doc_destroy(doc); return 0;
    }
    node = aether_doc_get_element_by_id(doc, "I");
    if (!node || aether_style_runtime_create(&options, &environment, NULL, NULL, &runtime) != AETHER_OK) {
        aether_doc_destroy(doc); return 0;
    }
    if (aether_style_runtime_add_stylesheet(runtime,
        "#I{width:10px;height:10px;opacity:1;margin-left:0}"
        "#I[type=i]{width:31px}#I[type=i i]{height:41px}"
        "#I[type=i s]{opacity:.5}#I[type=I s]{margin-left:17px}"
        "#I[data-x=value]{width:99px}#I[data-x=value s]{height:99px}",
        1, "installed-attribute-values.css", &sheet, &rules) != AETHER_OK || rules != 7) ok = 0;
    for (int pass = 0; ok && pass < 4; ++pass) {
        aether_style_update_t* updates = NULL;
        char* actual;
        int count;
        int canonical_type = 0;
        if (pass && aether_element_set_attribute(node, "TYPE", values[pass]) != AETHER_OK) ok = 0;
        actual = aether_element_get_attribute(node, "type");
        ok = ok && actual && strcmp(actual, values[pass]) == 0;
        aether_string_free(actual);
        actual = aether_element_get_attribute(node, "DATA-X");
        ok = ok && actual && strcmp(actual, "VALUE") == 0;
        aether_string_free(actual);
        for (int j = 0; j < aether_element_get_attribute_count(node); ++j) {
            char* key = aether_element_get_attribute_name(node, j);
            char* value = aether_element_get_attribute_value(node, j);
            if (key && strcmp(key, "type") == 0) {
                ++canonical_type;
                ok = ok && value && strcmp(value, values[pass]) == 0;
            }
            aether_string_free(key); aether_string_free(value);
        }
        ok = ok && canonical_type == 1 && aether_element_get_attribute_count(node) == 3;
        count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_document_element(doc), &updates);
        if (count < 0) { ok = 0; break; }
        for (int j = 0; j < count; ++j) {
            aether_node_t updated = NULL;
            aether_computed_style_handle_t handle = NULL;
            if (aether_style_runtime_get_update(updates[j], &updated, &handle, NULL) != AETHER_OK) {
                ok = 0; continue;
            }
            if (updated == node) {
                aether_computed_style_t style = {0};
                if (aether_computed_style_copy(handle, &style) != AETHER_OK) ok = 0;
                else {
                    width = style.width.value; height = style.height.value;
                    opacity = style.opacity; left = style.margin.left.value; have_style = 1;
                }
                aether_computed_style_free(&style);
            }
            aether_computed_style_handle_destroy(handle);
        }
        aether_style_updates_free(updates, count);
        ok = ok && have_style && width == (pass == 2 ? 10.f : 31.f) &&
            height == (pass == 2 ? 10.f : 41.f) && opacity == (pass == 1 ? .5f : 1.f) &&
            left == (pass == 0 || pass == 3 ? 17.f : 0.f);
        if (!ok) fprintf(stderr, "installed attribute values pass=%d width=%g height=%g opacity=%g left=%g\n",
                         pass, width, height, opacity, left);
    }
    aether_style_runtime_destroy(runtime);
    aether_doc_destroy(doc);
    return ok;
}

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

    {
        aether_doc_t doc = NULL;
        aether_node_t body, child, after;
        /* HTML source preprocessing normalizes CRLF to LF, including comments. */
        const char* expected[] = {"", "", "bang", "literal&amp;\n"};
        int ok;
        if (aether_doc_create(&doc) != AETHER_OK || !doc) return 1;
        ok = aether_doc_parse_html(doc,
            "<body><!--><!---><!--bang--!><!--literal&amp;\r\n--><p id=after>tail</p></body>") == AETHER_OK;
        body = aether_doc_get_body(doc);
        after = aether_doc_get_element_by_id(doc, "after");
        ok = ok && body && after && aether_node_get_child_count(body) == 5 &&
            aether_node_get_child(body, 4) == after && aether_node_get_parent(after) == body;
        for (int i = 0; i < 4; ++i) {
            char* value;
            child = aether_node_get_child(body, i);
            value = aether_node_get_text_content(child);
            ok = ok && child && aether_node_get_type(child) == AETHER_NODE_COMMENT &&
                aether_node_get_parent(child) == body && aether_node_get_child_count(child) == 0 &&
                value && strcmp(value, expected[i]) == 0;
            aether_string_free(value);
        }
        ok = ok && aether_doc_parse_html(doc, "<body><!--eof--!\0--><p id=phantom>") == AETHER_OK;
        body = aether_doc_get_body(doc);
        child = aether_node_get_child(body, 0);
        char* value = aether_node_get_text_content(child);
        ok = ok && body && child && aether_node_get_child_count(body) == 1 &&
            aether_node_get_type(child) == AETHER_NODE_COMMENT && aether_node_get_parent(child) == body &&
            value && strcmp(value, "eof") == 0 && !aether_doc_get_element_by_id(doc, "after") &&
            !aether_doc_get_element_by_id(doc, "phantom");
        aether_string_free(value);
        aether_doc_destroy(doc);
        if (!ok) { fputs("installed HTML comment cycle contract failed\n", stderr); return 1; }
    }

    if (!installed_attribute_values()) { fputs("installed HTML attribute value modes failed\n", stderr); return 1; }

    puts("OK: installed AetherUI::Core package, HTML names, end tags and comments are consumable from C11");
    return 0;
}
