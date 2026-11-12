#pragma once
// Private, immutable CSS comparison-math subset. No public length ABI changes.
#include "css_numeric_conversion_internal.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace aether::css {
enum class math_kind { number, length };
enum class math_op { literal, add, subtract, multiply, divide, minimum, maximum, clamp };
enum class math_unit { number, px, percent, em, rem, vw, vh };
struct math_node;
using math_tree = std::shared_ptr<const math_node>;
struct math_node {
    math_kind kind = math_kind::number;
    math_op op = math_op::literal;
    math_unit unit = math_unit::number;
    double value = 0;
    bool percentage = false;
    std::vector<math_tree> children;
};
namespace contextual_math_detail {
inline constexpr size_t max_nodes = 256, max_depth = 32, max_source = 16384;
inline bool space(char c) { return c==' ' || c=='\t' || c=='\r' || c=='\n' || c=='\f'; }
inline bool digit(char c) { return c>='0' && c<='9'; }
inline bool alpha(char c) { return (c>='a'&&c<='z') || (c>='A'&&c<='Z'); }
inline int hex(char c) { return digit(c)?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1; }
inline bool eval(const math_tree& t, double basis, double& out, size_t depth, size_t& budget) {
    if (!t || depth > max_depth || !budget--) return false;
    double result=t->value;
    if (t->op==math_op::literal) {
        if (t->unit==math_unit::percent) result*=basis/100.;
        else if (t->unit!=math_unit::px && t->unit!=math_unit::number) return false;
    } else {
        std::vector<double> args;
        for (const auto& c:t->children) { double v; if(!eval(c,basis,v,depth+1,budget)) return false; args.push_back(v); }
        if (args.empty()) return false;
        switch(t->op) {
        case math_op::add: if(args.size()!=2)return false; result=args[0]+args[1]; break;
        case math_op::subtract: if(args.size()!=2)return false; result=args[0]-args[1]; break;
        case math_op::multiply: if(args.size()!=2)return false; result=args[0]*args[1]; break;
        case math_op::divide: if(args.size()!=2||args[1]==0)return false; result=args[0]/args[1]; break;
        case math_op::minimum: result=*std::min_element(args.begin(),args.end()); break;
        case math_op::maximum: result=*std::max_element(args.begin(),args.end()); break;
        case math_op::clamp: if(args.size()!=3)return false; result=std::max(args[0],std::min(args[1],args[2])); break;
        default:return false;
        }
    }
    if(!std::isfinite(result))return false;
    out=result; return true;
}
class parser {
    std::string_view s; size_t p=0,nodes=0; c_numeric_locale locale;
    bool trivia(bool* ws=nullptr) {
        if(ws)*ws=false;
        while(p<s.size()) {
            if(space(s[p])) { if(ws)*ws=true; ++p; }
            else if(s.substr(p,2)=="/*") { auto e=s.find("*/",p+2); if(e==s.npos)return false; p=e+2; }
            else break;
        } return true;
    }
    bool take(char c) { if(p<s.size()&&s[p]==c){++p;return true;}return false; }
    bool ident(std::string& out) {
        out.clear();
        while(p<s.size()) {
            unsigned c=static_cast<unsigned char>(s[p]);
            if(alpha(s[p])||s[p]=='-'||s[p]=='_'||digit(s[p])) ++p;
            else if(s[p]=='\\') {
                if(++p==s.size() || s[p]=='\n'||s[p]=='\r'||s[p]=='\f')return false;
                if(hex(s[p])>=0) {
                    c=0; int count=0;
                    while(p<s.size()&&hex(s[p])>=0&&count++<6)c=c*16+hex(s[p++]);
                    if(p<s.size()&&space(s[p])) {char w=s[p++];if(w=='\r'&&p<s.size()&&s[p]=='\n')++p;}
                } else c=static_cast<unsigned char>(s[p++]);
            } else break;
            // All supported identifiers are ASCII. Unicode replacement cannot match.
            if(c==0||c>127)return false;
            out.push_back(c>='A'&&c<='Z'?char(c+32):char(c));
        } return !out.empty();
    }
    math_tree make(math_node v) {
        if(++nodes>max_nodes)return {};
        return std::make_shared<const math_node>(std::move(v));
    }
    math_tree operation(math_op op, std::vector<math_tree> args) {
        if(args.empty())return {};
        math_kind kind=args[0]->kind;
        if(op==math_op::multiply) {
            if(args[0]->kind==math_kind::length&&args[1]->kind==math_kind::length)return {};
            if(args[1]->kind==math_kind::length)kind=math_kind::length;
        } else if(op==math_op::divide) {
            if(args[1]->kind!=math_kind::number||args[1]->value==0)return {};
        } else for(auto& a:args)if(a->kind!=kind)return {};
        math_node n; n.kind=kind;n.op=op;n.children=std::move(args);
        for(auto& a:n.children)n.percentage|=a->percentage;
        auto tree=make(std::move(n)); if(!tree)return {};
        // Number subexpressions are constant. Folding rejects division by zero
        // and overflow before any contextual style can be published.
        if(kind==math_kind::number) {
            double value;size_t budget=max_nodes;
            if(!eval(tree,0,value,0,budget))return {};
            math_node literal;literal.value=value; return make(std::move(literal));
        } return tree;
    }
    math_tree numeric() {
        size_t begin=p;
        if(p<s.size()&&(s[p]=='+'||s[p]=='-'))++p;
        bool digits=false,nonzero=false;
        while(p<s.size()&&digit(s[p])) {digits=true;nonzero|=s[p]!='0';++p;}
        if(p+1<s.size()&&s[p]=='.'&&digit(s[p+1])) {++p;while(p<s.size()&&digit(s[p])) {digits=true;nonzero|=s[p]!='0';++p;}}
        if(!digits)return {};
        if(p<s.size()&&(s[p]=='e'||s[p]=='E')) {
            size_t end=p+1;if(end<s.size()&&(s[end]=='+'||s[end]=='-'))++end;
            size_t e=end;while(end<s.size()&&digit(s[end]))++end;if(end!=e)p=end;
        }
        auto number=locale.parse_double(std::string(s.substr(begin,p-begin)));
        if(!number.valid||number.range_error||(number.value==0&&nonzero))return {};
        math_node n;n.value=number.value;
        std::string unit;
        if(take('%'))unit="%";
        else if(p<s.size()&&(alpha(s[p])||s[p]=='\\'||s[p]=='_')) {if(!ident(unit))return {};}
        if(!unit.empty()) {
            n.kind=math_kind::length;
            if(unit=="px")n.unit=math_unit::px;
            else if(unit=="%") {n.unit=math_unit::percent;n.percentage=true;}
            else if(unit=="em")n.unit=math_unit::em;
            else if(unit=="rem")n.unit=math_unit::rem;
            else if(unit=="vw")n.unit=math_unit::vw;
            else if(unit=="vh")n.unit=math_unit::vh;
            else return {};
        } return make(std::move(n));
    }
    math_tree primary(size_t depth) {
        if(depth>max_depth||!trivia()||p==s.size())return {};
        if(take('(')) {auto t=sum(depth+1);if(!t||!trivia()||!take(')'))return {};return t;}
        if(alpha(s[p])||s[p]=='\\') {
            std::string name;if(!ident(name)||!take('('))return {};
            auto first=sum(depth+1);if(!first||!trivia())return {};
            if(name=="calc") {if(!take(')'))return {};return first;}
            math_op op;if(name=="min")op=math_op::minimum;else if(name=="max")op=math_op::maximum;else if(name=="clamp")op=math_op::clamp;else return {};
            std::vector<math_tree> args{first};
            while(take(',')) {auto t=sum(depth+1);if(!t||!trivia())return {};args.push_back(t);}
            if(!take(')')||(op==math_op::clamp&&args.size()!=3))return {};
            return operation(op,std::move(args));
        } return numeric();
    }
    math_tree product(size_t depth) {
        auto left=primary(depth);if(!left)return {};
        for(;;) {
            auto saved=p;if(!trivia())return {};
            if(p==s.size()||(s[p]!='*'&&s[p]!='/')) {p=saved;return left;}
            auto op=s[p++];auto right=primary(depth);if(!right)return {};
            left=operation(op=='*'?math_op::multiply:math_op::divide,{left,right});if(!left)return {};
        }
    }
    math_tree sum(size_t depth) {
        auto left=product(depth);if(!left)return {};
        for(;;) {
            bool before=false,after=false;if(!trivia(&before))return {};
            if(p==s.size()||(s[p]!='+'&&s[p]!='-'))return left;
            auto op=s[p++];if(!trivia(&after)||!before||!after)return {};
            auto right=product(depth);if(!right)return {};
            left=operation(op=='+'?math_op::add:math_op::subtract,{left,right});if(!left)return {};
        }
    }
public:
    explicit parser(std::string_view source):s(source){}
    math_tree parse() {
        if(s.size()>max_source||!trivia()||p==s.size())return {};
        // Top-level CSS value is one numeric token or one math function, not
        // an unwrapped sum, product, or parenthesized block.
        if(s[p]=='(')return {};
        const bool function_value=alpha(s[p])||s[p]=='\\';
        auto t=primary(0);if(!t||!trivia()||p!=s.size())return {};
        if(t->kind==math_kind::number) {
            if(t->value!=0||function_value)return {};
            math_node zero;zero.kind=math_kind::length;zero.unit=math_unit::px;t=make(std::move(zero));
        }
        // Bound actual tree depth too: a long left-associative sum has shallow
        // parser recursion but a deep result tree.
        std::vector<std::pair<math_tree,size_t>> stack{{t,0}};
        while(!stack.empty()) {auto item=stack.back();stack.pop_back();if(item.second>max_depth)return {};for(auto& c:item.first->children)stack.emplace_back(c,item.second+1);}
        return t;
    }
};
inline math_tree compute(const math_tree& t,double font,double root,double vw,double vh,size_t depth,size_t& budget) {
    if(!t||depth>max_depth||!budget--)throw std::overflow_error("CSS math budget exceeded");
    math_node n=*t;
    if(n.op==math_op::literal) {
        double scale=1;
        switch(n.unit) {case math_unit::em:scale=font;break;case math_unit::rem:scale=root;break;case math_unit::vw:scale=vw/100.;break;case math_unit::vh:scale=vh/100.;break;default:break;}
        n.value*=scale;
        if(n.unit!=math_unit::number&&n.unit!=math_unit::percent)n.unit=math_unit::px;
        if(!std::isfinite(n.value)||std::abs(n.value)>std::numeric_limits<float>::max())throw std::overflow_error("Non-finite computed CSS math");
    } else for(auto& child:n.children)child=compute(child,font,root,vw,vh,depth+1,budget);
    auto result=std::make_shared<const math_node>(std::move(n));
    if (!result->percentage) {size_t b=max_nodes;double value;if(!eval(result,0,value,0,b)||std::abs(value)>std::numeric_limits<float>::max())throw std::overflow_error("Non-finite computed CSS math expression");}
    return result;
}
} // namespace contextual_math_detail
inline bool parse_contextual_math(std::string_view source,math_tree& output) {
    auto candidate=contextual_math_detail::parser(source).parse();if(!candidate)return false;output=std::move(candidate);return true;
}
inline math_tree compute_contextual_math(const math_tree& tree,float font,float root,float vw,float vh) {
    size_t budget=contextual_math_detail::max_nodes;
    return contextual_math_detail::compute(tree,font,root,vw,vh,0,budget);
}
// Dependency flags describe the specified tree. Compute removes relative units.
enum math_dependency : uint32_t { math_percent=1, math_self_font=2, math_root_font=4, math_viewport=8 };
inline uint32_t math_dependencies(const math_tree& t) noexcept {
    if(!t)return 0;
    uint32_t flags=t->unit==math_unit::percent?math_percent:t->unit==math_unit::em?math_self_font:t->unit==math_unit::rem?math_root_font:(t->unit==math_unit::vw||t->unit==math_unit::vh)?math_viewport:uint32_t(0);
    for(const auto& c:t->children)flags|=math_dependencies(c);
    return flags;
}
inline bool contextual_math_equal(const math_tree& a,const math_tree& b) noexcept {
    if(a==b)return true;
    if(!a||!b||a->kind!=b->kind||a->op!=b->op||a->unit!=b->unit||a->value!=b->value||a->percentage!=b->percentage||a->children.size()!=b->children.size())return false;
    for(size_t i=0;i<a->children.size();++i)if(!contextual_math_equal(a->children[i],b->children[i]))return false;
    return true;
}
inline bool math_has_percentage(const math_tree& tree) {return tree&&tree->percentage;}
inline bool resolve_contextual_math(const math_tree& tree,double basis,double& output) {
    if(!std::isfinite(basis))return false;
    size_t budget=contextual_math_detail::max_nodes;double candidate;
    if(!contextual_math_detail::eval(tree,basis,candidate,0,budget)||std::abs(candidate)>std::numeric_limits<float>::max())return false;
    output=candidate;return true;
}
} // namespace aether::css
