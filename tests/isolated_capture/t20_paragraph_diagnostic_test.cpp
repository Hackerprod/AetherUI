#include "../../native/src/rendering/text/native_paragraph_diagnostic.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
static int fail_after=-1;
void* operator new(std::size_t n) {
    if(fail_after==0) throw std::bad_alloc();
    if(fail_after>0) --fail_after;
    if(auto p=std::malloc(n?n:1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
using namespace aether::rendering::text;
int main() {
    ParagraphDiagnosticBudget b; b.limits={4,8,4,2};
    ParagraphDiagnosticRef out;
    std::string text=" a\t";
    std::vector<ParagraphDiagnosticPiece> pieces={{1,text},{2,{}}};
    assert(freeze_paragraph_diagnostic(pieces,{7,8,9,10,11,12},b,out)==ParagraphDiagnosticStatus::prepared);
    assert(out->source_bytes==text && out->provenance.size()==2);
    assert(out->provenance[1].source_length==0 && out->provenance[1].frozen_begin==3);
    assert(out->context_unproven && out->font_unresolved && out->epochs.input_epoch==9);
    text[0]='x'; assert(out->source_bytes==" a\t");
    const auto historical=out;
    assert(freeze_paragraph_diagnostic(pieces,{},b,out,true)==ParagraphDiagnosticStatus::cancelled);
    assert(out==historical && b.requests==1 && b.source_bytes==3);
    assert(freeze_paragraph_diagnostic(pieces,{},b,out)==ParagraphDiagnosticStatus::prepared);
    assert(b.nodes==4 && b.ranges==4 && b.requests==2 && b.source_bytes==6);
    const auto prior=out;
    assert(freeze_paragraph_diagnostic({}, {},b,out)==ParagraphDiagnosticStatus::resource_limit);
    assert(out==prior && b.requests==2);
    for(int dimension=0;dimension<4;++dimension) {
        ParagraphDiagnosticBudget limited; limited.limits={2,3,2,1};
        if(dimension==0) limited.limits.nodes=1;
        if(dimension==1) limited.limits.source_bytes=2;
        if(dimension==2) limited.limits.ranges=1;
        if(dimension==3) limited.limits.requests=0;
        assert(freeze_paragraph_diagnostic(pieces,{},limited,out)==ParagraphDiagnosticStatus::resource_limit);
        assert(out==prior && limited.nodes==0 && limited.requests==0 && limited.source_bytes==0);
    }
    ParagraphDiagnosticBudget exact; exact.limits={2,3,2,1};
    assert(freeze_paragraph_diagnostic(pieces,{},exact,out)==ParagraphDiagnosticStatus::prepared);
    std::size_t used=std::numeric_limits<std::size_t>::max();
    assert(!paragraph_diagnostic_charge(used,1,used));
    assert(used==std::numeric_limits<std::size_t>::max());
    std::string large(100,'z');
    std::vector<ParagraphDiagnosticPiece> allocating={{5,large},{6,large}};
    int failures=0;
    for(int ordinal=0;ordinal<10;++ordinal) {
        ParagraphDiagnosticBudget fresh;
        auto retained=out;
        fail_after=ordinal;
        try { freeze_paragraph_diagnostic(allocating,{},fresh,out); }
        catch(const std::bad_alloc&) {
            fail_after=-1; ++failures;
            assert(out==retained && fresh.requests==0 && fresh.nodes==0 && fresh.source_bytes==0);
            continue;
        }
        fail_after=-1;
        assert(fresh.source_bytes==200 && out->provenance.size()==2);
        break;
    }
    assert(failures>=3);
    std::cout<<"T2.0 paragraph diagnostic PASS; OOM points="<<failures<<"; shaping=NONE font=UNRESOLVED context=UNPROVEN\n";
}
