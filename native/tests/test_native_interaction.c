/* Public C11 / shared-library consumer. No private headers or callbacks. */
#include <aether/aether.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"check failed line %d\n",__LINE__); exit(1); } } while(0)
#define INIT(t,n) t n; memset(&n,0,sizeof(n)); n.struct_size=sizeof(n)
_Static_assert(offsetof(aether_interaction_event_v1,kind)==4,"kind offset");
_Static_assert(offsetof(aether_interaction_event_v1,x)==16,"x offset");
_Static_assert(offsetof(aether_interaction_state_v1,state_version)==32,"version offset");
_Static_assert(sizeof(aether_interaction_control_info_v1)==28,"control size");
#if UINTPTR_MAX == UINT64_MAX
_Static_assert(sizeof(aether_interaction_options_v1)==56,"options LP64");
_Static_assert(sizeof(aether_interaction_event_v1)==56,"event LP64");
_Static_assert(sizeof(aether_interaction_result_v1)==40,"result LP64");
_Static_assert(sizeof(aether_interaction_state_v1)==72,"state LP64");
_Static_assert(sizeof(aether_interaction_frame_info_v1)==64,"frame LP64");
_Static_assert(offsetof(aether_interaction_event_v1,text)==24,"text LP64");
#endif
static unsigned char* read_file(const char* path,size_t* size){FILE*f=fopen(path,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long n=ftell(f);CHECK(n>0);rewind(f);unsigned char*p=malloc((size_t)n);CHECK(p);CHECK(fread(p,1,(size_t)n,f)==(size_t)n);CHECK(!fclose(f));*size=(size_t)n;return p;}
static aether_interaction_state_v1 state(aether_interaction_t s){INIT(aether_interaction_state_v1,v);CHECK(aether_interaction_state(s,&v)==AETHER_OK);return v;}
static aether_interaction_result_v1 send_event(aether_interaction_t s,uint32_t kind,uint32_t key,const char*text,float x,float y){INIT(aether_interaction_event_v1,e);INIT(aether_interaction_result_v1,o);e.kind=kind;e.key=key;e.text=(const uint8_t*)text;e.text_length=text?strlen(text):0;e.x=x;e.y=y;CHECK(aether_interaction_dispatch(s,&e,&o)==AETHER_OK);return o;}
static aether_interaction_result_v1 key(aether_interaction_t s,uint32_t kind,uint32_t k){return send_event(s,kind,k,NULL,0,0);}
static void render(aether_interaction_t s){INIT(aether_interaction_result_v1,r);CHECK(aether_interaction_render(s,&r)==AETHER_OK);CHECK(!r.activated_control_id);}
static unsigned char* pixels(aether_interaction_t s,size_t*n){INIT(aether_interaction_frame_info_v1,f);CHECK(aether_interaction_copy_frame(s,&f,NULL,0)==AETHER_OK);*n=(size_t)f.byte_count;unsigned char*p=malloc(*n);CHECK(p);CHECK(aether_interaction_copy_frame(s,&f,p,*n)==AETHER_OK);return p;}
static void snapshot(aether_interaction_t s,const char*dir,const char*name){INIT(aether_interaction_frame_info_v1,i);CHECK(aether_interaction_copy_frame(s,&i,NULL,0)==AETHER_OK);CHECK(i.format==AETHER_PIXEL_FORMAT_BGRA8_PREMULTIPLIED);CHECK(i.stride==(uint64_t)i.width*4);CHECK(i.byte_count==i.stride*(uint64_t)i.height);size_t n=(size_t)i.byte_count;unsigned char*p=malloc(n+32);CHECK(p);memset(p,0xa5,n+32);CHECK(aether_interaction_copy_frame(s,&i,p,n-1)==AETHER_ERROR_BUFFER_TOO_SMALL);for(size_t j=0;j<n+32;j++)CHECK(p[j]==0xa5);CHECK(aether_interaction_copy_frame(s,&i,p,n+32)==AETHER_OK);for(size_t j=n;j<n+32;j++)CHECK(p[j]==0xa5);char path[4096];CHECK(snprintf(path,sizeof(path),"%s/%s.bgra",dir,name)>0);FILE*f=fopen(path,"wb");CHECK(f);CHECK(fwrite(p,1,n,f)==n);CHECK(!fclose(f));free(p);}
int main(int argc,char**argv){
 CHECK(argc==4);size_t nr,nb;unsigned char*r=read_file(argv[1],&nr),*b=read_file(argv[2],&nb);
 const char*html="<html><body style=\"font-size:16px;line-height:24px\"><input type=\"text\" placeholder=\"Usuario\" style=\"width:160px\"><input type=\"password\" placeholder=\"Clave\" style=\"width:160px\"><button type=\"button\" style=\"width:160px\">Entrar</button></body></html>";
 INIT(aether_interaction_options_v1,o);o.regular_font=r;o.regular_font_size=nr;o.bold_font=b;o.bold_font_size=nb;
 CHECK(aether_interaction_options_v1_size()==sizeof(o));CHECK(aether_interaction_event_v1_size()==sizeof(aether_interaction_event_v1));CHECK(aether_interaction_result_v1_size()==sizeof(aether_interaction_result_v1));CHECK(aether_interaction_state_v1_size()==sizeof(aether_interaction_state_v1));CHECK(aether_interaction_frame_info_v1_size()==sizeof(aether_interaction_frame_info_v1));CHECK(aether_interaction_control_info_v1_size()==sizeof(aether_interaction_control_info_v1));
 aether_interaction_t s=(aether_interaction_t)(uintptr_t)1;o.flags=1;CHECK(aether_interaction_create(320,200,html,strlen(html),&o,&s)==AETHER_ERROR_INVALID_ARGUMENT);CHECK(!s);o.flags=0;o.reserved[2]=1;CHECK(aether_interaction_create(320,200,html,strlen(html),&o,&s)==AETHER_ERROR_INVALID_ARGUMENT);CHECK(!s);o.reserved[2]=0;
 CHECK(aether_interaction_create(320,200,html,strlen(html),&o,&s)==AETHER_OK);memset(r,0,nr);memset(b,0,nb);free(r);free(b); /* owned-input lifetime */
 CHECK(aether_interaction_destroy(NULL)==AETHER_OK);CHECK(aether_interaction_state(s,NULL)==AETHER_ERROR_INVALID_ARGUMENT);CHECK(aether_interaction_control_count(s,NULL)==AETHER_ERROR_INVALID_ARGUMENT);
 uint32_t count=99;CHECK(aether_interaction_control_count(s,&count)==AETHER_OK&&count==3);for(uint32_t j=0;j<count;j++){INIT(aether_interaction_control_info_v1,c);CHECK(aether_interaction_control_info(s,j,&c)==AETHER_OK);CHECK(c.control_id==j+1&&c.kind==j+1);}
 INIT(aether_interaction_frame_info_v1,fi);CHECK(aether_interaction_copy_frame(s,&fi,NULL,0)==AETHER_ERROR_NO_FRAME);
 struct { aether_interaction_result_v1 r; uint64_t canary; } guarded;memset(&guarded,0,sizeof(guarded));guarded.r.struct_size=sizeof(guarded.r);guarded.canary=UINT64_C(0x12345678abcdef01);INIT(aether_interaction_event_v1,e);e.kind=AETHER_INTERACTION_KEY_DOWN;e.key=AETHER_INTERACTION_KEY_TAB;
 unsigned char saved[sizeof(guarded)];memcpy(saved,&guarded,sizeof(saved));e.reserved[0]=1;CHECK(aether_interaction_dispatch(s,&e,&guarded.r)==AETHER_ERROR_INVALID_ARGUMENT);CHECK(!memcmp(saved,&guarded,sizeof(saved)));e.reserved[0]=0;e.struct_size--;CHECK(aether_interaction_dispatch(s,&e,&guarded.r)==AETHER_ERROR_INVALID_ARGUMENT);e.struct_size++;e.kind=AETHER_INTERACTION_POINTER_DOWN;e.key=0;e.x=NAN;CHECK(aether_interaction_dispatch(s,&e,&guarded.r)==AETHER_ERROR_INVALID_ARGUMENT);e.x=0;CHECK(aether_interaction_dispatch(s,&e,&guarded.r)==AETHER_ERROR_INVALID_STATE);CHECK(!memcmp(saved,&guarded,sizeof(saved)));

 CHECK(aether_interaction_dispatch(s,NULL,&guarded.r)==AETHER_ERROR_INVALID_ARGUMENT);CHECK(aether_interaction_dispatch(s,&e,NULL)==AETHER_ERROR_INVALID_ARGUMENT);CHECK(aether_interaction_render(s,NULL)==AETHER_ERROR_INVALID_ARGUMENT);CHECK(aether_interaction_resize(s,1,1,NULL)==AETHER_ERROR_INVALID_ARGUMENT);CHECK(aether_interaction_copy_frame(s,NULL,NULL,0)==AETHER_ERROR_INVALID_ARGUMENT);CHECK(aether_interaction_copy_frame(s,&fi,NULL,1)==AETHER_ERROR_INVALID_ARGUMENT);
 {INIT(aether_interaction_state_v1,bad);bad.reserved[3]=1;aether_interaction_state_v1 copy=bad;CHECK(aether_interaction_state(s,&bad)==AETHER_ERROR_INVALID_ARGUMENT);CHECK(!memcmp(&bad,&copy,sizeof(bad)));}
 {const uint8_t malformed[]={0xc0,0x80};e.kind=AETHER_INTERACTION_TEXT;e.x=0;e.y=0;e.text=malformed;e.text_length=sizeof(malformed);CHECK(aether_interaction_dispatch(s,&e,&guarded.r)==AETHER_ERROR_INVALID_ARGUMENT);CHECK(!memcmp(saved,&guarded,sizeof(saved)));e.text=NULL;e.text_length=0;e.kind=AETHER_INTERACTION_POINTER_UP;}
 render(s);snapshot(s,argv[3],"initial");CHECK(state(s).sequence==1);render(s);CHECK(state(s).sequence==1);
 key(s,AETHER_INTERACTION_KEY_DOWN,AETHER_INTERACTION_KEY_TAB);CHECK(state(s).focus_id==1);
 {render(s);size_t na,nz;unsigned char*a=pixels(s,&na);send_event(s,AETHER_INTERACTION_TEXT,0,"á",0,0);key(s,AETHER_INTERACTION_KEY_DOWN,AETHER_INTERACTION_KEY_BACKSPACE);render(s);unsigned char*z=pixels(s,&nz);CHECK(na==nz&&!memcmp(a,z,na));free(a);free(z);} /* scalar deletion restores placeholder pixels */
 send_event(s,AETHER_INTERACTION_TEXT,0,"á& lt;<input>\"'",0,0);key(s,AETHER_INTERACTION_KEY_DOWN,AETHER_INTERACTION_KEY_BACKSPACE);render(s);snapshot(s,argv[3],"text");
 key(s,AETHER_INTERACTION_KEY_DOWN,AETHER_INTERACTION_KEY_TAB);CHECK(state(s).focus_id==2);send_event(s,AETHER_INTERACTION_TEXT,0,"Ficticioá",0,0);key(s,AETHER_INTERACTION_KEY_DOWN,AETHER_INTERACTION_KEY_BACKSPACE);render(s);snapshot(s,argv[3],"password");
 {size_t na,nz;unsigned char*a=pixels(s,&na);for(int j=0;j<8;j++)key(s,AETHER_INTERACTION_KEY_DOWN,AETHER_INTERACTION_KEY_BACKSPACE);send_event(s,AETHER_INTERACTION_TEXT,0,"OtroDemo",0,0);render(s);unsigned char*z=pixels(s,&nz);CHECK(na==nz&&!memcmp(a,z,na));free(a);free(z);} /* same scalar count, same masked pixels */

 key(s,AETHER_INTERACTION_KEY_DOWN,AETHER_INTERACTION_KEY_TAB);CHECK(state(s).focus_id==3);CHECK(key(s,AETHER_INTERACTION_KEY_DOWN,AETHER_INTERACTION_KEY_ENTER).activated_control_id==3);CHECK(!key(s,AETHER_INTERACTION_KEY_DOWN,AETHER_INTERACTION_KEY_ENTER).activated_control_id);CHECK(!key(s,AETHER_INTERACTION_KEY_UP,AETHER_INTERACTION_KEY_ENTER).activated_control_id);CHECK(!key(s,AETHER_INTERACTION_KEY_DOWN,AETHER_INTERACTION_KEY_SPACE).activated_control_id);CHECK(state(s).arm_kind==AETHER_INTERACTION_ARM_SPACE);CHECK(key(s,AETHER_INTERACTION_KEY_UP,AETHER_INTERACTION_KEY_SPACE).activated_control_id==3);CHECK(!key(s,AETHER_INTERACTION_KEY_UP,AETHER_INTERACTION_KEY_SPACE).activated_control_id);
 render(s);snapshot(s,argv[3],"button");
 /* Fixed fixture coordinates, never a public target id. */
 send_event(s,AETHER_INTERACTION_POINTER_DOWN,0,NULL,10,60);CHECK(state(s).arm_kind==AETHER_INTERACTION_ARM_POINTER);e.x=10;e.y=60;e.kind=AETHER_INTERACTION_POINTER_UP;CHECK(aether_interaction_dispatch(s,&e,&guarded.r)==AETHER_ERROR_INVALID_STATE);render(s);CHECK(send_event(s,AETHER_INTERACTION_POINTER_UP,0,NULL,10,60).activated_control_id==3);render(s);CHECK(!send_event(s,AETHER_INTERACTION_POINTER_UP,0,NULL,10,60).activated_control_id);
 send_event(s,AETHER_INTERACTION_POINTER_DOWN,0,NULL,10,60);send_event(s,AETHER_INTERACTION_POINTER_CANCEL,0,NULL,0,0);CHECK(!state(s).arm_kind);render(s);send_event(s,AETHER_INTERACTION_POINTER_DOWN,0,NULL,10,60);render(s);CHECK(!send_event(s,AETHER_INTERACTION_POINTER_UP,0,NULL,-1,-1).activated_control_id);
 uint64_t oldframe=state(s).frame_version;CHECK(aether_interaction_resize(s,0,200,&guarded.r)==AETHER_OK);CHECK(aether_interaction_render(s,&guarded.r)==AETHER_ERROR_SUSPENDED);CHECK(state(s).frame_version==oldframe);CHECK(aether_interaction_copy_frame(s,&fi,NULL,0)==AETHER_OK);CHECK(fi.width==320);key(s,AETHER_INTERACTION_KEY_DOWN,AETHER_INTERACTION_KEY_TAB);CHECK(aether_interaction_resize(s,360,220,&guarded.r)==AETHER_OK);render(s);snapshot(s,argv[3],"resized");CHECK(state(s).state_version==state(s).frame_version);CHECK(guarded.canary==UINT64_C(0x12345678abcdef01));CHECK(aether_interaction_destroy(s)==AETHER_OK);
 puts("public C11 interaction checks PASS; pixel QA and stress are separate gates");return 0;
}
