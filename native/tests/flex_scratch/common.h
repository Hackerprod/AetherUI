#pragma once
#include "flex_resolver.h"
#include <bit>
#include <cstdlib>
#include <stdexcept>
static bool eq(float a,float b){return std::bit_cast<uint32_t>(a)==std::bit_cast<uint32_t>(b);}
static bool eq_input(const aether_flex_item_input_s& a,const aether_flex_item_input_s& b){
 return a.basis.kind==b.basis.kind && a.basis.length_is_definite==b.basis.length_is_definite && eq(a.basis.specified_main_size,b.basis.specified_main_size) && eq(a.basis.content_main_size,b.basis.content_main_size) && eq(a.basis.resolved_length,b.basis.resolved_length) && eq(a.min_main_size,b.min_main_size) && eq(a.max_main_size,b.max_main_size) && eq(a.outer_main_size,b.outer_main_size) && eq(a.flex_grow,b.flex_grow) && eq(a.flex_shrink,b.flex_shrink) && eq(a.main_decoration_size,b.main_decoration_size) && a.automatic_minimum.is_scroll_container==b.automatic_minimum.is_scroll_container && eq(a.automatic_minimum.content_size_suggestion,b.automatic_minimum.content_size_suggestion) && eq(a.automatic_minimum.specified_size_suggestion,b.automatic_minimum.specified_size_suggestion) && eq(a.automatic_minimum.transferred_size_suggestion,b.automatic_minimum.transferred_size_suggestion) && eq(a.automatic_minimum.max_main_size,b.automatic_minimum.max_main_size);
}
static void check_result(const aether_flex_resolve_result_s& a,const aether_flex_resolve_result_s& b){
 if(a.items.size()!=b.items.size()||a.lines.size()!=b.lines.size())std::abort();
 for(size_t i=0;i<a.items.size();++i){auto x=a.items[i],y=b.items[i];if(!eq(x.flex_base_size,y.flex_base_size)||!eq(x.hypothetical_main_size,y.hypothetical_main_size)||!eq(x.target_main_size,y.target_main_size)||x.frozen!=y.frozen||!eq(x.inner_flex_base_size,y.inner_flex_base_size))std::abort();}
 for(size_t i=0;i<a.lines.size();++i)if(a.lines[i].item_indices!=b.lines[i].item_indices||!eq(a.lines[i].hypothetical_outer_main_size,b.lines[i].hypothetical_outer_main_size))std::abort();
}

static void check_input(const aether_flex_resolve_input_s& in, const aether_flex_resolve_input_s& before, const std::vector<aether_flex_item_input_s>& items) {
 if(in.items!=before.items || !eq(in.available_main_size,before.available_main_size) || !eq(in.gap,before.gap) || in.wrap!=before.wrap) std::abort();
 if(in.items) {if(in.items->size()!=items.size()) std::abort();for(size_t k=0;k<items.size();++k)if(!eq_input((*in.items)[k],items[k])) std::abort();}
}
