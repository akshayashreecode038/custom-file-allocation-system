#include <stdarg.h>
#define main fas_cli_main
#include "main.c"
#undef main
static System webSystem;
static int webReady;
static int out(char *b,size_t cap,size_t *used,const char *fmt,...){
 int n;va_list a;va_start(a,fmt);n=vsnprintf(b+*used,cap-*used,fmt,a);va_end(a);
 if(n<0||(size_t)n>=cap-*used)return 0;*used+=(size_t)n;return 1;
}
int web_init(int blocks){if(blocks<8||blocks>MAX_BLOCKS)return 0;initializeDisk(&webSystem,blocks);webReady=1;return 1;}
int web_create(const char *name,int size,int method){if(!webReady)web_init(DEFAULT_BLOCKS);return (int)allocateFile(&webSystem,name,size,(AllocMethod)method,NULL);}
int web_delete(const char *name){int n=-1;if(!webReady)web_init(DEFAULT_BLOCKS);return deleteFileByName(&webSystem,name,&n)?n:-1;}
int web_state(char *b,size_t cap){
 size_t u=0;int i,k,owner,freeBlocks;if(!b||cap<2)return 0;if(!webReady)web_init(DEFAULT_BLOCKS);
 freeBlocks=countFreeBlocks(&webSystem.disk);
 if(!out(b,cap,&u,"{\"totalBlocks\":%d,\"fileCount\":%d,\"freeBlocks\":%d,\"usedBlocks\":%d,\"largestFreeRun\":%d,\"allocFailures\":%d,\"blocks\":[",webSystem.disk.totalBlocks,webSystem.fileCount,freeBlocks,webSystem.disk.totalBlocks-freeBlocks,largestFreeRun(&webSystem.disk,NULL),webSystem.allocFailures))return 0;
 for(i=0;i<webSystem.disk.totalBlocks;i++){owner=webSystem.disk.blocks[i].owner;
  if(!out(b,cap,&u,"%s{\"index\":%d,\"owner\":",i?",":"",i))return 0;
  if(owner==FREE_BLOCK){if(!out(b,cap,&u,"null,\"isIndex\":false}"))return 0;}
  else if(!out(b,cap,&u,"\"%s\",\"isIndex\":%s}",webSystem.files[owner].name,webSystem.disk.blocks[i].isIndex?"true":"false"))return 0;
 }
 if(!out(b,cap,&u,"],\"files\":["))return 0;k=0;
 for(i=0;i<MAX_FILES;i++){File *f=&webSystem.files[i];int n;if(!f->inUse)continue;
  if(!out(b,cap,&u,"%s{\"name\":\"%s\",\"size\":%d,\"method\":\"%s\",\"indexBlock\":%d,\"blocks\":[",k++?",":"",f->name,f->sizeBlocks,methodName(f->method),f->indexBlock))return 0;
  for(n=0;n<f->blockCount;n++)if(!out(b,cap,&u,"%s%d",n?",":"",f->blocks[n]))return 0;
  if(!out(b,cap,&u,"]}"))return 0;
 }
 if(!out(b,cap,&u,"]}"))return 0;return (int)u;
}
