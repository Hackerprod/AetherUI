#include "admission.h"
#include "../../style_runtime/css_variable_tokens.h"
#include "../../css/linear_gradient.h"
#include <algorithm>
#include <cctype>
#include <limits>
#include <string>

namespace aether::rendering::html_ltr {
std::uint32_t decode_scalar_utf8(std::string_view s, std::size_t& i) {
    if(i>=s.size()) throw Error{AETHER_ERROR_INVALID_ARGUMENT};
    unsigned c=static_cast<unsigned char>(s[i++]);
    if(c<0x80) { if(c==0) throw Error{AETHER_ERROR_INVALID_ARGUMENT}; return c; }
    unsigned count=0;std::uint32_t value=0,minimum=0;
    if(c>=0xc2 && c<=0xdf) {count=1;value=c&31;minimum=0x80;}
    else if(c>=0xe0 && c<=0xef) {count=2;value=c&15;minimum=0x800;}
    else if(c>=0xf0 && c<=0xf4) {count=3;value=c&7;minimum=0x10000;}
    else throw Error{AETHER_ERROR_INVALID_ARGUMENT};
    if(count>s.size()-i) throw Error{AETHER_ERROR_INVALID_ARGUMENT};
    for(unsigned n=0;n<count;++n) {unsigned next=static_cast<unsigned char>(s[i++]);if((next&0xc0)!=0x80)throw Error{AETHER_ERROR_INVALID_ARGUMENT};value=(value<<6)|(next&63);}
    if(value<minimum || value>0x10ffff || (value>=0xd800&&value<=0xdfff)) throw Error{AETHER_ERROR_INVALID_ARGUMENT};
    return value;
}
namespace {
[[noreturn]] void invalid() { throw Error{AETHER_ERROR_INVALID_ARGUMENT}; }
[[noreturn]] void unsupported() { throw Error{AETHER_ERROR_UNSUPPORTED}; }
[[noreturn]] void limit() { throw Error{AETHER_ERROR_LIMIT_EXCEEDED}; }
bool ws(char c) noexcept { return c==' ' || c=='\t' || c=='\r' || c=='\n'; }
bool css_ws(char c) noexcept { return ws(c) || c=='\f'; }
bool alpha(char c) noexcept { return (c>='a'&&c<='z') || (c>='A'&&c<='Z'); }
bool name_char(char c) noexcept { return alpha(c) || (c>='0'&&c<='9') || c=='-' || c==':' || c=='_'; }

std::string_view trim(std::string_view s) {while(!s.empty()&&css_ws(s.front()))s.remove_prefix(1);while(!s.empty()&&css_ws(s.back()))s.remove_suffix(1);return s;}
unsigned hex(char c) {if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;unsupported();}
std::uint32_t color(std::string_view v) {if(v.size()!=7||v.front()!='#')unsupported();std::uint32_t c=0;for(std::size_t i=1;i<v.size();++i)c=(c<<4)|hex(v[i]);return c;}
unsigned pixels(std::string_view v,unsigned lo,unsigned hi) {
    if(v.size()<3||v.substr(v.size()-2)!="px")unsupported();
    v.remove_suffix(2);
    unsigned number=0;
    for(char c:v) {if(c<'0'||c>'9')unsupported();unsigned digit=c-'0';if(number>(std::numeric_limits<unsigned>::max()-digit)/10)limit();number=number*10+digit;}
    if(number<lo||number>hi)limit();
    return number;
}
// Espejo de pixels() para porcentajes: dígitos + '%'. Fuera de rango llama
// a limit() (LIMIT_EXCEEDED), igual que pixels().
unsigned percent(std::string_view v,unsigned lo,unsigned hi) {
    if(v.size()<2||v.back()!='%')unsupported();
    v.remove_suffix(1);
    unsigned number=0;
    for(char c:v) {if(c<'0'||c>'9')unsupported();unsigned digit=c-'0';if(number>(std::numeric_limits<unsigned>::max()-digit)/10)limit();number=number*10+digit;}
    if(number<lo||number>hi)limit();
    return number;
}
// Divide un valor en palabras separadas por espacios CSS. Falso si está
// vacío o tiene más de max palabras (el compilador genérico acepta 1-2).
bool split_words(std::string_view v,std::string_view* words,std::size_t max,std::size_t& count) {
    count=0;std::size_t i=0;
    while(i<v.size()) {
        while(i<v.size()&&css_ws(v[i]))++i;
        if(i==v.size())break;
        if(count==max)return false;
        const std::size_t start=i;
        while(i<v.size()&&!css_ws(v[i]))++i;
        words[count++]=v.substr(start,i-start);
    }
    return count>0;
}
bool inherited_property(std::string_view p) {return p=="font-family"||p=="font-size"||p=="font-weight"||p=="line-height"||p=="color";}
// Ciclo 03 (Cambio 1): ¿empieza el valor por "linear-gradient("?
// Comparación insensible a mayúsculas; el parser decide el resto.
bool starts_gradient(std::string_view v) {
    constexpr std::string_view prefix="linear-gradient(";
    if(v.size()<prefix.size())return false;
    for(std::size_t i=0;i<prefix.size();++i)
        if(char(std::tolower(static_cast<unsigned char>(v[i])))!=prefix[i])return false;
    return true;
}
void css(Node& node,std::string_view input) {
    bool gradient_layer=false; // Ciclo 03 (Cambio 3): background-image es un gradiente.
    std::size_t i=0;
    while(i<input.size()) {
        while(i<input.size()&&css_ws(input[i]))++i;
        if(i==input.size())break;
        const std::size_t start=i;
        while(i<input.size()&&input[i]!=':') {
            const unsigned c=static_cast<unsigned char>(input[i]);
            if(c>=128||input[i]=='\\'||input[i]=='/'||input[i]=='@'||input[i]=='!'||input[i]=='('||input[i]==')')unsupported();
            if(input[i]==';')invalid();
            ++i;
        }
        if(i==input.size())invalid();
        auto prop=trim(input.substr(start,i-start));++i;
        if(prop.empty())invalid();
        for(char c:prop)if(!((c>='a'&&c<='z')||c=='-'))unsupported();
        if(prop.size()>1&&prop[0]=='-'&&prop[1]=='-')unsupported();
        const std::size_t vs=i;
        while(i<input.size()&&input[i]!=';') {
            const unsigned c=static_cast<unsigned char>(input[i]);
            if(c>=128||input[i]=='\\'||input[i]=='/'||input[i]=='@'||input[i]=='!'||(prop!="background-image"&&(input[i]=='('||input[i]==')')))unsupported();
            ++i;
        }
        auto value=trim(input.substr(vs,i-vs));if(value.empty())invalid();if(i<input.size())++i;
        if(std::any_of(node.declarations.begin(),node.declarations.end(),[&](const auto& d){return d.first==prop;}))unsupported();
        if(node.tag=="html"&&!inherited_property(prop))unsupported();
        if(prop=="color")node.inherited.color_rgb=color(value);
        else if(prop=="background-color") {if(value!="transparent") (void)color(value);}
        else if(prop=="background-image") {
            // Ciclo 03 (Cambio 1): linear-gradient(...) g1. El predicado es
            // la misma función que fija la evidencia del builder, así que
            // admisión y builder no pueden divergir en qué formas son g1.
            // Tras el Cambio 5 solo admite posiciones en % o sin posición.
            if(value!="none"&&starts_gradient(value)) {
                std::string source(value);
                if(!aether_linear_gradient_g1_source_supported(source.c_str()))unsupported();
                gradient_layer=true;
            } else if(value!="none") {
            // background-image: un solo url(...). El url('...') entrecomillado
            // lexa como varios tokens (función + string + cierre), así que el
            // bucle genérico de un token lo rechazaría; decode_url acepta ambas
            // formas y exige que todo el valor sea la url (url(a), url(b)
            // falla → unsupported). Sin data-URLs (espejo del gate B0).
                css_variable_lexer lexer(value.data(),value.size());
                std::string source;
                if(!lexer.decode_url(source))unsupported();
                if(source.empty())unsupported();
                if(source.size()>=5) {
                    std::string head=source.substr(0,5);
                    for(auto& c:head)c=char(std::tolower(static_cast<unsigned char>(c)));
                    if(head=="data:")unsupported();
                }
            }
        }
        else if(prop=="background-repeat") {
            // El compilador genérico acepta 1-2 palabras (parse_repeat).
            std::string_view w[2];std::size_t n=0;
            if(!split_words(value,w,2,n))unsupported();
            for(std::size_t k=0;k<n;++k) {auto word=w[k];if(word!="repeat"&&word!="no-repeat"&&word!="repeat-x"&&word!="repeat-y"&&word!="space"&&word!="round")unsupported();}
        }
        else if(prop=="background-position") {
            // Una o dos palabras, cada una keyword o longitud Npx (0..8192)
            // o porcentaje N% (0..100), espejo del gate B0 (:388).
            std::string_view w[2];std::size_t n=0;
            if(!split_words(value,w,2,n))unsupported();
            for(std::size_t k=0;k<n;++k) {
                auto word=w[k];
                if(word=="left"||word=="center"||word=="right"||word=="top"||word=="bottom")continue;
                if(word.size()>=2&&word.back()=='%') {(void)percent(word,0,100);continue;}
                (void)pixels(word,0,8192);
            }
        }
        else if(prop=="background-size") {
            // auto|cover|contain, o una o dos longitudes Npx (0..8192) / N%
            // (0..8192), espejo del gate B0 (:389).
            if(value!="auto"&&value!="cover"&&value!="contain") {
                std::string_view w[2];std::size_t n=0;
                if(!split_words(value,w,2,n))unsupported();
                for(std::size_t k=0;k<n;++k) {
                    auto word=w[k];
                    if(word=="auto")continue;
                    if(word.size()>=2&&word.back()=='%') {(void)percent(word,0,8192);continue;}
                    (void)pixels(word,0,8192);
                }
            }
        }
        else if(prop=="border-color") (void)color(value);
        else if(prop=="font-family") {if(value!="AetherLTR")unsupported();}
        else if(prop=="font-size")node.inherited.font_size=pixels(value,8,48);
        else if(prop=="line-height")node.inherited.line_height=pixels(value,8,96);
        else if(prop=="font-weight") {if(value!="400"&&value!="700")unsupported();node.inherited.font_weight=value=="400"?400:700;}
        else if(prop=="width") {if(value!="auto") (void)pixels(value,1,2048);}
        else if(prop=="border-style") {if(value!="none"&&value!="solid")unsupported();}
        else if(prop=="margin-top"||prop=="margin-right"||prop=="margin-bottom"||prop=="margin-left"||prop=="padding-top"||prop=="padding-right"||prop=="padding-bottom"||prop=="padding-left"||prop=="border-width") (void)pixels(value,0,128);
        else unsupported();
        node.declarations.emplace_back(prop,value);
    }
    // Ciclo 03 (Cambio 3): con gradiente, background-size solo admite
    // auto/cover/contain (o una lista de palabras todas auto, idéntica a
    // auto). Con longitudes explícitas la baldosa puede ser
    // arbitrariamente pequeña y el desbordamiento afloraría como INTERNAL;
    // aquí es un rechazo limpio. El post-bucle cubre ambos órdenes de
    // declaración (size antes o después de image).
    if(gradient_layer) {
        for(const auto& d:node.declarations) {
            if(d.first!="background-size")continue;
            const auto& v=d.second;
            if(v=="auto"||v=="cover"||v=="contain")continue;
            std::string_view w[2];std::size_t n=0;
            bool all_auto=false;
            if(split_words(v,w,2,n)) {
                all_auto=true;
                for(std::size_t k=0;k<n;++k)if(w[k]!="auto")all_auto=false;
            }
            if(!all_auto)unsupported();
        }
    }
    if(node.inherited.line_height<node.inherited.font_size)unsupported();
}
class Recognizer {
    std::string_view source;std::size_t at=0;Inventory result;std::uint32_t profile;
    bool starts(std::string_view v) const {return source.substr(at,v.size())==v;}
    void expect(char c) {if(at>=source.size()||source[at]!=c)invalid();++at;}
    std::string_view name() {const auto begin=at;if(at>=source.size()||!alpha(source[at]))invalid();while(at<source.size()&&name_char(source[at]))++at;return source.substr(begin,at-begin);}
    void text(Node& n,std::string_view s) {
        bool previous=false;
        for(std::size_t i=0;i<s.size();) {
            const auto cp=decode_scalar_utf8(s,i);
            if(cp=='&')unsupported();
            if(!repertoire_scalar(cp))unsupported();
            if(cp==' '&&(previous||i==1||i==s.size()))unsupported();
            previous=cp==' ';
            if(result.scalar_count==4096)limit();
            ++result.scalar_count;
        }
        n.text.assign(s);
    }
    void attribute_text(std::string& destination,std::string_view value) {
        std::size_t count=0;
        for(std::size_t i=0;i<value.size();) {
            const auto cp=decode_scalar_utf8(value,i);
            if(cp=='&'||cp=='<'||!repertoire_scalar(cp))unsupported();
            if(count==256||result.scalar_count==4096)limit();
            ++count;++result.scalar_count;
        }
        destination.assign(value);
    }
    void control_attributes(Node& node) {
        bool type=false,value=false,placeholder=false,style=false;
        for(;;) {
            const auto separator=at;
            while(at<source.size()&&source[at]==' ')++at;
            if(at==source.size())invalid();
            if(source[at]=='>') {++at;break;}
            if(at==separator||source[at]=='/'||ws(source[at]))invalid();
            const auto attr=name();expect('=');expect('"');const auto begin=at;
            while(at<source.size()&&source[at]!='"')++at;
            if(at==source.size())invalid();
            const auto val=source.substr(begin,at-begin);++at;
            bool* seen=nullptr;
            if(attr=="type")seen=&type;
            else if(attr=="style")seen=&style;
            else if(node.input()&&attr=="value")seen=&value;
            else if(node.input()&&attr=="placeholder")seen=&placeholder;
            else unsupported();
            if(*seen)invalid();
            *seen=true;
            if(attr=="type") {
                if(node.input() ? (val!="text"&&val!="password") : val!="button")unsupported();
                node.type.assign(val);
            } else if(attr=="style")css(node,val);
            else if(attr=="value")attribute_text(node.value,val);
            else attribute_text(node.placeholder,val);
        }
        if(!type)invalid();
        if(result.control_count==32)limit();
        ++result.control_count;
    }
    void opening(Node& node) {
        expect('<');const auto tag=name();node.tag.assign(tag);
        if(tag!="html"&&tag!="body"&&tag!="div"&&tag!="h1"&&tag!="p"&&!(profile==2&&(tag=="input"||tag=="button")))unsupported();
        if(node.control()){control_attributes(node);return;}
        const auto after_name=at;
        while(at<source.size()&&source[at]==' ')++at;
        if(at>=source.size())invalid();
        if(source[at]=='>'){++at;return;}
        if(source[at]=='/')unsupported();
        if(at==after_name||at-after_name!=1)unsupported();
        const auto attribute=name();if(attribute!="style")unsupported();
        expect('=');expect('"');const auto begin=at;
        while(at<source.size()&&source[at]!='"')++at;
        if(at==source.size())invalid();
        const auto value=source.substr(begin,at-begin);++at;
        while(at<source.size()&&source[at]==' ')++at;
        if(at>=source.size())invalid();
        if(source[at]!='>')unsupported();
        ++at;
        css(node,value);
    }
    void closing(const std::string& tag) {
        expect('<');expect('/');const auto close=name();
        if(close!=tag)invalid();
        while(at<source.size()&&source[at]==' ')++at;
        expect('>');
    }
    std::size_t element(std::size_t parent,unsigned depth) {
        if(depth>16||result.nodes.size()==256)limit();
        if(starts("<!--")||starts("<!")||starts("<?"))unsupported();
        Node node;node.parent=parent;if(parent!=Node::no_parent)node.inherited=result.nodes[parent].inherited;
        opening(node);
        if(parent==Node::no_parent) {if(node.tag!="html")unsupported();}
        else {const auto& p=result.nodes[parent].tag;if(p=="html"){if(node.tag!="body"||!result.nodes[parent].children.empty())unsupported();}else if(node.tag!="div"&&node.tag!="h1"&&node.tag!="p"&&!(profile==2&&node.control()))unsupported();}
        if(node.inherited.line_height<node.inherited.font_size)unsupported();
        const auto index=result.nodes.size();result.nodes.push_back(std::move(node));
        if(parent!=Node::no_parent)result.nodes[parent].children.push_back(index);
        if(result.nodes[index].input())return index;
        if(result.nodes[index].leaf()) {
            if(result.leaf_count==64)limit();
            ++result.leaf_count;
            const auto begin=at;while(at<source.size()&&source[at]!='<')++at;
            text(result.nodes[index],source.substr(begin,at-begin));
            if(!starts("</")) {if(at==source.size())invalid();unsupported();}
            if(result.nodes[index].tag=="button"&&result.nodes[index].text.empty())invalid();
        } else {
            for(;;) {
                const auto begin=at;while(at<source.size()&&ws(source[at]))++at;
                if(at!=begin)result.nodes[index].trivia.push_back({result.nodes[index].children.size(),std::string(source.substr(begin,at-begin))});
                if(starts("</"))break;
                if(at>=source.size())invalid();
                if(source[at]!='<')unsupported();
                element(index,depth+1);
            }
            if(result.nodes[index].tag=="html"&&result.nodes[index].children.size()!=1)invalid();
        }
        closing(result.nodes[index].tag);return index;
    }
public:
    explicit Recognizer(std::string_view s,std::uint32_t p):source(s),profile(p){}
    Inventory run() {
        if(source.empty())invalid();
        if(source.size()>65536)limit();
        // Validate complete bytes, including rejected markup and style values.
        for(std::size_t i=0;i<source.size();) (void)decode_scalar_utf8(source,i);
        if(starts("<!doctype html>"))at+=15;
        element(Node::no_parent,1);
        if(at!=source.size())invalid();
        return std::move(result);
    }
};
}
Inventory admit(std::string_view html,std::uint32_t profile) {if(profile!=1&&profile!=2)invalid();return Recognizer(html,profile).run();}
}
