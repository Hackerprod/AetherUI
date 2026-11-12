#pragma once
// Private initial-empty canvas profile. No bitmap allocation or public ABI.
#include "layout_types.h"
#include <stdexcept>
inline unsigned initial_canvas_dimension(const std::string* value, unsigned fallback) {
    if (!value) return fallback;
    size_t i=0; while(i<value->size() && ((*value)[i]==' '||(*value)[i]=='\t'||(*value)[i]=='\n'||(*value)[i]=='\r'||(*value)[i]=='\f')) ++i;
    if(i<value->size() && (*value)[i]=='+') ++i;
    if(i==value->size() || (*value)[i]<'0'||(*value)[i]>'9') return fallback;
    unsigned n=0;
    while(i<value->size() && (*value)[i]>='0'&&(*value)[i]<='9') {
        unsigned digit=static_cast<unsigned>((*value)[i++]-'0');
        if(n>(2147483647u-digit)/10u) return fallback;
        n=n*10+digit;
    }
    return n;
}
inline void set_initial_canvas_mode(aether_layout_engine_s* e,bool enabled) {
    if(!e || e->layout_in_progress) throw std::logic_error("CANVAS_MODE_BUSY");
    e->initial_canvas_mode=enabled; e->initial_canvas_entries.clear();
}
inline const initial_canvas_dimensions* initial_canvas_for(const aether_layout_engine_s* e,const aether_node_s* n) {
    if(!e || !e->initial_canvas_mode) return nullptr;
    auto it=e->initial_canvas_entries.find(const_cast<aether_node_s*>(n));
    return it==e->initial_canvas_entries.end()?nullptr:&it->second;
}
inline void prepare_initial_canvas(aether_layout_engine_s* e,aether_node_s* root) {
    e->initial_canvas_entries.clear(); if(!e->initial_canvas_mode) return;
    std::vector<aether_node_s*> todo{root}; size_t visited=0;
    while(!todo.empty()) {
        auto* n=todo.back(); todo.pop_back();
        if(++visited>50000) throw std::length_error("CANVAS_DOM_LIMIT");
        if(n->tag_name=="canvas") {
            if(e->initial_canvas_entries.size()==256) throw std::length_error("CANVAS_COUNT_LIMIT");
            const auto attr=[&](const char* key,unsigned def){ auto it=n->attributes.find(key);return initial_canvas_dimension(it==n->attributes.end()?nullptr:&it->second,def);};
            unsigned w=attr("width",300),h=attr("height",150);
            if(w>8192||h>8192) throw std::length_error("CANVAS_DIMENSION_LIMIT");
            e->initial_canvas_entries.emplace(n,initial_canvas_dimensions{static_cast<float>(w),static_cast<float>(h)});
        }
        todo.insert(todo.end(),n->children.begin(),n->children.end());
    }
}
// Inputs are resolved CSS lengths: NaN denotes auto. Returns CONTENT size.
inline initial_canvas_dimensions initial_canvas_content_size(initial_canvas_dimensions intrinsic,
    float css_width,float css_height,bool border_box,float dx,float dy) {
    bool w=std::isfinite(css_width),h=std::isfinite(css_height);
    if(w) css_width=std::max(0.f,css_width-(border_box?dx:0.f));
    if(h) css_height=std::max(0.f,css_height-(border_box?dy:0.f));
    if(w!=h && (intrinsic.width==0 || intrinsic.height==0)) throw std::invalid_argument("CANVAS_ZERO_RATIO_UNSUPPORTED");
    return {w?css_width:h?css_height*intrinsic.width/intrinsic.height:intrinsic.width,
            h?css_height:w?css_width*intrinsic.height/intrinsic.width:intrinsic.height};
}
