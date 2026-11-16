#define main legacy_raster_unused_main
#include "test_truetype_raster.cpp"
#undef main
int main(){std::vector<P> curve{{0,0,true},{256,384,false},{256,0,true},{0,0,true}};auto o=outline(face(sfnt({glyph(std::vector<std::vector<P>>(256,curve))})),0);tt::RasterWork w;tt::AggregateBudget r{1000000000},m{64000000};auto result=tt::rasterize_document_budgeted(o,font(o,8192),0,0,r,m,nullptr,&w);assert(std::get<tt::RasterError>(result)==tt::RasterError::budget_exceeded);assert(w.prepass_reserved>0&&w.main_reserved==0&&r.remaining==1000000000-w.prepass_reserved&&m.remaining==64000000);std::cout<<"Document per50M B-only gate PASS B="<<w.prepass_reserved<<"\n";}
