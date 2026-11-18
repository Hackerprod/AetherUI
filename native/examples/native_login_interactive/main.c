/* C11 event host: native engine owns focus, editing, shaping and rasterization. */
#include <aether/aether.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char* load(const char* path,size_t*n){FILE*f=fopen(path,"rb");if(!f)return NULL;if(fseek(f,0,SEEK_END)){fclose(f);return NULL;}long z=ftell(f);if(z<=0||z>16*1024*1024){fclose(f);return NULL;}rewind(f);unsigned char*p=malloc((size_t)z);if(!p){fclose(f);return NULL;}*n=fread(p,1,(size_t)z,f);fclose(f);if(*n!=(size_t)z){free(p);return NULL;}return p;}
static aether_status_t present(aether_interaction_t s,const char* path){aether_interaction_result_v1 r={0};r.struct_size=sizeof(r);aether_status_t rc=aether_interaction_render(s,&r);if(rc!=AETHER_OK)return rc;aether_interaction_frame_info_v1 f={0};f.struct_size=sizeof(f);rc=aether_interaction_copy_frame(s,&f,NULL,0);if(rc!=AETHER_OK)return rc;unsigned char*p=malloc((size_t)f.byte_count);if(!p)return AETHER_ERROR_OUT_OF_MEMORY;rc=aether_interaction_copy_frame(s,&f,p,(size_t)f.byte_count);if(rc==AETHER_OK){FILE*out=fopen(path,"wb");if(!out)rc=AETHER_ERROR_INVALID_STATE;else{if(fwrite(p,1,(size_t)f.byte_count,out)!=(size_t)f.byte_count)rc=AETHER_ERROR_INVALID_STATE;fclose(out);}}free(p);return rc;}
int main(int argc,char**argv){
 if(argc!=5){fprintf(stderr,"usage: %s login.html regular.ttf bold.ttf output.bgra\n",argv[0]);return 2;}
 size_t nh=0,nr=0,nb=0;unsigned char*h=load(argv[1],&nh),*r=load(argv[2],&nr),*b=load(argv[3],&nb);if(!h||!r||!b){free(h);free(r);free(b);return 2;}
 aether_interaction_options_v1 o={0};o.struct_size=sizeof(o);o.regular_font=r;o.regular_font_size=nr;o.bold_font=b;o.bold_font_size=nb;aether_interaction_t s=NULL;aether_status_t rc=aether_interaction_create(640,400,(const char*)h,nh,&o,&s);free(h);free(r);free(b);if(rc!=AETHER_OK){fprintf(stderr,"create code=%d\n",rc);return 1;}
 rc=present(s,argv[4]);fprintf(stderr,"render code=%d\n",rc);
 /* Commands translate OS-like events only; no hit testing or focus decisions. */
 char line[1100];while(fgets(line,sizeof(line),stdin)){size_t n=strlen(line);if(n&&line[n-1]=='\n')line[--n]=0;if(!strcmp(line,"quit"))break;
 aether_interaction_event_v1 e={0};e.struct_size=sizeof(e);aether_interaction_result_v1 out={0};out.struct_size=sizeof(out);int w=0,height=0;unsigned key=0,mods=0;
 if(sscanf(line,"resize %d %d",&w,&height)==2)rc=aether_interaction_resize(s,w,height,&out);
 else {if(!strncmp(line,"text ",5)){e.kind=AETHER_INTERACTION_TEXT;e.text=(const uint8_t*)line+5;e.text_length=n-5;}else if(sscanf(line,"down %f %f",&e.x,&e.y)==2)e.kind=AETHER_INTERACTION_POINTER_DOWN;else if(sscanf(line,"up %f %f",&e.x,&e.y)==2)e.kind=AETHER_INTERACTION_POINTER_UP;else if(sscanf(line,"keydown %u %u",&key,&mods)>=1){e.kind=AETHER_INTERACTION_KEY_DOWN;e.key=key;e.modifiers=mods;}else if(sscanf(line,"keyup %u",&key)==1){e.kind=AETHER_INTERACTION_KEY_UP;e.key=key;}else if(!strcmp(line,"cancel"))e.kind=AETHER_INTERACTION_POINTER_CANCEL;else if(!strcmp(line,"blur"))e.kind=AETHER_INTERACTION_BLUR;else {memset(line,0,sizeof(line));continue;}rc=aether_interaction_dispatch(s,&e,&out);}
 fprintf(stderr,"event=%u code=%d version=%llu activated=%u\n",e.kind,rc,(unsigned long long)out.state_version,out.activated_control_id);memset(line,0,sizeof(line));if(rc==AETHER_OK){rc=present(s,argv[4]);fprintf(stderr,"render code=%d\n",rc);}}
 memset(line,0,sizeof(line));return aether_interaction_destroy(s)==AETHER_OK?0:1;
}
