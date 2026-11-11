#include "aether_visual_compare/resource_manifest.hpp"
#include "test_support.hpp"
#include <string>
namespace {
std::string entry(std::string path="a.css",std::string size="0") {
 return "{\"path\":\""+path+"\",\"mime\":\"text/css\",\"size_bytes\":"+size+",\"sha256\":\""+std::string(64,'0')+"\",\"role\":\"stylesheet\"}";
}
std::string manifest(const std::string& e) {return "{\"schema_version\":\"aether.resources/1\",\"entries\":["+e+"]}";}
}
int main() {
 using namespace avc;
 CHECK(parse_manifest(manifest("")));
 CHECK(parse_manifest(manifest(entry())));
 CHECK(parse_manifest(manifest(entry("dir/a.css","16777216"))));
 for(const auto* n:{"-1","-0","0.0","1e0","16777217","18446744073709551616","01","+1"})CHECK(!parse_manifest(manifest(entry("a.css",n))));
 for(const auto* p:{"", ".", "..", "a/../b", "/a", "a/", "a//b", "a.", "a./b", "a b", "C:/a", "//server/a", "a%20b", "CON", "con.txt", "LPT9.css", "com1.foo"})CHECK(!valid_resource_path(p));
 CHECK(!valid_resource_path("a\\b"));CHECK(valid_resource_path("_a-b.0/x.css"));
 CHECK(!parse_manifest(manifest(entry()+","+entry())));
 CHECK(!parse_manifest("{\"schema_version\":\"aether.resources/2\",\"entries\":[]}"));
 CHECK(!parse_manifest("{\"schema_version\":\"aether.resources/1\",\"entries\":[],\"other\":true}"));
 auto unknown=entry();unknown.insert(1,"\"unknown\":null,");CHECK(!parse_manifest(manifest(unknown)));
 auto badmime=entry();badmime.replace(badmime.find("text/css"),8,"text/plain");CHECK(!parse_manifest(manifest(badmime)));
 auto badrole=entry();badrole.replace(badrole.find("stylesheet"),10,"script");CHECK(!parse_manifest(manifest(badrole)));
 CHECK(!parse_manifest(manifest(entry())+" false"));CHECK(!parse_json("/* comment */{}"));
 CHECK(!parse_json("{\"x\":1,\"x\":2}"));CHECK(!parse_json("{\"x\":1,\"\\u0078\":2}"));
 CHECK(!parse_json("[true,]"));CHECK(!parse_json("{\"a\":false,}"));CHECK(!parse_json("[.1]"));
 CHECK(parse_json("[null,true,false,-1,0.5,1e+2]"));
 auto escapes=parse_json("\"\\\"\\\\\\/\\b\\f\\n\\r\\t\\u0041\\ud83d\\ude00\"");CHECK(escapes && valid_utf8(escapes->scalar));
 for(const auto* s:{"\"\\ud800\"","\"\\udc00\"","\"\\ud800\\u0041\"","\"\\u000x\"","\"\\x\""})CHECK(!parse_json(s));
 CHECK(!parse_json(std::string("\"")+char(1)+"\""));
 CHECK(!valid_utf8(std::string("\xc0\xaf",2)));CHECK(!valid_utf8(std::string("\xed\xa0\x80",3)));CHECK(!valid_utf8(std::string("\xf4\x90\x80\x80",4)));CHECK(!valid_utf8(std::string("\xe2\x82",2)));
 CHECK(valid_utf8(std::string("\xf0\x9f\x98\x80",4)));
 CHECK(parse_json("\""+std::string(4096,'a')+"\""));CHECK(!parse_json("\""+std::string(4097,'a')+"\""));
 CHECK(!parse_json(std::string(1024*1024+1,' ')));
 CHECK(parse_json(std::string(7,'[')+"0"+std::string(7,']')));CHECK(!parse_json(std::string(8,'[')+"0"+std::string(8,']')));
 std::string entries;for(unsigned i=0;i<1024;++i){if(i)entries+=',';entries+=entry("r"+std::to_string(i));}CHECK(parse_manifest(manifest(entries)));entries+=","+entry("r1024");CHECK(!parse_manifest(manifest(entries)));
 auto escaped_path=entry("\\u0061.css");CHECK(parse_manifest(manifest(escaped_path)));
 auto hash=entry();hash[hash.find(std::string(64,'0'))]='A';CHECK(!parse_manifest(manifest(hash)));
}
