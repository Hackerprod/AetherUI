/* C11 consumer: supply licensed font bytes, render HTML, copy/present pixels.
   No host text measurement, font lookup, glyph drawing, or layout. */
#include <aether/aether.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char* read_bytes(const char* path,size_t* n){
 FILE* f=fopen(path,"rb");if(!f)return NULL;
 if(fseek(f,0,SEEK_END)!=0){fclose(f);return NULL;}long size=ftell(f);
 if(size<=0||size>16*1024*1024||fseek(f,0,SEEK_SET)!=0){fclose(f);return NULL;}
 unsigned char* p=(unsigned char*)malloc((size_t)size);if(!p){fclose(f);return NULL;}
 *n=fread(p,1,(size_t)size,f);fclose(f);if(*n!=(size_t)size){free(p);return NULL;}return p;
}
int main(int argc,char** argv){
 if(argc!=4){fprintf(stderr,"usage: %s regular.ttf bold.ttf output.bgra\n",argv[0]);return 2;}
 size_t nr=0,nb=0;unsigned char* r=read_bytes(argv[1],&nr);unsigned char* b=read_bytes(argv[2],&nb);
 if(!r||!b){free(r);free(b);return 2;}
 const char* html="<html><body style=\"background-color:#f3f6fa;padding-top:24px;padding-left:24px\"><div style=\"width:400px;background-color:#ffffff;padding-top:16px;padding-left:16px;padding-right:16px;padding-bottom:16px\"><h1 style=\"font-size:24px;line-height:36px;font-weight:700;color:#244466\">Iniciar sesión</h1><p style=\"margin-bottom:6px\">Usuario</p><input type=\"text\" value=\"Ana García\" placeholder=\"Usuario\" style=\"width:376px;padding-left:10px;padding-right:10px;padding-top:6px;padding-bottom:6px;border-width:1px;border-style:solid;border-color:#9aaabb;margin-bottom:10px\"><p style=\"margin-bottom:6px\">Contraseña</p><input type=\"password\" value=\"Ejemplo123\" placeholder=\"Clave\" style=\"width:376px;padding-left:10px;padding-right:10px;padding-top:6px;padding-bottom:6px;border-width:1px;border-style:solid;border-color:#9aaabb;margin-bottom:10px\"><input type=\"text\" placeholder=\"Código opcional\" style=\"width:376px;padding-left:10px;padding-right:10px;padding-top:6px;padding-bottom:6px;border-width:1px;border-style:solid;border-color:#9aaabb;margin-bottom:12px\"><button type=\"button\" style=\"width:376px;padding-left:10px;padding-right:10px;padding-top:8px;padding-bottom:8px;background-color:#244466;color:#ffffff;font-weight:700\">Entrar</button></div></body></html>";
 aether_html_ltr_options options={sizeof(options),AETHER_HTML_LTR_PROFILE_STATIC_CONTROLS_2,r,nr,b,nb,{0,0,0,0}};
 aether_surface_t surface=NULL;aether_status_t status=aether_surface_create(640,400,&surface);
 if(status==AETHER_OK)status=aether_surface_render_html_ltr(surface,html,strlen(html),&options);
 free(r);free(b); /* Inputs can be released before reading confirmed pixels. */
 aether_surface_frame_info info={0};info.struct_size=sizeof(info);
 if(status==AETHER_OK)status=aether_surface_copy_frame(surface,&info,NULL,0);
 unsigned char* pixels=status==AETHER_OK?(unsigned char*)malloc((size_t)info.byte_count):NULL;
 if(status==AETHER_OK&&!pixels)status=AETHER_ERROR_OUT_OF_MEMORY;
 if(status==AETHER_OK)status=aether_surface_copy_frame(surface,&info,pixels,(size_t)info.byte_count);
 if(status==AETHER_OK){FILE* out=fopen(argv[3],"wb");if(!out)status=AETHER_ERROR_INVALID_STATE;else{if(fwrite(pixels,1,(size_t)info.byte_count,out)!=(size_t)info.byte_count)status=AETHER_ERROR_INVALID_STATE;fclose(out);}}
 free(pixels);aether_surface_destroy(surface);
 if(status!=AETHER_OK){fprintf(stderr,"render/copy failed: %d\n",status);return 1;}return 0;
}
