#include <aether/aether.h>
#include "../src/dom/dom_types.h"
#include <aether/aether_contextual_style.h>
#include <string>

#include <cstdio>
#include <cstdlib>

static void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static void capability_transitions() {
    aether_doc_t doc{}; aether_style_runtime_t rt{};
    require(aether_doc_create(&doc)==AETHER_OK,"capability document");
    require(aether_doc_parse_html(doc,"<body><div id='host'><span id='authored'></span></div></body>")==AETHER_OK,"capability DOM");
    auto host=aether_doc_get_element_by_id(doc,"host");
    auto authored=aether_doc_get_element_by_id(doc,"authored");
    aether_style_environment_t env{800,600,1,16,0,0,0};
    require(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&rt)==AETHER_OK,"capability runtime");
    auto add=[&](const char* css) { int64_t id{}; require(aether_style_runtime_add_stylesheet(rt,css,0,"capability",&id,nullptr)==AETHER_OK,"capability add"); return id; };
    auto remove=[&](int64_t id) { require(aether_style_runtime_remove_stylesheet(rt,id),"capability remove"); };
    auto check=[&](const char* before,const char* after) {
        for(int pass=0;pass<2;++pass) {
            aether_style_transaction_t tx{}; aether_style_revision_t revision{};
            require(aether_style_runtime_prepare_revision(rt,doc,&env,&tx)==AETHER_OK,"capability prepare");
            require(aether_style_runtime_commit_revision(rt,tx,&revision)==AETHER_OK,"capability commit");
            aether_style_transaction_destroy(tx); aether_style_revision_destroy(revision);
            require(host->children.size()==1+size_t(before!=nullptr)+size_t(after!=nullptr),"exact pseudo count");
            size_t i=0;
            auto pseudo=[&](const char* text,aether_pseudo_element_t kind) {
                auto node=host->children[i++];
                require(node->is_generated&&node->pseudo_element==kind&&node->parent==host&&node->style_origin==host,"pseudo identity and order");
                require(node->children.size()==size_t(*text!=0),"empty content generates an empty leaf");
                if(*text) require(node->children.front()->text==text,"generated text");
            };
            if(before) pseudo(before,aether_pseudo_element_t::before);
            require(host->children[i++]==authored&&authored->parent==host,"authored child preserved");
            if(after) pseudo(after,aether_pseudo_element_t::after);
        }
    };
    check(nullptr,nullptr);
    auto base=add("#host{--label:'inherited';content:'host'}");
    require(aether_element_set_attribute(host,"style","content:'inline'")==AETHER_OK,"inline content");
    check(nullptr,nullptr);
    auto first=add("#host::before{content:var(--label)}"); check("inherited",nullptr);
    auto second=add("div::before{content:'second'}"); check("inherited",nullptr);
    auto after=add("*::after{content:''}");
    // Universal rule also targets the authored child; host ordering stays exact.
    check("inherited",""); remove(first); check("second",""); remove(second); check(nullptr,"");
    remove(after); check(nullptr,nullptr);
    for(const char* content : {"none","normal","''","'text'","var(--missing)","attr(id)"}) {
        std::string css=std::string("#host::before,#host::after{content:")+content+"}";
        auto id=add(css.c_str());
        const char* expected=std::string(content)=="''"?"":std::string(content)=="'text'"?"text":nullptr;
        check(expected,expected); remove(id); check(nullptr,nullptr);
    }
    auto conditional=add("@media(min-width:900px){.enabled:hover::before{content:'active'}}");
    check(nullptr,nullptr);
    require(aether_element_set_attribute(host,"class","enabled")==AETHER_OK,"class activation");
    aether_element_set_state(host,AETHER_STATE_HOVER);
    check(nullptr,nullptr);
    env.viewport_width=1000;
    check("active",nullptr);
    aether_element_set_state(host,0); check(nullptr,nullptr);
    aether_element_set_state(host,AETHER_STATE_HOVER); aether_style_runtime_clear_cache(rt); check("active",nullptr);
    remove(conditional); check(nullptr,nullptr);
    auto empty=add(""); remove(base); remove(empty); check(nullptr,nullptr);
    aether_style_runtime_destroy(rt); aether_doc_destroy(doc);
}

int main() {
    capability_transitions();
    aether_doc_t document = nullptr;
    require(aether_doc_create(&document) == AETHER_OK, "document creation failed");
    require(aether_doc_parse_html(document, "<body><div id='hud'></div></body>") == AETHER_OK,
            "document parsing failed");

    aether_style_runtime_options_t options{};
    aether_style_environment_t environment{960, 720, 1, 16, 0, 0, 0};
    aether_style_runtime_t runtime = nullptr;
    require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime) == AETHER_OK,
            "runtime creation failed");

    int64_t sheet = 0;
    require(aether_style_runtime_add_stylesheet(
                runtime,
                "#hud::after { content: \"HUD\"; "
                "background: linear-gradient(90deg, #00f2ff, #ff0055), linear-gradient(0deg, #000, #111); "
                "transform: translate(-50%, -50%) rotate(45deg); "
                "box-shadow: 0 0 10px #00f2ff; text-shadow: 1px 1px 2px currentColor; }",
                2, "generated-pseudo.css", &sheet, nullptr) == AETHER_OK,
            "stylesheet compilation failed");

    auto* body = aether_doc_get_body(document);
    require(body != nullptr, "body lookup failed");
    for (int pass = 0; pass < 8; ++pass) {
        aether_style_update_t* updates = nullptr;
        const int32_t count = aether_style_runtime_resolve_subtree(runtime, body, &updates);
        require(count >= (pass == 0 ? 2 : 1) && updates != nullptr,
                "pseudo-element updates were not emitted");

        bool found_dynamic_pseudo_style = false;
        for (int32_t index = 0; index < count; ++index) {
            aether_computed_style_handle_t handle = nullptr;
            require(aether_style_runtime_get_update(updates[index], nullptr, &handle, nullptr) == AETHER_OK,
                    "style update access failed");
            aether_computed_style_t style{};
            require(aether_computed_style_copy(handle, &style) == AETHER_OK,
                    "style update snapshot could not be copied");
            if (style.transforms.count > 0) {
                found_dynamic_pseudo_style = true;
                require(style.background_layers.count == 2, "pseudo background layer list was not preserved");
                require(style.box_shadows.count == 1, "pseudo box shadow list was not preserved");
                require(style.text_shadows.count == 1, "pseudo text shadow list was not preserved");
            }
            aether_computed_style_free(&style);
            aether_computed_style_handle_destroy(handle);
        }
        require(found_dynamic_pseudo_style, "generated pseudo style was missing from the update list");
        aether_style_updates_free(updates, count);
    }

    aether_style_runtime_destroy(runtime);
    aether_doc_destroy(document);
    return 0;
}
