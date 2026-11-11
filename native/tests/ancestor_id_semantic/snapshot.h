#pragma once
#include <bit>
#include <type_traits>
#include <iomanip>
#include <cstdint>
#include <fstream>
#include <string>
#include <stdexcept>
#include <vector>

// Records contain named semantic fields, never raw struct bytes or addresses.
struct Writer { std::ostream& out; };
template<class T> requires (std::is_integral_v<T> || std::is_enum_v<T>)
inline void value(Writer& w,const std::string& p,T v) {
    w.out << p << "=i:" << static_cast<int64_t>(v) << '\n';
}
inline void value(Writer& w,const std::string& p,float v) {
    // Exact scalar representation, including signed zero; no tolerance masks changes.
    w.out << p << "=f:" << std::bit_cast<uint32_t>(v) << '\n';
}
inline void string_bytes(Writer& w,const std::string& p,const char* v,size_t n) {
    static constexpr char hex[]="0123456789abcdef";
    w.out << p << "=s:";
    for(size_t i=0;i<n;++i) { auto c=static_cast<unsigned char>(v[i]); w.out.put(hex[c>>4]); w.out.put(hex[c&15]); }
    w.out.put('\n');
}
inline void value(Writer& w,const std::string& p,char* v) {
    if(!v) w.out << p << "=null\n";
    else string_bytes(w,p,v,std::strlen(v));
}
template<size_t N> inline void value(Writer& w,const std::string& p,const char (&v)[N]) {
    size_t n=0; while(n<N && v[n]) ++n;
    if(n==N) throw std::runtime_error("unterminated fixed public string");
    string_bytes(w,p,v,n); // Unused bytes after NUL are not semantic string data.
}
template<class T> inline void sequence(Writer& w,const std::string& p,const T* v,int32_t n) {
    if(n<0 || (n && !v)) throw std::runtime_error("invalid public array");
    value(w,p+".length",n);
    for(int32_t i=0;i<n;++i) value(w,p+"["+std::to_string(i)+"]",v[i]);
}
#include "fields.generated.h"
inline void semantic_snapshot(aether_style_map_t styles,aether_display_list_t list,
        const std::vector<aether_node_t>& all,aether_style_update_t* updates,int32_t update_count,
        const std::filesystem::path& path) {
    std::ofstream file(path,std::ios::binary);
    if(!file) throw std::runtime_error("cannot open semantic snapshot");
    Writer w{file}; value(w,"dom.count",all.size());
    for(size_t i=0;i<all.size();++i) {
        const std::string prefix="dom["+std::to_string(i)+"]";
        // Stable public identity is also retained verbatim in display commands.
        value(w,prefix+".style_node_id",aether_node_get_style_node_id(all[i]));
        auto handle=aether_style_map_get(styles,all[i]);
        value(w,prefix+".has_style",bool(handle));
        if(!handle) continue;
        aether_computed_style_t style{};
        auto result=aether_computed_style_copy(handle,&style);
        aether_computed_style_handle_destroy(handle);
        if(result!=AETHER_OK) throw std::runtime_error("cannot copy full public style");
        try { value(w,prefix+".style",style); }
        catch(...) { aether_computed_style_free(&style); throw; }
        aether_computed_style_free(&style);
    }
    // Include every public update as well as the original DOM map. This also
    // covers generated styles returned by this unchanged page's initial resolve.
    value(w,"updates.count",update_count);
    for(int32_t i=0;i<update_count;++i) {
        aether_node_t node{}; aether_computed_style_handle_t handle{}; uint32_t impact{};
        if(aether_style_runtime_get_update(updates[i],&node,&handle,&impact)!=AETHER_OK || !handle)
            throw std::runtime_error("cannot read public update");
        const auto prefix="updates["+std::to_string(i)+"]";
        value(w,prefix+".style_node_id",aether_node_get_style_node_id(node));
        value(w,prefix+".impact",impact);
        aether_computed_style_t style{};
        auto result=aether_computed_style_copy(handle,&style);
        aether_computed_style_handle_destroy(handle);
        if(result!=AETHER_OK) throw std::runtime_error("cannot copy update style");
        try { value(w,prefix+".style",style); }
        catch(...) { aether_computed_style_free(&style); throw; }
        aether_computed_style_free(&style);
    }
    int32_t count=aether_display_list_get_count(list); value(w,"display.count",count);
    for(int32_t i=0;i<count;++i) {
        aether_paint_command_t cmd{};
        if(aether_display_list_get_command(list,i,&cmd)!=AETHER_OK) throw std::runtime_error("cannot copy command");
        try { value(w,"display["+std::to_string(i)+"]",cmd); }
        catch(...) { aether_paint_command_free(&cmd); throw; }
        aether_paint_command_free(&cmd);
    }
    file.flush(); if(!file) throw std::runtime_error("semantic snapshot write failed");
}
