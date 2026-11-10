#include <aether/aether.h>

#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>

static int failures = 0;
static int checks = 0;


static void require(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

// Collect geometric failures so the base implementation exercises both axes and
// every relayout, rather than stopping at the first incorrect shrink result.
static void expect(float actual, float expected, const char* scenario, const char* field) {
    ++checks;
    if (!std::isfinite(actual) || std::fabs(actual - expected) > 0.001f) {
        std::fprintf(stderr, "FAIL: %s: %s = %g, expected %g (finite)\n",
                     scenario, field, actual, expected);
        ++failures;
    }
}

static aether_text_metrics_t measure_empty(const char*, const aether_computed_style_t*, float, void*) {
    return {0.f, 0.f, 0.f};
}

struct Fixture {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    aether_style_map_t styles = nullptr;
    aether_layout_engine_t engine = nullptr;
    aether_style_update_t* updates = nullptr;
    int32_t update_count = 0;

    Fixture(const char* html, const char* css, aether_text_measurer_fn measurer = measure_empty) {
        require(aether_doc_create(&doc) == AETHER_OK, "create document");
        require(aether_doc_parse_html(doc, html) == AETHER_OK, "parse HTML");
        aether_style_runtime_options_t options{};
        aether_style_environment_t environment{640, 480, 1, 16, 0, 0, 0};
        require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime) == AETHER_OK,
                "create style runtime");
        int64_t sheet = 0;
        int32_t rules = 0;
        require(aether_style_runtime_add_stylesheet(runtime, css, 0, "flex-decoration.css", &sheet, &rules)
                    == AETHER_OK, "add stylesheet");
        update_count = aether_style_runtime_resolve_subtree(
            runtime, aether_doc_get_document_element(doc), &updates);
        require(update_count > 0, "resolve styles through runtime");
        require(aether_style_runtime_create_resolved_style_map(runtime, doc, &styles) == AETHER_OK,
                "create resolved style map");
        require(aether_layout_create(measurer, nullptr, &engine) == AETHER_OK, "create layout engine");
    }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    ~Fixture() {
        aether_layout_destroy(engine);
        aether_style_map_destroy(styles);
        aether_style_updates_free(updates, update_count);
        aether_style_runtime_destroy(runtime);
        aether_doc_destroy(doc);
    }

    aether_node_t node(const char* id) const {
        const auto element = aether_doc_get_element_by_id(doc, id);
        require(element != nullptr, "find fixture element");
        return element;
    }

    aether_layout_result_t layout(float width, float height) const {
        aether_layout_result_t result = nullptr;
        require(aether_layout_layout(engine, doc, styles, width, height, nullptr, nullptr, &result)
                    == AETHER_OK && result != nullptr, "perform layout");
        return result;
    }

    aether_rectf rect(aether_layout_result_t result, const char* id) const {
        const auto box = aether_layout_result_find_primary_box(result, node(id));
        require(box != nullptr, "find primary layout box");
        return aether_layout_box_get_border_box(box);
    }
};


