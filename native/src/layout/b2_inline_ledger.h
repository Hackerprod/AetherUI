#pragma once
// Internal B2 provenance building block. Callers must register decoded DOM text,
// not HTML source bytes, and preserve this object through final validation.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory_resource>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_set>

namespace aether::layout::b2 {
struct LimitExceeded : std::runtime_error { LimitExceeded():runtime_error("B2 quota exceeded"){} };
struct InvalidLedger : std::runtime_error { InvalidLedger():runtime_error("Invalid B2 provenance"){} };
inline void require(bool ok) { if(!ok) throw InvalidLedger(); }
inline size_t checked_add(size_t a,size_t b) { if(b>std::numeric_limits<size_t>::max()-a)throw LimitExceeded();return a+b; }
inline size_t checked_mul(size_t a,size_t b) { if(a&&b>std::numeric_limits<size_t>::max()/a)throw LimitExceeded();return a*b; }
enum class Quota : size_t { dom_nodes,text_nodes,comments,dom_bytes,author_scalars,visible_scalars,contexts,participants,spans,tokens,fragments,lines,placements,glyphs,bridge_calls,products,provider_visits,token_visits,count };
inline constexpr std::array<size_t,size_t(Quota::count)> limits{1024,512,256,65536,8192,4096,512,2048,16384,8192,16384,4096,16384,16384,32768,32768,65536,65536};
class Counters {
 std::array<size_t,size_t(Quota::count)> values_{};
public:
 void charge(Quota q,size_t n=1) { auto i=size_t(q);require(i<values_.size()); if(n>limits[i]-values_[i])throw LimitExceeded();values_[i]+=n; }
 size_t used(Quota q)const { return values_.at(size_t(q)); }
};
// All allocations made with this resource are charged before allocation. An
// allocator's reallocation naturally charges new and old buffers concurrently.
// Upstream allocator metadata, DOM and legacy layout storage are not covered.
class MemoryBudget final:public std::pmr::memory_resource {
 std::pmr::memory_resource* upstream_;size_t live_=0,peak_=0,limit_;
 void* do_allocate(size_t bytes,size_t alignment)override {
  if(bytes>limit_-live_)throw LimitExceeded();
  void* p=upstream_->allocate(bytes,alignment);live_+=bytes;if(live_>peak_)peak_=live_;return p;
 }
 void do_deallocate(void* p,size_t bytes,size_t alignment)override {upstream_->deallocate(p,bytes,alignment);live_-=bytes;}
 bool do_is_equal(const std::pmr::memory_resource& other)const noexcept override {return this==&other;}
public:
 explicit MemoryBudget(size_t limit=4*1024*1024,std::pmr::memory_resource* upstream=std::pmr::get_default_resource()):upstream_(upstream),limit_(limit){}
 size_t live()const noexcept{return live_;} size_t peak()const noexcept{return peak_;}
};
using Id=uint32_t;
inline constexpr Id none=std::numeric_limits<Id>::max();
enum class Kind {text,comment,inline_node,br};
enum class Fate {visible,contributor,discarded};
enum class Discard {none,leading,trailing,soft_break,empty_context};
inline bool boundary(std::string_view text,size_t at){return at<=text.size()&&(at==text.size()||(static_cast<unsigned char>(text[at])&0xc0)!=0x80);}
inline bool whitespace(std::string_view text){for(char c:text)if(c!=' '&&c!='\t'&&c!='\r'&&c!='\n'&&c!='\f')return false;return true;}
inline size_t scalar_count(std::string_view text){size_t n=0;for(size_t i=0;i<text.size();){auto c=static_cast<unsigned char>(text[i++]);if(c<0x80){++n;continue;}unsigned extra=c>=0xc2&&c<=0xdf?1:c>=0xe0&&c<=0xef?2:c>=0xf0&&c<=0xf4?3:99;require(extra!=99&&extra<=text.size()-i);uint32_t cp=c&((1u<<(6-extra))-1);for(unsigned j=0;j<extra;++j){auto d=static_cast<unsigned char>(text[i++]);require((d&0xc0)==0x80);cp=(cp<<6)|(d&63);}require(cp>=(extra==1?0x80u:extra==2?0x800u:0x10000u)&&cp<=0x10ffff&&!(cp>=0xd800&&cp<=0xdfff));++n;}return n;}
struct Origin {
 uintptr_t identity,owner,style,font;size_t ordinal;Kind kind;Id context;std::pmr::string text;
 Origin(uintptr_t n,uintptr_t o,uintptr_t s,uintptr_t f,size_t ord,Kind k,Id c,std::string_view t,std::pmr::memory_resource* r):identity(n),owner(o),style(s),font(f),ordinal(ord),kind(k),context(c),text(t,r){}
};
struct Context {uintptr_t owner;size_t sequence;int mode,align;};
struct Decision {Id context,line;Discard reason;};
struct Span {Id origin;size_t begin,end;Fate fate;Id placement=none,group=none,decision=none;size_t run_begin=0,run_end=0;Id expected_token=none,expected_fragment=none;};
struct Line {Id context;float baseline,x,width;};
struct Placement {
 Id context,line,token,fragment,group;uintptr_t owner,style,font;float x,y,width,height,baseline;size_t glyphs;std::pmr::string text;
 Placement(Id c,Id l,Id t,Id f,Id g,uintptr_t o,uintptr_t s,uintptr_t b,float xx,float yy,float w,float h,float base,size_t gs,std::string_view str,std::pmr::memory_resource* r):context(c),line(l),token(t),fragment(f),group(g),owner(o),style(s),font(b),x(xx),y(yy),width(w),height(h),baseline(base),glyphs(gs),text(str,r){}
};
struct Break {Id origin,line;};
class Ledger {
 MemoryBudget memory_;Counters counts_;bool frozen_=false;
 std::pmr::vector<Context> contexts_{&memory_};std::pmr::vector<Origin> origins_{&memory_};
 std::pmr::vector<Id> participants_{&memory_};std::pmr::vector<Span> spans_{&memory_};
 std::pmr::vector<Decision> decisions_{&memory_};std::pmr::vector<Line> lines_{&memory_};
 std::pmr::vector<Placement> placements_{&memory_};std::pmr::vector<Break> breaks_{&memory_};
 static Id id(size_t n){if(n>=none)throw LimitExceeded();return static_cast<Id>(n);}
public:
 explicit Ledger(std::pmr::memory_resource* upstream=std::pmr::get_default_resource()):memory_(4*1024*1024,upstream){}
 Ledger(const Ledger&)=delete;Ledger& operator=(const Ledger&)=delete;
 std::pmr::memory_resource* resource(){return &memory_;}
 const MemoryBudget& memory()const{return memory_;} Counters& counters(){return counts_;} const Counters& counters()const{return counts_;}
 const auto& origins()const{return origins_;}const auto& contexts()const{return contexts_;}const auto& spans()const{return spans_;}const auto& placements()const{return placements_;}const auto& lines()const{return lines_;}
 Id context(uintptr_t owner,size_t sequence,int mode,int align){require(!frozen_&&owner);counts_.charge(Quota::contexts);auto n=id(contexts_.size());contexts_.push_back({owner,sequence,mode,align});return n;}
 // Register flow participants only; charge DOM totals/metadata in caller's
 // postparse traversal, once, using counters().
 Id origin(uintptr_t identity,uintptr_t owner,uintptr_t style,uintptr_t font,size_t ordinal,Kind kind,Id context,std::string_view text={}){
  require(!frozen_&&context<contexts_.size()&&identity&&owner&&style);
  for(const auto& o:origins_){counts_.charge(Quota::token_visits);require(o.identity!=identity);}
  require(kind==Kind::text||kind==Kind::comment||text.empty());
  const auto scalars=scalar_count(text);if(kind==Kind::text)counts_.charge(Quota::author_scalars,scalars);
  counts_.charge(Quota::participants);auto n=id(origins_.size());origins_.emplace_back(identity,owner,style,font,ordinal,kind,context,text,&memory_);participants_.push_back(n);return n;
 }
 void freeze(){require(!frozen_);frozen_=true;}
 Id decision(Id context,Id line,Discard reason){require(frozen_&&context<contexts_.size()&&reason!=Discard::none);if(decisions_.size()>=limits[size_t(Quota::spans)])throw LimitExceeded();auto n=id(decisions_.size());decisions_.push_back({context,line,reason});return n;}
 void span(Span s){require(frozen_);counts_.charge(Quota::spans);spans_.push_back(s);}
 Id line(Id context,float baseline,float x,float width,bool charge=true){require(frozen_&&context<contexts_.size());if(charge)counts_.charge(Quota::lines);auto n=id(lines_.size());lines_.push_back({context,baseline,x,width});return n;}
 Id placement(Id context,Id line,Id token,Id fragment,Id group,uintptr_t owner,uintptr_t style,uintptr_t font,float x,float y,float width,float height,float baseline,size_t glyphs,std::string_view text,bool charge_visible=true,bool charge_final=true){
  require(frozen_);if(charge_final){counts_.charge(Quota::placements);counts_.charge(Quota::glyphs,glyphs);}if(charge_visible)counts_.charge(Quota::visible_scalars,scalar_count(text));auto n=id(placements_.size());placements_.emplace_back(context,line,token,fragment,group,owner,style,font,x,y,width,height,baseline,glyphs,text,&memory_);return n;
 }
 void forced_break(Id origin,Id line){require(frozen_);counts_.charge(Quota::participants);breaks_.push_back({origin,line});}
 // Validation uses bounded PMR scratch and exact origin/range identity. The
 // transport caller additionally checks epochs, run glyph clusters/metrics and
 // real resolver decisions; a caller-supplied string alone is not that proof.
 void validate(){
  require(frozen_);
  for(const auto& o:origins_)require(o.context<contexts_.size()&&o.identity&&o.owner&&o.style);
  std::pmr::vector<size_t> covered(origins_.size(),0,&memory_),placement_bytes(placements_.size(),0,&memory_);
  std::pmr::vector<unsigned> break_counts(origins_.size(),0,&memory_),seen(origins_.size(),0,&memory_);
  for(auto p:participants_){require(p<origins_.size());require(++seen[p]==1);}for(auto n:seen)require(n==1);
  for(size_t i=0;i<lines_.size();++i){const auto& l=lines_[i];require(l.context<contexts_.size()&&std::isfinite(l.baseline)&&std::isfinite(l.x)&&std::isfinite(l.width)&&l.width>=0);if(i&&lines_[i-1].context==l.context)require(lines_[i-1].baseline<=l.baseline);}
  std::pmr::vector<Id> last_origin(contexts_.size(),none,&memory_),last_placement(contexts_.size(),none,&memory_);
  for(const auto& s:spans_){
   counts_.charge(Quota::token_visits);
   require(s.origin<origins_.size());const auto& o=origins_[s.origin];require(o.kind==Kind::text&&s.begin==covered[s.origin]&&s.end>s.begin&&boundary(o.text,s.begin)&&boundary(o.text,s.end));
   if(last_origin[o.context]!=none){require(origins_[last_origin[o.context]].ordinal<=o.ordinal);}
   last_origin[o.context]=s.origin;covered[s.origin]=s.end;
   auto raw=std::string_view(o.text).substr(s.begin,s.end-s.begin);
   if(s.fate==Fate::discarded){require(whitespace(raw)&&s.placement==none&&s.group==none&&s.decision<decisions_.size());const auto& d=decisions_[s.decision];require(d.context==o.context&&d.reason!=Discard::none);if(d.reason==Discard::soft_break)require(d.line<lines_.size()&&lines_[d.line].context==o.context);continue;}
   require(s.decision==none&&s.placement<placements_.size());if(last_placement[o.context]!=none)require(last_placement[o.context]<=s.placement);last_placement[o.context]=s.placement;const auto& p=placements_[s.placement];require(s.expected_token!=none&&s.expected_fragment!=none&&p.token==s.expected_token&&p.fragment==s.expected_fragment);require(p.context==o.context&&s.run_begin<=s.run_end&&s.run_end<=p.text.size());
   if(s.fate==Fate::visible){require(s.group==none&&p.group==none&&p.owner==o.owner&&p.style==o.style&&p.font==o.font&&s.run_begin==placement_bytes[s.placement]&&s.run_end-s.run_begin==raw.size()&&std::string_view(p.text).substr(s.run_begin,s.run_end-s.run_begin)==raw);placement_bytes[s.placement]=s.run_end;}
   else {require(s.fate==Fate::contributor&&whitespace(raw)&&s.group!=none&&s.group==p.group&&p.text==" "&&s.run_begin==0&&s.run_end==1);if(placement_bytes[s.placement]==0)require(p.owner==o.owner&&p.style==o.style&&p.font==o.font);placement_bytes[s.placement]=1;}
  }
  for(size_t i=0;i<origins_.size();++i)require(covered[i]==(origins_[i].kind==Kind::text?origins_[i].text.size():0));
  // Independently constrain whitespace decisions using neighboring visible
  // source ranges and forced breaks. A resolver decision cannot authorize a
  // false leading/trailing discard in the middle of an ordinary visible line.
  std::pmr::vector<Id> previous(spans_.size(),none,&memory_),next(spans_.size(),none,&memory_),last(contexts_.size(),none,&memory_);
  for(size_t i=0;i<spans_.size();++i){counts_.charge(Quota::token_visits);const auto& s=spans_[i];const auto c=origins_[s.origin].context;previous[i]=last[c];if(s.fate==Fate::visible)last[c]=id(i);}
  std::fill(last.begin(),last.end(),none);
  for(size_t i=spans_.size();i>0;){--i;counts_.charge(Quota::token_visits);const auto& s=spans_[i];const auto c=origins_[s.origin].context;next[i]=last[c];if(s.fate==Fate::visible)last[c]=id(i);}
  auto break_between=[&](Id a,Id b,Id context){for(const auto& o:origins_){counts_.charge(Quota::token_visits);if(o.kind==Kind::br&&o.context==context&&o.ordinal>origins_[a].ordinal&&o.ordinal<origins_[b].ordinal)return true;}return false;};
  for(size_t i=0;i<spans_.size();++i){counts_.charge(Quota::token_visits);const auto& s=spans_[i];if(s.fate==Fate::visible)continue;const auto c=origins_[s.origin].context;const auto before=previous[i],after=next[i];
   const bool leading=before==none||break_between(spans_[before].origin,s.origin,c);
   const bool trailing=after==none||break_between(s.origin,spans_[after].origin,c);
   if(s.fate==Fate::contributor){require(!leading&&!trailing);const auto& p=placements_[s.placement];require(placements_[spans_[before].placement].line==p.line&&placements_[spans_[after].placement].line==p.line);}
   else {const auto& d=decisions_[s.decision];switch(d.reason){case Discard::leading:require(leading);break;case Discard::trailing:require(trailing);break;case Discard::empty_context:require(before==none&&after==none);break;case Discard::soft_break:require(!leading&&!trailing&&placements_[spans_[before].placement].line==d.line&&placements_[spans_[after].placement].line>d.line);break;default:require(false);}}
  }

  std::pmr::unordered_set<Id> fragments{&memory_},groups{&memory_};
  for(size_t i=0;i<placements_.size();++i){counts_.charge(Quota::token_visits);const auto& p=placements_[i];require(p.context<contexts_.size()&&p.line<lines_.size()&&lines_[p.line].context==p.context&&p.token!=none&&p.fragment!=none&&p.owner&&p.style&&p.font&&!p.text.empty()&&placement_bytes[i]==p.text.size());for(float f:{p.x,p.y,p.width,p.height,p.baseline})require(std::isfinite(f));require(p.width>=0&&p.height>=0&&p.baseline>=0&&p.y+p.baseline==lines_[p.line].baseline);require(fragments.insert(p.fragment).second);if(p.group!=none)require(groups.insert(p.group).second);if(i&&placements_[i-1].context==p.context){require(placements_[i-1].line<=p.line);if(placements_[i-1].line==p.line)require(placements_[i-1].x+placements_[i-1].width<=p.x);}}
  for(const auto& b:breaks_){require(b.origin<origins_.size()&&origins_[b.origin].kind==Kind::br&&b.line<lines_.size()&&lines_[b.line].context==origins_[b.origin].context&&++break_counts[b.origin]==1);}
  for(size_t i=0;i<origins_.size();++i)require(break_counts[i]==(origins_[i].kind==Kind::br?1u:0u));
 }
#ifdef AETHER_HTML_LTR_TESTING
 auto& test_decisions(){return decisions_;}auto& test_lines(){return lines_;}
 auto& test_spans(){return spans_;}auto& test_origins(){return origins_;}auto& test_participants(){return participants_;}auto& test_breaks(){return breaks_;}auto& test_placements(){return placements_;}
#endif
};
}
