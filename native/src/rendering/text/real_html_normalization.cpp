#include "real_html_normalization.h"
#include "real_html_text.h"
namespace aether::rendering::text::truetype {
std::shared_ptr<const HtmlNormalizationProof> HtmlNormalizationProof::create_budgeted(
 HtmlWhiteSpaceProfile profile,std::shared_ptr<const RealFontBinding> binding,
 const std::vector<std::string>& pieces,std::size_t& steps,std::size_t& logical,Cancellation* cancel) {
 struct Counted final:Cancellation {
  std::size_t& steps;Cancellation* inner;std::optional<HtmlTextError> failure;
  Counted(std::size_t& s,Cancellation* c):steps(s),inner(c){}
  bool cancelled() noexcept override {
   if(failure)return true;
   if(steps>=1000000){failure=HtmlTextError::limit_exceeded;return true;}
   ++steps;if(inner&&inner->cancelled()){failure=HtmlTextError::cancelled;return true;}return false;
  }
  void check(){if(cancelled())throw *failure;}
 } counted(steps,cancel);
 counted.check();
 std::size_t bytes=0,empty=0;
 for(const auto& piece:pieces){counted.check();if(piece.size()>4096-bytes)throw HtmlTextError::limit_exceeded;bytes+=piece.size();if(piece.empty())++empty;}
 if(pieces.size()>128||empty>128)throw HtmlTextError::limit_exceeded;
 // Conservative logical reservations, including temporary tokens. This does
 // not measure allocator capacities or RSS. Instrumented steps combine scanner
 // byte/validation steps and normalization checkpoints, not CPU instructions.
 constexpr std::size_t cap=2*1024*1024;
 auto charge=[&](std::size_t count,std::size_t size){if(logical>cap||(size&&(count>(cap-logical)/size)))throw HtmlTextError::limit_exceeded;logical+=count*size;};
 charge(1,sizeof(HtmlNormalizationProof));charge(bytes,3);
 charge(pieces.size(),sizeof(std::string)+sizeof(HtmlSourcePiece));
 charge(bytes+empty,sizeof(HtmlNormalizationEntry)+sizeof(Token));
 try {
  auto proof=create(profile,std::move(binding),pieces,&counted);
  if(counted.failure)throw *counted.failure;
  return proof;
 }catch(...){if(counted.failure)throw *counted.failure;throw;}
}
std::shared_ptr<const HtmlNormalizationProof> HtmlNormalizationProof::create(
 HtmlWhiteSpaceProfile profile,std::shared_ptr<const RealFontBinding> binding,
 const std::vector<std::string>& pieces,Cancellation* cancel) {
 auto check=[&]{if(cancel&&cancel->cancelled())throw HtmlTextError::cancelled;};
 check();
 if(!binding || pieces.size()>128)throw HtmlTextError::invalid_input;
 std::vector<Token> tokens;
 std::vector<HtmlSourcePiece> sources;
 std::string input;
 for(std::size_t ordinal=0;ordinal<pieces.size();++ordinal) {
  check();const auto& piece=pieces[ordinal];
  if(piece.size()>4096-input.size())throw HtmlTextError::limit_exceeded;
  const auto begin=input.size();input+=piece;sources.push_back({ordinal,begin,input.size()});
  if(piece.empty())tokens.push_back({ordinal,0,begin,0,false});
  for(std::size_t at=0;at<piece.size();) {
   check();const auto start=at;auto c=static_cast<unsigned char>(piece[at++]);unsigned cp=c;
   if(c>=128) {
    if((c!=0xc2 && c!=0xc3)||at==piece.size())throw HtmlTextError::unsupported;
    auto tail=static_cast<unsigned char>(piece[at++]);if((tail&192)!=128)throw HtmlTextError::invalid_input;
    cp=((c&31)<<6)|(tail&63);
   }
   const bool whitespace=cp==32||cp==9||cp==10;
   if(!((cp>=32&&cp<=126)||(cp>=160&&cp<=255&&cp!=173)||
       (profile==HtmlWhiteSpaceProfile::nowrap&&(cp==9||cp==10))))throw HtmlTextError::unsupported;
   tokens.push_back({ordinal,start,begin+start,at-start,whitespace});
   if(tokens.size()>4096+128)throw HtmlTextError::limit_exceeded;
  }
 }
 std::string output;
 std::vector<HtmlNormalizationEntry> mapping;mapping.reserve(tokens.size());
 auto record=[&](const Token& t,std::size_t anchor,std::size_t length){
  mapping.push_back({t.piece,t.local,t.length,t.global,anchor,length});
 };
 if(profile==HtmlWhiteSpaceProfile::pre) {
  output=input;
  for(const auto& t:tokens){check();record(t,t.global,t.length);}
 } else {
  for(std::size_t i=0;i<tokens.size();) {
   check();const auto& t=tokens[i];
   if(t.length && !t.whitespace) {
    record(t,output.size(),t.length);output.append(input,t.global,t.length);++i;
   } else {
    const auto start=i;bool whitespace=false;
    while(i<tokens.size()&&(!tokens[i].length||tokens[i].whitespace)) {
     check();whitespace=whitespace||tokens[i].whitespace;++i;
    }
    const auto anchor=output.size();const bool emit=whitespace&&anchor!=0&&i<tokens.size();
    if(emit)output.push_back(' ');
    for(auto j=start;j<i;++j){check();record(tokens[j],anchor,tokens[j].length&&emit?1:0);}
   }
  }
 }
 if(output.size()>input.size()||mapping.size()>4096+128)throw HtmlTextError::limit_exceeded;
 check();
 return std::shared_ptr<const HtmlNormalizationProof>(new HtmlNormalizationProof(profile,std::move(binding),std::move(input),pieces,std::move(sources),std::move(output),std::move(mapping)));
}
}