void run(bool row, const char* name, const char* basis, float available, float ca, float cb,
         const std::string& root_a, const std::string& root_b,
         float expected_a, float expected_b, bool grow = false, const std::string& descendant_a = "", const std::string& extra = "", float expected_pos = NAN) {
 const std::string main = row ? "width" : "height";
 const std::string cross = row ? "height" : "width";
 auto expand = [&](std::string s) {
  for(size_t pos; (pos=s.find("MAIN"))!=std::string::npos;) s.replace(pos,4,main);
  return s;
 };
 std::string css = "*{margin:0;padding:0;border:0;box-sizing:border-box;display:block;}body{display:flex;"+
  main+":"+std::to_string(available)+"px;"+cross+":100px;flex-direction:"+(row?"row":"column")+
  ";align-items:flex-start;}body>div{flex:"+(grow?std::string("1"):std::string("0"))+" 1 "+basis+";min-"+main+":0;"+cross+":20px;}"+
  "#ca{"+main+":"+std::to_string(ca)+"px;"+cross+":20px;"+expand(descendant_a)+"}"+
  "#cb{"+main+":"+std::to_string(cb)+"px;"+cross+":20px;}#a{"+expand(root_a)+"}#b{"+expand(root_b)+"}"+expand(extra);
 Fixture f("<body id='container'><div id='a'><div id='ca'></div></div><div id='b'><div id='cb'></div></div></body>",css.c_str());
 auto result=f.layout(700,700); auto a=f.rect(result,"a"),b=f.rect(result,"b");
 const std::string label=std::string(row?"row ":"column ")+name;
 float av=row?a.width:a.height,bv=row?b.width:b.height,bpos=row?b.x:b.y;
 printf("%-43s a=%-11.7g b=%-11.7g b_offset=%-11.7g expected=%g/%g/%g\n",label.c_str(),av,bv,bpos,expected_a,expected_b,std::isnan(expected_pos)?expected_a:expected_pos);
 expect(av,expected_a,label.c_str(),"a size"); expect(bv,expected_b,label.c_str(),"b size"); expect(bpos,std::isnan(expected_pos)?expected_a:expected_pos,label.c_str(),"b offset");
 aether_layout_result_destroy(result);
}
static void column_content_layout() {
 const char* html = "<body id='container'><div id='a'><div id='ca'><div id='x'></div><div id='y'></div></div></div><div id='b'><div id='cb'></div></div></body>";
 const std::string base = "*{display:block;margin:0;padding:0;border:0;box-sizing:border-box;}body{display:flex;flex-direction:column;align-items:flex-start;width:200px;height:200px;}body>div{flex:0 1 content;min-height:0;width:100px;height:10px;}#cb{height:40px;}";
 {
  // The nested row wraps at the item's cross width (100), yielding 40px.
  Fixture f(html,(base+"#ca{display:flex;flex-wrap:wrap;width:100%;}#x,#y{width:60px;height:20px;flex:0 0 auto;}").c_str());
  auto r=f.layout(500,500);
  expect(f.rect(r,"a").height,40,"nested wrapped row","content height");
  expect(f.rect(r,"ca").width,100,"nested wrapped row","cross constraint");
  expect(f.rect(r,"y").y,20,"nested wrapped row","second line");
  aether_layout_result_destroy(r);
 }
 {
  // An unresolved percentage basis is content even though the preferred
  // height is definite. Container height is auto, so the base is 40px.
  for(const char* basis : {"50%", "calc(0% + 30px)", "calc(50% + 30px)"}) {
  Fixture f(html,(base+"body{height:auto;}body>div{flex-basis:"+basis+";}#ca{height:40px;}").c_str());
  auto r=f.layout(500,500);
  expect(f.rect(r,"a").height,40,"percentage basis indefinite","A height");
  expect(f.rect(r,"b").height,40,"percentage basis indefinite","B height");
  expect(f.rect(r,"container").height,80,"percentage basis indefinite","container height");
  aether_layout_result_destroy(r);
  }
 }
 {
  // In intrinsic measurement the ignored preferred height cannot resolve the
  // percentage child. Its 60px content supplies the basis; after flex the
  // final definite 100px height resolves the child to 50px.
  Fixture f(html,(base+"body>div{flex-grow:1;}#ca{height:50%;}#x{height:60px;}#cb{height:60px;}").c_str());
  auto r=f.layout(500,500);
  expect(f.rect(r,"a").height,100,"percentage descendant","A flex height");
  expect(f.rect(r,"b").height,100,"percentage descendant","B flex height");
  expect(f.rect(r,"ca").height,50,"percentage descendant","post-flex percent height");
  aether_layout_result_destroy(r);
 }
}

