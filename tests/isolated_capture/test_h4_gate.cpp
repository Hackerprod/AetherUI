#include "h4_profile.h"
#include <cassert>
#include <iostream>
static void admitted(const std::string& s,bool sheet,int n){assert(h4::admitted_functions(s,sheet).size()==static_cast<size_t>(n));}
int main(){
 admitted(".x{padding:28px max(24px,calc((100% - 1180px)/2));flex:0 1 clamp(80px,30%,180px)}",true,3);
 admitted("padding-left:m\\61 x(2px,3%);",false,1);
 admitted("padding:max(1px,2px) !IMPORTANT",false,1);
 admitted("padding:max(1px,2px) min(2px,3px)",false,2);
 admitted("padding-top:calc(1px + 2%)",false,1);
 admitted(".x:max(1px,2px){padding:1px}",true,0);
 admitted(".x{width:max(1px,2px)}",true,0);
 admitted("--x:max(1px,2px)",false,0);
 admitted("padding:fit-content(2px)",false,0);
 admitted("padding:max(2px, calc-size(auto, size))",false,0);
 admitted("padding:max(1px,2px) -3px",false,0);
 admitted("padding:max(1px,2px) 1px 1px 1px 1px",false,0);
 admitted("padding:max(1px,2px) junk",false,0);
 admitted("padding:calc(1px+2px)",false,0);
 admitted("padding:calc(1px / 0)",false,0);
 admitted("padding:calc(1px + 2)",false,0);
 admitted("flex:1 clamp(1px,2%,3px)",false,0);
 admitted("flex:0 1 clamp(1px,2%,3px) junk",false,0);
 admitted("flex:0 -1 clamp(1px,2%,3px)",false,0);
 admitted("padding:max(1px,2px)!junk",false,0);
 admitted(".x{padding:max(1px,2px)",true,0);
 admitted("padding:max(1px,2px)}",false,0);
 admitted("padding:max(1px,2px);bogus",false,0);
 admitted(".x{padding:max(1px,2px)} .y{width:max(1px,2px)}",true,1);
 std::cout<<"h4_gate:24 checks PASS\n";
}
