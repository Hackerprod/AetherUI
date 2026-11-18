/* C11 public-header/shared-library pixel QA; no private layout/paint access. */
#include <aether/aether.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"geometry check line %d\n",__LINE__);exit(1);}}while(0)
#define INIT(t,n) t n;memset(&n,0,sizeof(n));n.struct_size=sizeof(n)
static aether_interaction_options_v1 options;
static unsigned char* readfont(const char*path,size_t*n){FILE*f=fopen(path,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long z=ftell(f);CHECK(z>0);rewind(f);unsigned char*p=malloc((size_t)z);CHECK(p);CHECK(fread(p,1,(size_t)z,f)==(size_t)z);CHECK(!fclose(f));*n=(size_t)z;return p;}
static aether_interaction_t create(const char*type,const char*placeholder,int width){char html[1024];int n=snprintf(html,sizeof(html),"<html><body style=\"margin-top:0px;margin-right:0px;margin-bottom:0px;margin-left:0px;padding-top:0px;padding-right:0px;padding-bottom:0px;padding-left:0px;background-color:#ffffff;font-size:16px;line-height:24px;color:#000000\"><input type=\"%s\" placeholder=\"%s\" style=\"margin-top:0px;margin-right:0px;margin-bottom:0px;margin-left:0px;padding-top:0px;padding-right:0px;padding-bottom:0px;padding-left:0px;border-width:0px;width:%dpx;background-color:#ffffff\"></body></html>",type,placeholder,width);if(!width)n=snprintf(html,sizeof(html),"<html><body style=\"width:1px;background-color:#ffffff;font-size:16px;line-height:24px;color:#000000\"><input type=\"text\" style=\"width:auto;padding-left:1px;padding-right:1px\"></body></html>");CHECK(n>0&&(size_t)n<sizeof(html));aether_interaction_t s=NULL;aether_status_t status=aether_interaction_create(96,48,html,(size_t)n,&options,&s);if(status)fprintf(stderr,"create status %d width %d\n",status,width);CHECK(status==AETHER_OK);return s;}
static aether_status_t event(aether_interaction_t s,uint32_t kind,uint32_t key,const char*text,float x,float y){INIT(aether_interaction_event_v1,e);INIT(aether_interaction_result_v1,r);e.kind=kind;e.key=key;e.text=(const uint8_t*)text;e.text_length=text?strlen(text):0;e.x=x;e.y=y;return aether_interaction_dispatch(s,&e,&r);}
static void focus(aether_interaction_t s){CHECK(event(s,AETHER_INTERACTION_KEY_DOWN,AETHER_INTERACTION_KEY_TAB,NULL,0,0)==AETHER_OK);}
static void blur(aether_interaction_t s){CHECK(event(s,AETHER_INTERACTION_BLUR,0,NULL,0,0)==AETHER_OK);}
static void text(aether_interaction_t s,const char*v){CHECK(event(s,AETHER_INTERACTION_TEXT,0,v,0,0)==AETHER_OK);}
static unsigned char* frame(aether_interaction_t s){INIT(aether_interaction_result_v1,r);CHECK(aether_interaction_render(s,&r)==AETHER_OK);INIT(aether_interaction_frame_info_v1,i);CHECK(aether_interaction_copy_frame(s,&i,NULL,0)==AETHER_OK);CHECK(i.width==96&&i.height==48&&i.stride==384&&i.byte_count==18432&&i.format==AETHER_PIXEL_FORMAT_BGRA8_PREMULTIPLIED);unsigned char*p=malloc((size_t)i.byte_count);CHECK(p);CHECK(aether_interaction_copy_frame(s,&i,p,(size_t)i.byte_count)==AETHER_OK);return p;}
static int equalpx(const unsigned char*a,const unsigned char*b,int x,int y){size_t i=((size_t)y*96+(size_t)x)*4;return !memcmp(a+i,b+i,4);}
/* Difference must be a visible vertical caret inside the fixture content box,
   not at the end of its placeholder or outside the clipped content. */
static void caret(const unsigned char*a,const unsigned char*b,int expected_x){unsigned count=0;int lo=48,hi=-1;for(int y=0;y<24;y++)for(int x=0;x<40;x++)if(!equalpx(a,b,x,y)){CHECK(x==expected_x&&y>=0&&y<24);++count;if(y<lo)lo=y;if(y>hi)hi=y;}CHECK(count>=16&&hi-lo>=15);}
static void outside_same(const unsigned char*a,const unsigned char*b,int width){unsigned changed=0;for(int y=0;y<48;y++)for(int x=0;x<96;x++)if(!equalpx(a,b,x,y)){CHECK(x<width&&y<24);++changed;}if(width)CHECK(changed);else CHECK(!changed);}
static void destroy(aether_interaction_t s){CHECK(aether_interaction_destroy(s)==AETHER_OK);}
/* Enabled only after R3.1 fractional policy is linked into the real DSO. */
static void fractional_controls(void){
 for(int placeholder=0;placeholder<2;++placeholder){
  const char*html=placeholder?"<html><body style=\"font-size:8px;line-height:8px\"><input type=\"text\" placeholder=\"Name\" style=\"width:40px\"><input type=\"password\" placeholder=\"Key\" style=\"width:40px\"><button type=\"button\" style=\"width:40px\">Go</button></body></html>":"<html><body style=\"font-size:8px;line-height:8px\"><input type=\"text\" style=\"width:40px\"><input type=\"password\" style=\"width:40px\"><button type=\"button\" style=\"width:40px\">Go</button></body></html>";
  aether_interaction_t s=NULL;CHECK(aether_interaction_create(64,40,html,strlen(html),&options,&s)==AETHER_OK);
  for(int step=0;step<7;++step){
   if(step==1||step==3||step==5)focus(s);
   if(step==2||step==4)text(s,"Demo");
   if(step==6)blur(s);
   INIT(aether_interaction_result_v1,r);CHECK(aether_interaction_render(s,&r)==AETHER_OK);
   INIT(aether_interaction_state_v1,st);CHECK(aether_interaction_state(s,&st)==AETHER_OK&&st.frame_version==st.state_version);
   INIT(aether_interaction_frame_info_v1,fi);unsigned char pixels[64*40*4];CHECK(aether_interaction_copy_frame(s,&fi,pixels,sizeof(pixels))==AETHER_OK&&fi.width==64&&fi.height==40);
  }
  destroy(s);
 }
}
int main(int argc,char**argv){CHECK(argc==3||argc==4);memset(&options,0,sizeof(options));options.struct_size=sizeof(options);options.regular_font=readfont(argv[1],&options.regular_font_size);options.bold_font=readfont(argv[2],&options.bold_font_size);
 /* Placeholder retains its own pixels, adding caret at content origin x=0. */
 aether_interaction_t s=create("text","MMMM",40);unsigned char*a=frame(s);focus(s);unsigned char*b=frame(s);caret(a,b,0);free(a);free(b);destroy(s);
 /* Clipping without horizontal scrolling: appending a long suffix preserves
    every visible non-caret pixel. Focus/blur isolates the clamped end caret. */
 char many[257];memset(many,'W',256);many[256]=0;s=create("text","",40);focus(s);a=frame(s);char prefix[33];memset(prefix,'W',32);prefix[32]=0;text(s,prefix);b=frame(s);outside_same(a,b,40);free(a);text(s,many+32);unsigned char*c=frame(s);CHECK(!memcmp(b,c,18432));blur(s);a=frame(s);caret(a,c,39);free(a);free(b);free(c);destroy(s);
 /* 256 Unicode scalars, encoded in 512 bytes. A different 256-scalar value
    must produce identical bullets. Never log the value or frame content. */
 char scalars[513];for(int i=0;i<256;i++){scalars[2*i]=(char)0xc3;scalars[2*i+1]=(char)0xa1;}scalars[512]=0;
 s=create("password","",40);focus(s);a=frame(s);text(s,scalars);b=frame(s);outside_same(a,b,40);free(a);blur(s);a=frame(s);caret(a,b,39);free(a);aether_interaction_t other=create("password","",40);focus(other);text(other,many);c=frame(other);CHECK(!memcmp(b,c,18432));free(c);destroy(other);focus(s);INIT(aether_interaction_state_v1,before);CHECK(aether_interaction_state(s,&before)==AETHER_OK);CHECK(event(s,AETHER_INTERACTION_TEXT,0,"x",0,0)==AETHER_ERROR_LIMIT_EXCEEDED);INIT(aether_interaction_state_v1,after);CHECK(aether_interaction_state(s,&after)==AETHER_OK);CHECK(!memcmp(&before,&after,sizeof(before)));c=frame(s);CHECK(!memcmp(b,c,18432));free(b);free(c);destroy(s);
 /* Width zero suppresses glyphs AND caret, rather than leaking at x=0. */
 s=create("text","",0);focus(s);a=frame(s);for(size_t i=0;i<18432;i+=4)CHECK(!(a[i]==0&&a[i+1]==0&&a[i+2]==0&&a[i+3]));text(s,many);b=frame(s);outside_same(a,b,0);free(a);free(b);destroy(s);
 /* A resize invalidates pointer geometry until a new frame is committed. */
 s=create("text","",40);a=frame(s);free(a);INIT(aether_interaction_result_v1,r);CHECK(aether_interaction_resize(s,120,60,&r)==AETHER_OK);CHECK(event(s,AETHER_INTERACTION_POINTER_DOWN,0,NULL,10,10)==AETHER_ERROR_INVALID_STATE);CHECK(aether_interaction_state(s,&after)==AETHER_OK);CHECK(!after.focus_id&&after.state_version!=after.frame_version);CHECK(aether_interaction_render(s,&r)==AETHER_OK);CHECK(event(s,AETHER_INTERACTION_POINTER_DOWN,0,NULL,10,10)==AETHER_OK);CHECK(aether_interaction_state(s,&after)==AETHER_OK&&after.focus_id==1);destroy(s);
 if(argc==4){CHECK(!strcmp(argv[3],"--fractional"));fractional_controls();puts("font8 fractional public controls PASS: empty, placeholder, text, password, all focus states");}
 free((void*)options.regular_font);free((void*)options.bold_font);puts("public geometry PASS: placeholder origin, clipped text/no scroll, 256-scalar masked end caret, zero width, resize pointer freshness");return 0;
}
