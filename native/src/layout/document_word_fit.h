#pragma once
#include "inline_line_resolver.h"
#include "document_text_context.h"
#include <span>

namespace aether::layout {
// Internal adapter component. Callers supply only already-certified catalog
// entries with one authenticated used-policy tuple for the entire word. Width
// queries have no callbacks and return integer-prefix-derived local doubles.
// This helper neither creates source authority nor changes legacy line breaking.
struct DocumentWordPart {
 std::size_t table{}, boundaries{};
 double height{},baseline{};
};
struct DocumentWordPiece {
 std::size_t table{},begin{},end{};
 double width{},height{},baseline{};
};
class DocumentWordFit final {
 static double sum(double a,double b){
  const double out=a+b;
  if(!std::isfinite(out)||out<0)throw std::overflow_error("DOCUMENT_FIT_ADVANCE");
  return out;
 }
public:
 // emit must append exactly this piece to the same resolver and retain the
 // construction interval; close must close/reset it. Neither may call external
 // code or invalidate the catalog phase. The enclosing adapter owns rollback.
 template<class Width,class Emit,class Close>
 static void append(std::span<const DocumentWordPart> parts,
  document_inline_line_resolver& line,double available,bool opportunity,
  bool emergency,DocumentTextBudget& budget,Width width,Emit emit,Close close){
  if(!std::isfinite(available)||available<0)
   throw std::invalid_argument("DOCUMENT_FIT_AVAILABLE");
  double whole=0;
  for(const auto& part:parts){
   budget.normalized(1);
   if(part.boundaries<2||!std::isfinite(part.height)||part.height<0||
      !std::isfinite(part.baseline)||part.baseline<0||part.baseline>part.height)
    throw std::invalid_argument("DOCUMENT_FIT_PART");
   whole=sum(whole,width(part.table,0,part.boundaries-1));
  }
  // CSS ordinary wrapping moves a word before considering emergency splits.
  if(line.needs_break(whole,opportunity))close();
  if(!emergency||sum(line.physical_width(),whole)<=available){
   for(const auto& part:parts){
    budget.normalized(1);
    emit(DocumentWordPiece{part.table,0,part.boundaries-1,
      width(part.table,0,part.boundaries-1),part.height,part.baseline});
   }
   return;
  }
  for(const auto& part:parts){
   std::size_t begin=0;
   const auto last=part.boundaries-1;
   while(begin<last){
    std::size_t end=begin;
    double accepted=0;
    // Every query is a local exact prefix subtraction, not summed rounded
    // glyph advances. Distinct zero-width boundaries still make progress.
    for(std::size_t candidate=begin+1;candidate<=last;++candidate){
     budget.normalized(1);
     const double local=width(part.table,begin,candidate);
     if(sum(line.physical_width(),local)>available)break;
     end=candidate;accepted=local;
    }
    if(end==begin){
     if(!line.empty()){close();continue;}
     // One authenticated indivisible unit overflows an empty line. Never loop
     // forever or drop it merely because it cannot fit the available width.
     end=begin+1;accepted=width(part.table,begin,end);
    }
    emit(DocumentWordPiece{part.table,begin,end,accepted,part.height,part.baseline});
    begin=end;
    if(begin<last)close();
   }
  }
 }
};
}
