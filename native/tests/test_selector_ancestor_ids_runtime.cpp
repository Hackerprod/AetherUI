// Public shared-core test: ordinary ID changes/reparenting only between complete
// resolutions. No mutation inside diagnostic callbacks and no generated content.
#include <aether/aether.h>
#include <cstdio>
#include <map>
#include <string>
#ifdef AETHER_ANCESTOR_COUNTED
#include "ancestor_id_counted/counters.h"
#endif

namespace {
size_t checks=0,failures=0,diagnostics=0;
void check(bool ok,const char* msg){++checks;if(!ok){++failures;std::fprintf(stderr,"FAIL %s\n",msg);}}
void diagnostic(const char*,const char*,const char*,const char*,void*){++diagnostics;}
struct Fixture {
    aether_doc_t doc=nullptr;
    aether_style_runtime_t runtime=nullptr;
    aether_node_t target=nullptr,left=nullptr,right=nullptr,root=nullptr;
    std::map<aether_node_t,float> widths;
    Fixture(bool callback){
        check(aether_doc_create(&doc)==AETHER_OK,"create document");
        check(aether_doc_parse_html(doc,"<body><section id=left><span id=target class=target></span></section><section id=right></section></body>")==AETHER_OK,"parse ordinary document");
        target=aether_doc_get_element_by_id(doc,"target");left=aether_doc_get_element_by_id(doc,"left");right=aether_doc_get_element_by_id(doc,"right");root=aether_doc_get_document_element(doc);
        check(target&&left&&right&&root,"fixture nodes exist");
        aether_style_environment_t env{640,480,1,16,0,0,0};
        check(aether_style_runtime_create(nullptr,&env,callback?diagnostic:nullptr,nullptr,&runtime)==AETHER_OK,"create runtime");
        int64_t sheet=0;int32_t count=0;
        check(aether_style_runtime_add_stylesheet(runtime,".target{width:1px}#missing .target{width:99px}#left .target{width:7px}#right .target{width:11px}",2,"ancestor-ID-test",&sheet,&count)==AETHER_OK&&count==4,"four ordinary rules");
    }
    ~Fixture(){if(runtime)aether_style_runtime_destroy(runtime);if(doc)aether_doc_destroy(doc);}
    int resolve(float expected){
        aether_style_update_t* updates=nullptr;int count=aether_style_runtime_resolve_subtree(runtime,root,&updates);check(count>=0,"resolve status");
        for(int i=0;i<count;++i){aether_node_t node=nullptr;aether_computed_style_handle_t h=nullptr;
            check(aether_style_runtime_get_update(updates[i],&node,&h,nullptr)==AETHER_OK&&node&&h,"read update");
            aether_computed_style_t s{};
            check(aether_computed_style_copy(h,&s)==AETHER_OK,"copy style");
            if(node==target){check(s.width.unit==AETHER_CSS_UNIT_PX,"target width unit");widths[node]=s.width.value;}
            aether_computed_style_free(&s);aether_computed_style_handle_destroy(h);
        }
        if(updates)aether_style_updates_free(updates,count);
        check(widths.count(target)&&widths[target]==expected,"expected current target width");return count;
    }
    void id(aether_node_t n,const char* value){check(aether_element_set_attribute(n,"id",value)==AETHER_OK,"ordinary ID mutation");}
};
void run(bool callback){
#ifdef AETHER_ANCESTOR_COUNTED
    ancestor_counters={};
#endif
    Fixture f(callback);check(f.resolve(7)>0,"initial real work");check(f.resolve(7)==0,"unchanged resolve");
    f.id(f.left,"absent");check(f.resolve(1)>0,"ID removal emits real change");
    f.id(f.left,"left");check(f.resolve(7)>0,"ID restore emits real change");
    check(aether_node_remove_child(f.left,f.target)==AETHER_OK,"remove ordinary child");
    check(aether_node_append_child(f.right,f.target)==AETHER_OK,"append ordinary child");
    check(f.resolve(11)>0,"reparent emits changed target");
    f.id(f.right,"absent");check(f.resolve(1)>0,"reparented ID removal");
    f.id(f.right,"right");check(f.resolve(11)>0,"reparented ID restoration");
    check(aether_node_remove_child(f.right,f.target)==AETHER_OK,"detach to restore");
    check(aether_node_append_child(f.left,f.target)==AETHER_OK,"restore original parent");
    check(f.resolve(7)>0,"original parent fresh preparation");
    for(int i=0;i<4;++i){check(aether_element_set_attribute(f.root,"style",i%2?"opacity:1":"opacity:.5")==AETHER_OK,"ordinary root opacity mutation");check(f.resolve(7)>0,"repeated real resolution");}
#ifdef AETHER_ANCESTOR_COUNTED
    if(callback)check(ancestor_counters.disabled_entries>0&&!ancestor_counters.enabled_entries&&!ancestor_counters.admitted&&!ancestor_counters.collections,"original nonnull callback disables filter for every preparation");
    else check(ancestor_counters.enabled_entries>0&&!ancestor_counters.disabled_entries&&ancestor_counters.admitted>0&&ancestor_counters.collections>0,"null callback exercises enabled production route");
#endif
}
}
int main(){run(false);run(true);std::printf("ancestor IDs runtime: %zu checks, %zu failures; diagnostics=%zu\n",checks,failures,diagnostics);return failures?1:0;}
