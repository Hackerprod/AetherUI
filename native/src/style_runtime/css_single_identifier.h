#pragma once
#include "css_component_tokens.h"
#include <string>
// Decode exactly one identifier, never reinterpret decoded punctuation as CSS.
inline bool css_single_identifier(const std::string& source, std::string& output) {
    css_component_lexer lexer(source.data(), source.size());
    auto token=lexer.next();
    while(token.kind==css_variable_token_kind::whitespace||token.kind==css_variable_token_kind::comment)token=lexer.next();
    if(token.kind!=css_variable_token_kind::ident)return false;
    const auto ident=token;
    token=lexer.next();
    while(token.kind==css_variable_token_kind::whitespace||token.kind==css_variable_token_kind::comment)token=lexer.next();
    if(token.kind!=css_variable_token_kind::end)return false;
    std::string staged;
    css_variable_lexer decoder(source.data()+ident.begin,ident.end-ident.begin);
    if(!decoder.decode_identifier([](void* p,const char* b,size_t n){auto& s=*static_cast<std::string*>(p);for(size_t i=0;i<n;++i){char c=b[i];s+=c>='A'&&c<='Z'?char(c+32):c;}return true;},&staged))return false;
    output=std::move(staged);return true;
}
