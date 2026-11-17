/* External C11 static-controls consumer: public header + production DSO only.
   No text measurement/rasterization in the consumer. R1 seed uses native APIs. */
#include <aether/aether.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
#define OK(x) CHECK((x)==AETHER_OK)
static unsigned checks;
static void expected(aether_status_t got,aether_status_t want,const char* label){++checks;if(got!=want)fprintf(stderr,"FAIL %s: got %d expected %d\n",label,got,want);CHECK(got==want);}
static uint8_t* read_file(const char* path,size_t* size){FILE* f=fopen(path,"rb");long n;uint8_t* bytes;CHECK(f);CHECK(fseek(f,0,SEEK_END)==0);n=ftell(f);CHECK(n>0);CHECK(fseek(f,0,SEEK_SET)==0);*size=(size_t)n;bytes=(uint8_t*)malloc(*size);CHECK(bytes);CHECK(fread(bytes,1,*size,f)==*size);CHECK(fclose(f)==0);return bytes;}
static aether_surface_frame_info info(void){aether_surface_frame_info i;memset(&i,0xa5,sizeof(i));i.struct_size=sizeof(i);return i;}
static aether_surface_frame_info query(aether_surface_t s){aether_surface_frame_info i=info();OK(aether_surface_copy_frame(s,&i,NULL,0));return i;}
static void metadata(aether_surface_frame_info i,int w,int h,uint64_t seq){CHECK(i.struct_size==sizeof(i)&&i.width==w&&i.height==h&&i.stride==(uint64_t)w*4&&i.byte_count==(uint64_t)w*h*4&&i.sequence==seq&&i.format==AETHER_PIXEL_FORMAT_BGRA8_PREMULTIPLIED);}
typedef struct {aether_surface_frame_info i;uint8_t* pixels;} Saved;
static Saved capture(aether_surface_t s){Saved r;r.i=query(s);r.pixels=(uint8_t*)malloc((size_t)r.i.byte_count);CHECK(r.pixels);OK(aether_surface_copy_frame(s,&r.i,r.pixels,(size_t)r.i.byte_count));return r;}
static void unchanged(aether_surface_t s,const Saved* old){Saved now=capture(s);metadata(now.i,old->i.width,old->i.height,old->i.sequence);CHECK(now.i.byte_count==old->i.byte_count&&!memcmp(now.pixels,old->pixels,(size_t)now.i.byte_count));free(now.pixels);}
static void release(Saved* s){free(s->pixels);s->pixels=NULL;}
static void save(const char* prefix,const char* suffix,const Saved* s){char path[4096];FILE* f;CHECK(snprintf(path,sizeof(path),"%s-%s.bgra",prefix,suffix)>0);f=fopen(path,"wb");CHECK(f);CHECK(fwrite(s->pixels,1,(size_t)s->i.byte_count,f)==(size_t)s->i.byte_count);CHECK(fclose(f)==0);}
static aether_status_t render(aether_surface_t s,const char* html,const aether_html_ltr_options* o){return aether_surface_render_html_ltr(s,html,strlen(html),o);}
static void fails(aether_surface_t s,const char* html,const aether_html_ltr_options* o,aether_status_t want,const char* label){Saved before=capture(s);expected(render(s,html,o),want,label);unchanged(s,&before);release(&before);}
static void succeeds(aether_surface_t s,const char* html,const aether_html_ltr_options* o,const char* label){uint64_t seq=query(s).sequence;expected(render(s,html,o),AETHER_OK,label);CHECK(query(s).sequence==seq+1);}