static aether_text_metrics_t measure_text(const char* text, const aether_computed_style_t*, float, void*) {
 return {5.f*static_cast<float>(std::strlen(text)),10.f,8.f};
}
static void real_column_content() {
 const std::string base="*{margin:0;padding:0;border:0;box-sizing:border-box;}html,body,div{display:block;}body{display:flex;flex-direction:column;align-items:flex-start;width:200px;height:250px;}#a{flex:0 1 content;height:5px;min-height:0;width:25px;font-size:10px;line-height:10px;}";
 struct Case { const char* name; const char* inner; const char* extra; float height; };
 const Case cases[]={
  {"text multiline", "aaaa aaaa", "",20},
  {"text nowrap", "aaaa aaaa", "#a{white-space:nowrap;}",10},
  {"collapsed whitespace", "  aaaa    aaaa  ", "",20},
  {"inline atomics", "<span></span><span></span>", "span{display:inline-block;width:20px;height:15px;vertical-align:top;}",30},
  {"sized input", "<input>", "#a{width:100px;}input{display:block;width:80px;height:30px;}",30},
 };
 for(const auto& c:cases) {
  std::string html="<body><div id='a'>"+std::string(c.inner)+"</div></body>";
  Fixture f(html.c_str(),(base+c.extra).c_str(),measure_text);
  auto r=f.layout(500,500); expect(f.rect(r,"a").height,c.height,c.name,"content height");
  aether_layout_result_destroy(r);
 }
 {
  // The flex item itself is a row. Its own main (outer column) constraints
  // must not be applied inside nested row layout while measuring its basis.
  Fixture f("<body><div id='a'><div id='ca'></div></div><div id='b'><div id='cb'></div></div></body>",
   (base+"#a{display:flex;width:100px;height:50px;max-height:100px;}#b{flex:0 1 content;min-height:0;width:100px;}#ca,#cb{height:200px;width:20px;}").c_str());
  auto r=f.layout(500,500);
  expect(f.rect(r,"a").height,100,"item itself flex row","A height");
  expect(f.rect(r,"b").height,150,"item itself flex row","B height");
  aether_layout_result_destroy(r);
 }
 {
  Fixture f("<body><div id='a'><div id='ca'></div></div></body>",
   (base+"#a{width:200px;min-width:80px;max-width:100px;}#ca{height:200px;max-height:60px;width:100%;}").c_str());
  auto r=f.layout(500,500);
  expect(f.rect(r,"a").width,100,"cross and descendant constraints","A width");
  expect(f.rect(r,"a").height,60,"cross and descendant constraints","A content height");
  expect(f.rect(r,"ca").height,60,"cross and descendant constraints","descendant maximum");
  aether_layout_result_destroy(r);
 }
}

static void alternate_basis() {
 const char* html="<body><div id='a'><div id='ca'></div></div><div id='b'><div id='cb'></div></div></body>";
 Fixture f(html,"*{display:block;margin:0;padding:0;border:0;}body{display:flex;width:250px;align-items:flex-start;}body>div{flex:0 1 auto;width:50px;height:20px;min-width:0;}#ca,#cb{width:200px;height:10px;}");
 auto old=f.layout(500,500);
 expect(f.rect(old,"a").width,50,"CSS alternation","initial auto");
 int64_t sheet=0;
 require(aether_style_runtime_add_stylesheet(f.runtime,"body>div{flex-basis:content;}",0,"basis-toggle",&sheet,nullptr)==AETHER_OK,"add content override");
 for(int step=0;step<2;++step) {
  if(step) require(aether_style_runtime_remove_stylesheet(f.runtime,sheet),"remove override");
  aether_style_update_t* updates=nullptr;
  const int count=aether_style_runtime_resolve_subtree(f.runtime,aether_doc_get_document_element(f.doc),&updates);
  require(count>=0,"resolve changed basis");aether_style_updates_free(updates,count);
  aether_style_map_destroy(f.styles); f.styles=nullptr;
  require(aether_style_runtime_create_resolved_style_map(f.runtime,f.doc,&f.styles)==AETHER_OK,"recreate changed map");
  auto r=f.layout(500,500);
  expect(f.rect(r,"a").width,step?50:125,"CSS alternation","new basis A");
  expect(f.rect(r,"b").width,step?50:125,"CSS alternation","new basis B");
  expect(f.rect(old,"a").width,50,"CSS alternation","retained auto unchanged");
  aether_layout_result_destroy(r);
 }
 aether_layout_result_destroy(old);
}

