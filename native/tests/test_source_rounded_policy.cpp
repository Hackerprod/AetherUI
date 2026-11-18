// Internal seam regression: source authorization must not leak into LTR profiles.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "rendering/html_ltr/paint_finalize.h"
#include "rendering/display_list_types.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>
using namespace aether::rendering;
using namespace aether::rendering::html_ltr;
static std::vector<unsigned char> read(const char* p){std::ifstream f(p,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
static aether_corner_radii radii(float r){return {r,r,r,r,r,r,r,r};}
static aether_paint_command_t fill(float r){aether_paint_command_t c{};c.tag=AETHER_PAINT_FILL_ROUNDED_RECT;c.data.fill_rounded_rect={{0,0,80,60},radii(r),{1,0,0,1}};return c;}
static aether_paint_command_t stroke(float r,float width){aether_paint_command_t c{};c.tag=AETHER_PAINT_STROKE_ROUNDED_RECT;c.data.stroke_rounded_rect.rect={0,0,80,60};c.data.stroke_rounded_rect.radii=radii(r);c.data.stroke_rounded_rect.color={1,0,0,1};c.data.stroke_rounded_rect.stroke_width=width;return c;}
static void check(FontProvider& fonts,std::vector<aether_paint_command_t> commands,uint32_t profile,BoxPaintPolicy policy,aether_status_t expected,bool focus=false,bool corrupt_focus=false){
 NativePaintList n;NativeListAccess::list(n)=new aether_display_list_s;NativeListAccess::list(n)->commands=commands;
 aether_doc_t doc{};
 if(focus){assert(aether_doc_create(&doc)==AETHER_OK);assert(aether_doc_parse_html(doc,"<html><body></body></html>")==AETHER_OK);auto owner=aether_doc_get_document_element(doc);assert(owner);n.interaction_focus={owner,owner,{2,2,76,56},0};if(corrupt_focus)n.interaction_focus.border.width+=1;}
 aether_status_t status=AETHER_OK;try{const auto masks=finalize_paint(n,{},fonts,{},profile,"AetherLTR",policy);assert(masks.empty());}catch(const Error& e){status=e.status;}
 if(status!=expected)std::fprintf(stderr,"policy %d profile %u got %d expected %d\n",int(policy),profile,status,expected);
 assert(status==expected);aether_doc_destroy(doc);
}
int main(int argc,char**argv){assert(argc==3);FontProvider fonts(read(argv[1]),read(argv[2]),2);
 constexpr auto source=BoxPaintPolicy::source_rounded,square=BoxPaintPolicy::square_only;
 for(uint32_t profile:{1u,2u}){
  check(fonts,{fill(12)},profile,square,AETHER_ERROR_UNSUPPORTED);
  check(fonts,{stroke(11,2)},profile,square,AETHER_ERROR_UNSUPPORTED);
  check(fonts,{fill(0),stroke(0,128)},profile,square,AETHER_OK);
  check(fonts,{fill(12),stroke(11,2)},profile,source,AETHER_OK);
  check(fonts,{stroke(4,8)},profile,source,AETHER_OK); // b=outer R
  check(fonts,{fill(0),stroke(0,128)},profile,source,AETHER_OK);
 }
 // Omitted policy argument remains square-only, regardless of profile.
 for(uint32_t profile:{1u,2u}){NativePaintList n;NativeListAccess::list(n)=new aether_display_list_s;NativeListAccess::list(n)->commands={fill(12)};bool rejected=false;try{finalize_paint(n,{},fonts,{},profile);}catch(const Error&e){assert(e.status==AETHER_ERROR_UNSUPPORTED);rejected=true;}assert(rejected);}
 auto c=fill(12);c.data.fill_rounded_rect.radii.top_left_y=11;check(fonts,{c},2,source,AETHER_ERROR_UNSUPPORTED);
 c=fill(12);c.data.fill_rounded_rect.radii.bottom_right_x=10;check(fonts,{c},2,source,AETHER_ERROR_UNSUPPORTED);
 check(fonts,{fill(-1)},2,source,AETHER_ERROR_UNSUPPORTED);
 check(fonts,{fill(33)},2,source,AETHER_ERROR_UNSUPPORTED);
 c=fill(12);c.data.fill_rounded_rect.rect.width=20;check(fonts,{c},2,source,AETHER_ERROR_UNSUPPORTED);
 c=fill(12);c.data.fill_rounded_rect.rect.height=0;check(fonts,{c},2,source,AETHER_ERROR_UNSUPPORTED);
 c=fill(12);c.data.fill_rounded_rect.radii.top_right_y=std::numeric_limits<float>::quiet_NaN();check(fonts,{c},2,source,AETHER_ERROR_INTERNAL);
 check(fonts,{stroke(12,9)},2,source,AETHER_ERROR_UNSUPPORTED);
 check(fonts,{stroke(1,3)},2,source,AETHER_ERROR_UNSUPPORTED); // b > recovered outer R
 check(fonts,{stroke(30,8)},2,source,AETHER_ERROR_UNSUPPORTED); // outer R > 32
 check(fonts,{stroke(12,0)},2,source,AETHER_ERROR_UNSUPPORTED);
 check(fonts,{stroke(12,-1)},2,source,AETHER_ERROR_UNSUPPORTED);
 check(fonts,{stroke(12,std::numeric_limits<float>::quiet_NaN())},2,source,AETHER_ERROR_INTERNAL);
 check(fonts,{stroke(12,std::numeric_limits<float>::infinity())},2,source,AETHER_ERROR_INTERNAL);
 c=fill(13.75f);c.data.fill_rounded_rect.rect={0,0,27.5f,35.5f};check(fonts,{c},2,source,AETHER_OK);
 aether_paint_command_t push{},pop{};push.tag=AETHER_PAINT_PUSH_CLIP;push.data.push_clip.rect={0,0,80,60};push.data.push_clip.radii=radii(12);pop.tag=AETHER_PAINT_POP_CLIP;
 check(fonts,{push,fill(12),pop},2,source,AETHER_ERROR_UNSUPPORTED);push.data.push_clip.radii={};check(fonts,{push,fill(12),pop},2,source,AETHER_OK);
 // Existing rounded translucent interaction focus is a separate exact contract.
 auto focus=stroke(2,1);focus.data.stroke_rounded_rect.color={0.145f,0.388f,0.922f,0.75f};
 for(auto policy:{square,source}){check(fonts,{focus},2,policy,AETHER_OK,true);check(fonts,{focus},2,policy,AETHER_ERROR_INTERNAL,true,true);}
 puts("SOURCE ROUNDED POLICY: shared profiles stay square, source validation and focus invariants PASS");
}
