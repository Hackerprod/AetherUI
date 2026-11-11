// Frozen W/S populations. All validation and mutation are outside resolver timing.
#define main original_page_main
#include "../test_flexbox_page.cpp"
#undef main
#include <iomanip>
static std::string read_file(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("unreadable input");
    return {(std::istreambuf_iterator<char>(f)), {}};
}
int main(int argc, char** argv) {
    if (argc != 6) return 2; // cell html css output expected-subjects
    try {
        const std::string cell=argv[1]; const bool warm=cell=="W1440";
        const int iterations=warm?8:16;
        std::filesystem::create_directories(argv[4]);
        report.open(std::filesystem::path(argv[4])/"report.log");
        report << std::setprecision(17);
        Pipeline p;
        require(aether_doc_create(&p.doc)==AETHER_OK,"create");
        const auto html=read_file(argv[2]);
        require(aether_doc_parse_html(p.doc,html.c_str())==AETHER_OK,"parse");
        const auto root=aether_doc_get_document_element(p.doc);
        std::vector<aether_node_t> all; nodes(root,all);
        std::vector<aether_node_t> targets,sections;
        aether_node_t movable=nullptr;
        std::string css=warm?"":read_file(argv[3]);
        for(auto n:all) {
            if(has_class(n,"target")) targets.push_back(n);
            if(has_class(n,"movable")) movable=n;
            auto tag=take(aether_element_get_tag_name(n));
            if(tag=="section") sections.push_back(n);
            if(warm&&tag=="style") {std::vector<aether_node_t> ns; nodes(n,ns); for(auto x:ns) css+=take(aether_node_get_text_content(x)); css+='\n';}
        }
        require(warm||targets.size()==std::stoul(argv[5]),"fixed subject count");
        aether_style_environment_t env{warm?1440.f:640.f,warm?900.f:480.f,1,16,0,0,0};
        require(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&p.runtime)==AETHER_OK,"runtime");
        int64_t sheet=0; int32_t rules=0;
        if(warm) require(aether_style_runtime_add_stylesheet(p.runtime,aether_style_runtime_defaults_css(),0,"defaults",&sheet,&rules)==AETHER_OK,"UA");
        require(aether_style_runtime_add_stylesheet(p.runtime,css.c_str(),2,"fixed-control",&sheet,&rules)==AETHER_OK,"CSS");
        p.update_count=aether_style_runtime_resolve_subtree(p.runtime,root,&p.updates);
        require(p.update_count>0,"initial resolution");
        aether_style_updates_free(p.updates,p.update_count);p.updates=nullptr;p.update_count=0;
        std::vector<aether_css_length> initial_widths;
        auto inspect=[&](float opacity,bool first,int iteration) {
            require(aether_style_runtime_create_resolved_style_map(p.runtime,p.doc,&p.styles)==AETHER_OK,"map");
            auto get=[&](aether_node_t n) { aether_computed_style_t s{};auto h=aether_style_map_get(p.styles,n);require(h&&aether_computed_style_copy(h,&s)==AETHER_OK,"copy");aether_computed_style_handle_destroy(h);return s; };
            auto r=get(root); if(!first) require(r.opacity==opacity,"changed root opacity"); report<<"opacity="<<iteration<<','<<r.opacity<<'\n'; aether_computed_style_free(&r);
            for(size_t j=0;j<targets.size();++j) {
                auto s=get(targets[j]);
                if(first) initial_widths.push_back(s.width);
                else if(cell=="S13") require(iteration%2 ? s.width.unit==AETHER_CSS_UNIT_AUTO : s.width.unit==AETHER_CSS_UNIT_PX&&s.width.value==17.f,"reparent ID expected width");
                else require(s.width.unit==initial_widths[j].unit&&s.width.value==initial_widths[j].value,"stable target width");
                report<<"width="<<iteration<<','<<j<<','<<s.width.unit<<','<<s.width.value<<'\n';
                aether_computed_style_free(&s);
            }
            aether_style_map_destroy(p.styles);p.styles=nullptr;
        };
        inspect(1,true,-1);
        double sum=0; int observed=0;
        for(int i=0;i<5+iterations;++i) {
            require(aether_element_set_attribute(root,"style",i%2?"opacity:1":"opacity:0.5")==AETHER_OK,"opacity setter");
            if(cell=="S13") {
                require(sections.size()==2&&movable,"mutation fixture");
                auto active=sections[i%2], inactive=sections[1-i%2];
                auto old=aether_node_get_parent(movable);
                if(old!=active) {require(aether_node_remove_child(old,movable)==AETHER_OK,"detach");require(aether_node_append_child(active,movable)==AETHER_OK,"append");}
                require(aether_element_set_attribute(inactive,"id","other")==AETHER_OK,"inactive ID");
                require(aether_element_set_attribute(active,"id",i%2?"unmatched":"hit")==AETHER_OK,"active ID");
            }
            auto start=Clock::now();
            p.update_count=aether_style_runtime_resolve_subtree(p.runtime,root,&p.updates);
            auto end=Clock::now();
            double ms=std::chrono::duration<double,std::milli>(end-start).count();
            require(p.update_count>0&&p.updates,"real nonzero updates");
            report<<"iteration="<<i<<",measured="<<(i>=5)<<",ms="<<ms<<",updates="<<p.update_count<<'\n';
            if(i>=5) {sum+=ms;++observed;}
            inspect(i%2?1.f:.5f,false,i);
            aether_style_updates_free(p.updates,p.update_count);p.updates=nullptr;p.update_count=0;
        }
        report<<"style_mean_ms="<<sum/observed<<"\nmeasured_count="<<observed<<"\nfailures="<<failures<<"\n";
        process_metrics("end");
        return failures?1:0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
