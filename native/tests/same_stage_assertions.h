#pragma once
// Allocation-free, explicit mutation protocol. Output alone is not provenance:
// pair with trusted supervisor/build receipts and source/binary hashes.
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace ss {
inline const char* active=nullptr;
inline unsigned completed=0;
inline constexpr int assertion_exit=86, precondition_exit=87, protocol_exit=88;
inline bool id_ok(const char* id){if(!id||!*id)return false;for(auto p=id;*p;++p)if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='-'||*p=='_'||*p=='.'||*p=='/'))return false;return true;}
[[noreturn]] inline void protocol(){std::fputs("AETHER_ASSERT_V1\tPROTOCOL_ERROR\n",stdout);std::fflush(stdout);std::_Exit(protocol_exit);}
inline bool selected(const char* id){if(!id_ok(id))protocol();auto requested=std::getenv("AETHER_ASSERT_CASE");return !requested||!*requested||std::strcmp(requested,id)==0;}
inline void emit(const char* event,const char* assertion,const char* result){std::fprintf(stdout,"AETHER_ASSERT_V1\t%s\t%s\t%s\t%s\n",event,active?active:"none",assertion,result);std::fflush(stdout);}
struct Case {
 explicit Case(const char* id){if(active||!id_ok(id))protocol();active=id;emit("BEGIN","none","none");}
 ~Case(){if(!active)protocol();emit("END","none","PASS");active=nullptr;++completed;}
 Case(const Case&)=delete;Case& operator=(const Case&)=delete;
};
inline void check(const char* id,bool condition,bool precondition=false){
 if(!active||!id_ok(id))protocol();emit(precondition?"PRECONDITION":"ASSERT",id,condition?"PASS":"FAIL");
 if(!condition)std::_Exit(precondition?precondition_exit:assertion_exit);
}
template<class F> inline void run(const char* id,F fn){if(selected(id)){Case scope(id);fn();}}
inline int finish(){if(active||!completed)protocol();std::fprintf(stdout,"AETHER_ASSERT_V1\tSUITE_END\tnone\tnone\t%u\n",completed);std::fflush(stdout);return 0;}
}
#define SS_CHECK(id, ...) ::ss::check(id, bool((__VA_ARGS__)), false)
#define SS_PRECONDITION(id, ...) ::ss::check(id, bool((__VA_ARGS__)), true)
#define SS_STRING_I(x) #x
#define SS_STRING(x) SS_STRING_I(x)
#define SS_REQUIRE(...) SS_PRECONDITION("line-" SS_STRING(__LINE__), __VA_ARGS__)
