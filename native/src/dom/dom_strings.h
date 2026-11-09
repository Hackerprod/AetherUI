#pragma once
#include <string_view>
#include <cstdint>
inline bool aether_dom_valid_string(std::string_view s) noexcept {
    for(size_t i=0;i<s.size();){auto c=static_cast<unsigned char>(s[i++]);if(!c)return false;if(c<0x80)continue;
        unsigned n=0;uint32_t cp=0,min=0;
        if(c>=0xc2&&c<=0xdf){n=1;cp=c&31;min=0x80;}else if(c>=0xe0&&c<=0xef){n=2;cp=c&15;min=0x800;}else if(c>=0xf0&&c<=0xf4){n=3;cp=c&7;min=0x10000;}else return false;
        if(s.size()-i<n)return false;while(n--){c=static_cast<unsigned char>(s[i++]);if((c&0xc0)!=0x80)return false;cp=(cp<<6)|(c&63);}if(cp<min||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))return false;
    }return true;
}
