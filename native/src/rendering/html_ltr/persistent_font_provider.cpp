#include "persistent_font_provider.h"
#include "font_product_kernel.h"
namespace aether::rendering::html_ltr {
namespace {
[[noreturn]] void fail(aether_status_t s){throw Error{s};}
void require(bool b){if(!b)fail(AETHER_ERROR_INTERNAL);}
void check(tt::Error e){switch(e){case tt::Error::out_of_memory:fail(AETHER_ERROR_OUT_OF_MEMORY);case tt::Error::limit_exceeded:fail(AETHER_ERROR_LIMIT_EXCEEDED);case tt::Error::unsupported:fail(AETHER_ERROR_UNSUPPORTED);case tt::Error::malformed:fail(AETHER_ERROR_INVALID_ARGUMENT);case tt::Error::cancelled:fail(AETHER_ERROR_INTERNAL);}fail(AETHER_ERROR_INTERNAL);}
void check(tt::BindingError e){switch(e){case tt::BindingError::out_of_memory:fail(AETHER_ERROR_OUT_OF_MEMORY);case tt::BindingError::limit_exceeded:fail(AETHER_ERROR_LIMIT_EXCEEDED);case tt::BindingError::unsupported:fail(AETHER_ERROR_UNSUPPORTED);case tt::BindingError::invalid_input:case tt::BindingError::malformed:fail(AETHER_ERROR_INVALID_ARGUMENT);case tt::BindingError::cancelled:fail(AETHER_ERROR_INTERNAL);}fail(AETHER_ERROR_INTERNAL);}
void check(tt::CmapError e){switch(e){case tt::CmapError::out_of_memory:fail(AETHER_ERROR_OUT_OF_MEMORY);case tt::CmapError::limit_exceeded:fail(AETHER_ERROR_LIMIT_EXCEEDED);case tt::CmapError::unsupported:fail(AETHER_ERROR_UNSUPPORTED);case tt::CmapError::invalid_input:case tt::CmapError::malformed:fail(AETHER_ERROR_INVALID_ARGUMENT);case tt::CmapError::cancelled:fail(AETHER_ERROR_INTERNAL);}fail(AETHER_ERROR_INTERNAL);}
uint32_t scalar(std::string_view s,size_t& at){auto cp=decode_scalar_utf8(s,at);if(cp!=0x2022&&!repertoire_scalar(cp))fail(AETHER_ERROR_UNSUPPORTED);return cp;}
}
std::shared_ptr<PersistentFontProvider> PersistentFontProvider::create(OwnedBudget& budget,const uint8_t* regular,size_t rn,const uint8_t* bold,size_t bn){
 if(!regular||!rn||(!bold&&bn))fail(AETHER_ERROR_INVALID_ARGUMENT);
 if(rn>16u*1024u*1024u||bn>16u*1024u*1024u-rn)fail(AETHER_ERROR_LIMIT_EXCEEDED);
 auto p=std::allocate_shared<PersistentFontProvider>(OwnedAllocator<PersistentFontProvider>(budget,OwnedCategory::glyphs),Token{},budget.handle());
 p->regular_=p->source({regular,rn});if(bn)p->bold_=p->source({bold,bn});return p;
}
PersistentFontProvider::Bundle PersistentFontProvider::source(std::span<const uint8_t> bytes){
 auto source=std::allocate_shared<owned_text::Source>(OwnedAllocator<owned_text::Source>(domain_,OwnedCategory::inputs),domain_,bytes);
 auto fr=tt::detail::FontKernel::face<owned_text::Storage>(source,0,nullptr,nullptr);if(auto e=std::get_if<tt::Error>(&fr))check(*e);
 auto face=std::get<std::shared_ptr<const owned_text::Face>>(std::move(fr));
 auto mr=tt::detail::FontKernel::map<owned_text::Storage>(face,nullptr,nullptr);if(auto e=std::get_if<tt::CmapError>(&mr))check(*e);
 return {std::move(source),std::move(face),std::get<std::shared_ptr<const owned_text::Map>>(std::move(mr))};
}
PersistentFontProvider::Operation::Operation(PersistentFontProvider& p):p_(&p){require(!p.active_);p.active_=true;p.work_={};p.raster_budget={512000000u};p.mask_budget={16u*1024u*1024u};}
PersistentFontProvider::Operation::~Operation(){p_->active_=false;}
void PersistentFontProvider::visit(){if(work_.scalar_visits==65536)fail(AETHER_ERROR_LIMIT_EXCEEDED);++work_.scalar_visits;}
std::shared_ptr<const OwnedFontBinding> PersistentFontProvider::binding(int size,int weight){
 require(active_&&size>=8&&size<=48&&(weight==400||weight==700));const auto& bundle=weight==700?bold_:regular_;if(!bundle.source)fail(AETHER_ERROR_UNSUPPORTED);
 BindingSlot* vacant=nullptr;for(auto& e:bindings_){if(e.value&&e.size==size&&e.weight==weight)return e.value;if(!e.value&&!vacant)vacant=&e;}if(!vacant)fail(AETHER_ERROR_LIMIT_EXCEEDED);
 auto b=std::allocate_shared<OwnedFontBinding>(OwnedAllocator<OwnedFontBinding>(domain_,OwnedCategory::glyphs));b->face=bundle.face;b->map=bundle.map;
 b->font=std::allocate_shared<owned_text::Font>(OwnedAllocator<owned_text::Font>(domain_,OwnedCategory::glyphs),bundle.source,size*64);
 *vacant={size,weight,b};++work_.instances;return b;
}
std::shared_ptr<const owned_text::Outline> PersistentFontProvider::outline(const std::shared_ptr<const OwnedFontBinding>& b,uint32_t glyph){
 require(active_&&bool(b));bool owned=false;for(const auto& entry:bindings_)if(entry.value==b){owned=true;break;}require(owned);
 for(const auto& e:outlines_)if(e.value&&e.source==b->font->source.get()&&e.glyph==glyph)return e.value;
 constexpr uint64_t work=1000000;if(work>FontProvider::outline_work_limit-work_.outline_work_reserved)fail(AETHER_ERROR_LIMIT_EXCEEDED);work_.outline_work_reserved+=work;
 tt::Work observed;auto result=tt::detail::FontKernel::outline<owned_text::Storage>(*b->face,glyph,nullptr,&observed);require(observed.units<=work);if(auto e=std::get_if<tt::Error>(&result))check(*e);
 auto value=std::get<std::shared_ptr<const owned_text::Outline>>(std::move(result));outlines_[next_outline_]={b->font->source.get(),glyph,value};next_outline_=(next_outline_+1)%outlines_.size();++work_.outlines;return value;
}
std::shared_ptr<const OwnedFontProduct> PersistentFontProvider::product(std::string_view source,int size,int weight,int line_height){
 require(active_);if(work_.requests==32768)fail(AETHER_ERROR_LIMIT_EXCEEDED);++work_.requests;
 if(source.size()>text::max_text_bytes)fail(AETHER_ERROR_LIMIT_EXCEEDED);
 require(line_height>=8&&line_height<=96);if(line_height<size)fail(AETHER_ERROR_UNSUPPORTED);
 size_t n=0;for(size_t at=0;at<source.size();){visit();scalar(source,at);++n;}if(n>text::max_glyphs)fail(AETHER_ERROR_LIMIT_EXCEEDED);
 auto p=std::allocate_shared<OwnedFontProduct>(OwnedAllocator<OwnedFontProduct>(domain_,OwnedCategory::glyphs),domain_);p->binding=binding(size,weight);
 auto run=std::allocate_shared<owned_text::Run>(OwnedAllocator<owned_text::Run>(domain_,OwnedCategory::glyphs),domain_,p->binding->font);
 run->source.assign(source);run->glyphs.reserve(n);p->prefixes.reserve(n+1);p->prefixes.push_back(0);
 auto vmr=tt::read_vertical_metrics(p->binding->font->source->bytes,p->binding->face->units_per_em());if(auto e=std::get_if<tt::BindingError>(&vmr))check(*e);
 run->metrics=shape_font_product(source,run->glyphs,p->prefixes,std::get<tt::FontVerticalMetrics>(vmr),size,line_height,
  [&](std::string_view value,size_t& at){visit();return scalar(value,at);},
  [&](uint32_t cp){uint16_t glyph=0;bool found=false;
   for(size_t i=0;i<cmap_size_;++i){const auto& e=cmap_[i];if(e.source==p->binding->font->source.get()&&e.scalar==cp){glyph=e.glyph;found=true;break;}}
   if(!found){if(work_.cmap_lookups==4352||cmap_size_==cmap_.size())fail(AETHER_ERROR_LIMIT_EXCEEDED);++work_.cmap_lookups;
    auto mapped=tt::detail::FontKernel::lookup(*p->binding->map,cp,nullptr);if(auto e=std::get_if<tt::CmapError>(&mapped))check(*e);if(std::holds_alternative<tt::Missing>(mapped))fail(AETHER_ERROR_UNSUPPORTED);
    glyph=std::get<tt::Mapped>(mapped).glyph_id;cmap_[cmap_size_++]={p->binding->font->source.get(),cp,glyph};}
   auto ol=outline(p->binding,glyph);return std::pair{uint32_t(glyph),uint64_t(ol->metrics.advance)};
  });
 p->run=std::move(run);return p;
}
}
