#include "typography_evidence.h"
#include <cassert>
#include <iostream>
using namespace aether::style;
int main() {
 TypographyEvidence e; assert(e.family_state==TypographyFieldState::unknown && !e.coverage_complete);
 typography_parse_family("Arial, \"serif\", serif, 'Long, family', sans-serif",e);
 assert(e.family_state==TypographyFieldState::known && e.families.size()==5);
 assert(!e.families[1].generic && e.families[2].generic && e.families[3].name=="Long, family");
 typography_parse_family("'"+std::string(512,'a')+"', sans-serif",e); assert(e.families[0].name.size()==512);
 typography_parse_family("Arial,",e); assert(e.family_state==TypographyFieldState::unsupported && e.families.empty());
 typography_parse_family("serif x",e); assert(e.family_state==TypographyFieldState::unsupported);
 typography_parse_family("\\41 rial, s\\65 rif",e); assert(e.families[0].name=="Arial" && e.families[1].generic);
 typography_parse_family("'serif' foo",e); assert(e.family_state==TypographyFieldState::unsupported);
 typography_parse_style("OBLIQUE",e); assert(e.style_state==TypographyFieldState::known && e.font_style=="oblique");
 typography_parse_style("oblique 10deg",e); assert(e.style_state==TypographyFieldState::unsupported);
 typography_parse_spacing("normal",e); assert(e.spacing_normal && e.spacing_state==TypographyFieldState::known);
 typography_parse_spacing("0",e); assert(!e.spacing_normal && e.spacing_px==0 && e.spacing_state==TypographyFieldState::known);
 typography_parse_spacing("-2pt",e); assert(e.spacing_px==-2*96/72.0);
 typography_parse_spacing("1em",e); assert(e.spacing_state==TypographyFieldState::unsupported);
 typography_parse_spacing("1px 2px",e); assert(e.spacing_state==TypographyFieldState::unsupported);
 typography_parse_spacing("1e2px",e); assert(e.spacing_px==100);
 typography_parse_spacing("1px/**/",e); assert(e.spacing_px==1);
 typography_parse_family(std::string(65537,'a'),e); assert(e.family_state==TypographyFieldState::unsupported && e.families.empty());
 std::string many="A";for(int i=0;i<256;++i)many+=",A";
 typography_parse_family(many,e);assert(e.family_state==TypographyFieldState::unsupported && e.families.empty());
 typography_parse_family("Arial/**/,/**/serif",e);assert(e.family_state==TypographyFieldState::known && e.families.size()==2);
 typography_parse_family("initial",e);assert(e.family_state!=TypographyFieldState::known);
 typography_parse_spacing("calc(1px + 1px)",e);assert(e.spacing_state==TypographyFieldState::unsupported);
 typography_parse_spacing("1e999px",e);assert(e.spacing_state==TypographyFieldState::unsupported);
 typography_parse_spacing("12",e);assert(e.spacing_state==TypographyFieldState::unsupported);
 typography_parse_spacing("-.5px",e);assert(e.spacing_state==TypographyFieldState::known && e.spacing_px==-.5);
 auto copy=e; assert(copy==e); assert(!copy.coverage_complete);
 std::cout<<"T1.8 typography parser PASS; complete lists, quoted/generic, limits, units, unsupported forms\n";
}