// All expectations below follow the raw bases and CSS Flexbox free-space
// distribution. No rendered fixture is used as an oracle for another fixture.
static void boundaries(bool row) {
 run(row,"min wins over max","content",300,100,200,
     "min-MAIN:150px;max-MAIN:50px;","",150,150);
 run(row,"subpixel maximum","auto",250,200,200,"max-MAIN:100.125px;","",100.125f,149.875f);
 run(row,"percentage definite","50%",250,200,200,"max-MAIN:100px;","",100,125);
 run(row,"calc definite","calc(50% + 25px)",250,200,200,"max-MAIN:100px;","",100,150);
 // The runtime currently rejects min-width/height:auto (its registered
 // initial value is 0). Actual automatic minima are covered with authentic
 // immutable legacy records in test_flex_intrinsic_basis_lifetime.cpp.
 run(row,"fractional shrink","content",250,200,200,"flex-shrink:.25;","flex-shrink:.25;",162.5f,162.5f);
 run(row,"fractional grow","content",500,100,200,"flex-grow:.25;","flex-grow:.25;",150,250);
 const std::string start = row ? "left" : "top", end = row ? "right" : "bottom";
 const std::string decor = "padding-"+start+":10px;padding-"+end+":10px;border-"+start+":5px solid black;border-"+end+":5px solid black;";
 // Content bases are 200 each. Fixed decoration consumes 30, leaving 220
 // content space: equal shrink weights give 110 content px to each item.
 for (const char* sizing : {"content-box", "border-box"}) {
  run(row,sizing,"content",250,200,200,decor+"box-sizing:"+sizing+";MAIN:50px;","MAIN:50px;",140,110);
 }
 run(row,"signed margins and gap","content",250,200,200,
     "margin-"+start+":-10px;margin-"+end+":20px;","",110,110,false,"","body{gap:20px;}",140);
 run(row,"auto margin receives free space","content",500,100,200,
     "margin-"+start+":auto;","",100,200,false,"","",300);
 run(row,"wrap uses hypothetical maximum","content",250,200,200,
     "max-MAIN:100px;","",100,200,false,"","body{flex-wrap:wrap;}",0);
}
int main() {
 for(bool row : {true,false}) {
  run(row,"max:auto","auto",250,200,200,"max-MAIN:100px;","",100,150);
  run(row,"max:content","content",250,200,200,"max-MAIN:100px;","",100,150);
  run(row,"max:200px control","200px",250,200,200,"max-MAIN:100px;","",100,150);
  run(row,"max:auto specified control","auto",250,200,200,"MAIN:200px;max-MAIN:100px;","MAIN:200px;",100,150);
  run(row,"min:auto grow","auto",500,100,200,"min-MAIN:200px;","",200,300,true);
  run(row,"min:content grow","content",500,100,200,"min-MAIN:200px;","",200,300,true);
  run(row,"content ignore small specified","content",250,200,200,"MAIN:50px;","MAIN:50px;",125,125);
  run(row,"content ignore large specified","content",250,200,200,"MAIN:300px;","MAIN:100px;",125,125);
 }
 for(bool row : {true,false}) boundaries(row);
 column_content_layout();
 real_column_content();
 alternate_basis();
 printf("\n%d checks; %d failures.\n",checks,failures);
 return failures?1:0;
}