static const char* input_a="<html><body style=\"background-color:#ffffff;padding-left:16px;padding-top:16px\"><input type=\"text\" value=\"Ana\" style=\"width:160px;padding-left:8px;padding-right:8px;padding-top:6px;padding-bottom:6px;border-width:1px;border-style:solid;border-color:#223344\"></body></html>";
static const char* empty_a="<html><body style=\"background-color:#ffffff;padding-left:16px;padding-top:16px\"><input type=\"text\" style=\"width:160px;padding-left:8px;padding-right:8px;padding-top:6px;padding-bottom:6px;border-width:1px;border-style:solid;border-color:#223344\"></body></html>";
static void equal_pixels(const Saved* a,const Saved* b){CHECK(a->i.byte_count==b->i.byte_count);CHECK(!memcmp(a->pixels,b->pixels,(size_t)a->i.byte_count));}
static void case_html(aether_surface_t s,const aether_html_ltr_options* o,const char* content,aether_status_t status,const char* label){char html[16384];CHECK(snprintf(html,sizeof(html),"<html><body>%s</body></html>",content)>0);if(status==AETHER_OK)succeeds(s,html,o,label);else fails(s,html,o,status,label);}
static void captures(aether_surface_t s,const char* prefix,const char* suffix){Saved x=capture(s);save(prefix,suffix,&x);release(&x);}
static void g1(aether_surface_t s,const aether_html_ltr_options* o,const char* prefix){
 Saved a,b;uint64_t seq;
 succeeds(s,input_a,o,"G1 input native");a=capture(s);save(prefix,"input",&a);
 succeeds(s,empty_a,o,"G1 empty native");b=capture(s);save(prefix,"empty",&b);CHECK(memcmp(a.pixels,b.pixels,(size_t)a.i.byte_count));release(&a);release(&b);
 /* Effective content width zero: decorations consume the entire 1px container. */
 succeeds(s,"<html><body style=\"background-color:#ffffff;width:1px\"><input type=\"text\" value=\"Ana\" style=\"padding-left:1px;padding-right:1px;border-width:1px;border-style:solid;border-color:#223344\"></body></html>",o,"G1 total clip");a=capture(s);save(prefix,"zero-clip",&a);
 succeeds(s,"<html><body style=\"background-color:#ffffff;width:1px\"><input type=\"text\" style=\"padding-left:1px;padding-right:1px;border-width:1px;border-style:solid;border-color:#223344\"></body></html>",o,"G1 total clip empty oracle");b=capture(s);equal_pixels(&a,&b);release(&a);release(&b);
 {aether_html_ltr_options h=*o;h.profile=1;fails(s,input_a,&h,AETHER_ERROR_UNSUPPORTED,"H input remains rejected");}
 succeeds(s,input_a,o,"G1 lifetime baseline");a=capture(s);
 {aether_html_ltr_options t=*o;size_t n=strlen(input_a);char* html=(char*)malloc(n+1);uint8_t* rf=(uint8_t*)malloc(o->regular_font_size);uint8_t* bf=(uint8_t*)malloc(o->bold_font_size);CHECK(html&&rf&&bf);memcpy(html,input_a,n+1);memcpy(rf,o->regular_font,o->regular_font_size);memcpy(bf,o->bold_font,o->bold_font_size);t.regular_font=rf;t.bold_font=bf;seq=query(s).sequence;OK(aether_surface_render_html_ltr(s,html,n,&t));memset(html,0,n+1);memset(rf,0,o->regular_font_size);memset(bf,0,o->bold_font_size);free(html);free(rf);free(bf);CHECK(query(s).sequence==seq+1);}
 b=capture(s);equal_pixels(&a,&b);release(&a);release(&b);
 case_html(s,o,"<input type=\"text\" value=\"Ana\" style=\"width:1px\">",AETHER_OK,"G1 width1");captures(s,prefix,"width1");
 case_html(s,o,"<input type=\"text\" value=\"Ana\" style=\"width:0px\">",AETHER_ERROR_LIMIT_EXCEEDED,"G1 width0 reject");
 case_html(s,o,"<input type=\"text\" value=\"  A  B  \" placeholder=\"Hidden\">",AETHER_OK,"G1 preserve spaces");
 case_html(s,o,"<input type=\"text\" value=\"Ana\"><textarea></textarea>",AETHER_ERROR_UNSUPPORTED,"G1 admission late tag");
 succeeds(s,input_a,o,"G1 recovery");
}
static void full(aether_surface_t s,const aether_html_ltr_options* o,const char* prefix){
 const struct {const char* html; aether_status_t status;} cases[]={
 {"<input type=\"text\">",AETHER_OK},{"<input  value=\"Ana\" placeholder=\"Usuario\" type=\"text\" >",AETHER_OK},{"<input style=\"width:200px\" placeholder=\"Usuario\" type=\"text\" value=\"Ana\">",AETHER_OK},
 {"<input type=\"password\" value=\" á  é \" placeholder=\"Clave\">",AETHER_OK},{"<input type=\"password\" placeholder=\"Clave\">",AETHER_OK},{"<input type=\"password\" value=\"\" placeholder=\"\">",AETHER_OK},{"<button type=\"button\">Entrar</button>",AETHER_OK},
 {"<input>",AETHER_ERROR_INVALID_ARGUMENT},{"<input value=\"Ana\">",AETHER_ERROR_INVALID_ARGUMENT},{"<input type=\"text\" type=\"text\">",AETHER_ERROR_INVALID_ARGUMENT},{"<input type=\"text\" value=\"A\" value=\"A\">",AETHER_ERROR_INVALID_ARGUMENT},
 {"<input type=\"text\"/>",AETHER_ERROR_INVALID_ARGUMENT},{"<input type=\"text\"></input>",AETHER_ERROR_INVALID_ARGUMENT},{"<input type =\"text\">",AETHER_ERROR_INVALID_ARGUMENT},{"<input type= \"text\">",AETHER_ERROR_INVALID_ARGUMENT},{"<input type='text'>",AETHER_ERROR_INVALID_ARGUMENT},{"<input type=text>",AETHER_ERROR_INVALID_ARGUMENT},{"<input\ttype=\"text\">",AETHER_ERROR_INVALID_ARGUMENT},{"<input\ntype=\"text\">",AETHER_ERROR_INVALID_ARGUMENT},
 {"<input type=\"checkbox\">",AETHER_ERROR_UNSUPPORTED},{"<input type=\"Text\">",AETHER_ERROR_UNSUPPORTED},{"<input type=\"text\" disabled=\"disabled\">",AETHER_ERROR_UNSUPPORTED},{"<input type=\"text\" autofocus=\"autofocus\">",AETHER_ERROR_UNSUPPORTED},{"<input type=\"text\" tabindex=\"0\">",AETHER_ERROR_UNSUPPORTED},{"<input type=\"text\" readonly=\"readonly\">",AETHER_ERROR_UNSUPPORTED},{"<input type=\"text\" onclick=\"x\">",AETHER_ERROR_UNSUPPORTED},
 {"<button>Entrar</button>",AETHER_ERROR_INVALID_ARGUMENT},{"<button type=\"submit\">Entrar</button>",AETHER_ERROR_UNSUPPORTED},{"<button type=\"button\"></button>",AETHER_ERROR_INVALID_ARGUMENT},{"<button type=\"button\"><p>A</p></button>",AETHER_ERROR_UNSUPPORTED},{"<button type=\"button\"> A</button>",AETHER_ERROR_UNSUPPORTED},{"<button type=\"button\">A  B</button>",AETHER_ERROR_UNSUPPORTED},{"<button type=\"button\" value=\"X\">A</button>",AETHER_ERROR_UNSUPPORTED},
 {"<input type=\"password\" value=\"A&amp;B\">",AETHER_ERROR_UNSUPPORTED},{"<input type=\"password\" value=\"A<B\">",AETHER_ERROR_UNSUPPORTED},{"<input type=\"password\" value=\"\t\">",AETHER_ERROR_UNSUPPORTED},{"<input type=\"password\" value=\"\n\">",AETHER_ERROR_UNSUPPORTED},{"<input type=\"password\" value=\"•\">",AETHER_ERROR_UNSUPPORTED},{"<input type=\"password\" value=\"😀\">",AETHER_ERROR_UNSUPPORTED},{"<input type=\"password\" value=\"é\">",AETHER_ERROR_UNSUPPORTED},
 {"<input type=\"password\" value=\"A\" placeholder=\"•\">",AETHER_ERROR_UNSUPPORTED},{"<input type=\"password\" value=\"A\" placeholder=\"😀\">",AETHER_ERROR_UNSUPPORTED},{"<input type=\"password\" value=\"A\" placeholder=\"\xc0\xaf\">",AETHER_ERROR_INVALID_ARGUMENT},{"<input type=\"password\" value=\"\xed\xa0\x80\">",AETHER_ERROR_INVALID_ARGUMENT},{"<input type=\"password\" value=\"\xf0\x9f\">",AETHER_ERROR_INVALID_ARGUMENT},
 {"<input type=\"text\" style=\"height:40px\">",AETHER_ERROR_UNSUPPORTED},{"<input type=\"text\" style=\"opacity:1\">",AETHER_ERROR_UNSUPPORTED},{"<input type=\"text\" style=\"transform:translateX(1px)\">",AETHER_ERROR_UNSUPPORTED},{"<input type=\"text\" style=\"background:linear-gradient(red,blue)\">",AETHER_ERROR_UNSUPPORTED},{"<input type=\"text\" style=\"box-shadow:1px 1px #000000\">",AETHER_ERROR_UNSUPPORTED}
 };
 for(size_t k=0;k<sizeof(cases)/sizeof(cases[0]);++k){char label[64];snprintf(label,sizeof(label),"R3 grammar case %zu",k);case_html(s,o,cases[k].html,cases[k].status,label);}
 {const char bytes[]="<html><body><input type=\"password\" value=\"A\0B\"></body></html>";Saved old=capture(s);expected(aether_surface_render_html_ltr(s,bytes,sizeof(bytes)-1,o),AETHER_ERROR_INVALID_ARGUMENT,"password NUL");unchanged(s,&old);release(&old);}
 /* Every failure checks committed bytes and sequence. Exact quota endpoints. */
 {char content[20000];char value[258];memset(value,'i',257);value[256]=0;snprintf(content,sizeof(content),"<input type=\"password\" value=\"%s\">",value);case_html(s,o,content,AETHER_OK,"value256");value[256]='i';value[257]=0;snprintf(content,sizeof(content),"<input type=\"password\" value=\"%s\">",value);case_html(s,o,content,AETHER_ERROR_LIMIT_EXCEEDED,"value257");value[256]=0;snprintf(content,sizeof(content),"<input type=\"text\" value=\"X\" placeholder=\"%s\">",value);case_html(s,o,content,AETHER_OK,"hidden placeholder256");value[256]='i';snprintf(content,sizeof(content),"<input type=\"text\" value=\"X\" placeholder=\"%s\">",value);case_html(s,o,content,AETHER_ERROR_LIMIT_EXCEEDED,"hidden placeholder257");
 content[0]=0;for(int k=0;k<32;++k)strcat(content,"<input type=\"text\">");case_html(s,o,content,AETHER_OK,"controls32");strcat(content,"<button type=\"button\">A</button>");case_html(s,o,content,AETHER_ERROR_LIMIT_EXCEEDED,"controls33");
 value[256]=0;content[0]=0;for(int k=0;k<8;++k){size_t n=strlen(content);snprintf(content+n,sizeof(content)-n,"<input type=\"password\" value=\"%s\" placeholder=\"%s\">",value,value);}case_html(s,o,content,AETHER_OK,"authored4096 hidden included");strcat(content,"<p>A</p>");case_html(s,o,content,AETHER_ERROR_LIMIT_EXCEEDED,"authored4097");
 }
 /* Value wins over placeholder; fixed placeholder color ignores authored value color. */
 {Saved a,b;case_html(s,o,"<input type=\"text\" value=\"Ana\" placeholder=\"Hidden\">",AETHER_OK,"value precedence");a=capture(s);case_html(s,o,"<input type=\"text\" value=\"Ana\">",AETHER_OK,"value no placeholder");b=capture(s);equal_pixels(&a,&b);release(&a);release(&b);case_html(s,o,"<input type=\"text\" placeholder=\"Ana\" style=\"color:#ff0000\">",AETHER_OK,"placeholder red style");a=capture(s);case_html(s,o,"<input type=\"text\" placeholder=\"Ana\" style=\"color:#00ff00\">",AETHER_OK,"placeholder green style");b=capture(s);equal_pixels(&a,&b);release(&a);release(&b);}
 case_html(s,o,"<p>Ana</p><input type=\"text\" value=\"Ana\" style=\"color:#ff0000\"><input type=\"password\" value=\"Ana\"><button type=\"button\" style=\"font-weight:700;color:#0000ff\">Ana</button>",AETHER_OK,"identity same strings owners");captures(s,prefix,"identity");
 case_html(s,o,"<input type=\"password\" value=\"abc\"><input type=\"password\" value=\" áé\" style=\"font-weight:700\"><input type=\"password\" placeholder=\"Clave\">",AETHER_OK,"password capture");captures(s,prefix,"password");
 /* Independent composition oracle: text may change only pixels in exact content clip. */
 {Saved a,b;case_html(s,o,"<input type=\"text\" value=\"WWWWWWWWWWWWWWWWWWWWWWWW\" style=\"width:20px;padding-left:4px;padding-right:4px;padding-top:4px;padding-bottom:4px;border-width:1px;border-style:solid;border-color:#223344;background-color:#ffffff\">",AETHER_OK,"long clipped input");a=capture(s);save(prefix,"long-clip",&a);case_html(s,o,"<input type=\"text\" style=\"width:20px;padding-left:4px;padding-right:4px;padding-top:4px;padding-bottom:4px;border-width:1px;border-style:solid;border-color:#223344;background-color:#ffffff\">",AETHER_OK,"long clip empty oracle");b=capture(s);int changed=0;for(int y=0;y<a.i.height;++y)for(int x=0;x<a.i.width;++x){size_t n=(size_t)y*(size_t)a.i.stride+(size_t)x*4;int differs=memcmp(a.pixels+n,b.pixels+n,4)!=0;if(x<5||x>=25||y<5||y>=29)CHECK(!differs);else if(differs)changed=1;}CHECK(changed);release(&a);release(&b);}
 for(int k=0;k<16;++k){succeeds(s,input_a,o,"repeat value");succeeds(s,empty_a,o,"repeat empty");}
}
int main(int argc,char** argv){
 size_t nr,nb;uint8_t* regular;uint8_t* bold;aether_surface_t s=NULL;aether_html_ltr_options o;const char* prefix;
 CHECK(argc==4||argc==5);prefix=argv[3];regular=read_file(argv[1],&nr);bold=read_file(argv[2],&nb);memset(&o,0,sizeof(o));o.struct_size=sizeof(o);o.profile=AETHER_HTML_LTR_PROFILE_STATIC_CONTROLS_2;o.regular_font=regular;o.regular_font_size=nr;o.bold_font=bold;o.bold_font_size=nb;
 CHECK(AETHER_ABI_VERSION_GET_MINOR(aether_abi_version())==37);CHECK(aether_abi_version()==AETHER_ABI_VERSION_CURRENT);CHECK(aether_abi_capabilities()&AETHER_CAPABILITY_NATIVE_HTML_LTR);CHECK(aether_html_ltr_options_size()==sizeof(o));CHECK(aether_html_ltr_profile_limitations(1)==3);CHECK(aether_html_ltr_profile_limitations(2)==15);CHECK(aether_html_ltr_profile_limitations(3)==UINT32_MAX);
 OK(aether_surface_create(640,320,&s));OK(render(s,"<html><body></body></html>",&o));
 {aether_html_ltr_options bad=o;bad.profile=3;fails(s,input_a,&bad,AETHER_ERROR_INVALID_ARGUMENT,"unknown profile");bad=o;bad.struct_size++;fails(s,input_a,&bad,AETHER_ERROR_INVALID_ARGUMENT,"wrong struct");for(unsigned k=0;k<4;++k){bad=o;bad.reserved[k]=1;fails(s,input_a,&bad,AETHER_ERROR_INVALID_ARGUMENT,"reserved");}}
 g1(s,&o,prefix);if(argc!=5||strcmp(argv[4],"--g1"))full(s,&o,prefix);
 free(regular);free(bold);captures(s,prefix,"final");OK(aether_surface_destroy(s));printf("PASS R3 public C11 %s checks=%u; header + DSO only; no host text measurement or painting\n",argc==5?"G1":"matrix",checks);return 0;
}
