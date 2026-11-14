/* SPDX-License-Identifier: MIT. Exact profile signature, read-only. */
#include "profile_scene.h"
#include <string.h>
static int get(PSRead r,void *c,uintptr_t p,void *v,size_t n){return p>=65536&&p<=0x7fffffffffffULL&&n<=0x800000000000ULL-p&&r(p,v,n,c);}
static int match(uintptr_t part,uintptr_t native,uintptr_t shadow,const PSProfile *p,PSRead r,void *c){
 uintptr_t vt=0,head=0,links[2],node,previous;uint32_t type=0;
 if(!get(r,c,part,&vt,8)||(vt!=native&&vt!=shadow)||!get(r,c,part+0x20,&type,4)||type!=p->selector)return 0;
 unsigned char predicate=0;if(!get(r,c,part+0x24,&predicate,1)||predicate!=1||!get(r,c,part+0xe8,&head,8)||!get(r,c,head,links,16))return 0;
 unsigned char seen[PS_MAX_LEVELS]={0};unsigned count=p->native_count+p->route_count;node=links[0];previous=head;
 for(unsigned i=0;i<count;i++){
  uintptr_t next[2];uint32_t id=0;unsigned char entry[0x34];
  if(node==head||!get(r,c,node,entry,sizeof(entry)))return 0;
  memcpy(next,entry,16);memcpy(&id,entry+0x30,4);if(next[1]!=previous)return 0;
  int index=PSProfileLevel(p,id);if(index<0||(unsigned)index>=count||seen[index])return 0;seen[index]=1;previous=node;node=next[0];
 }
 uintptr_t end[2],again=0;
 return node==head&&previous==links[1]&&get(r,c,head,end,16)&&!memcmp(end,links,16)&&get(r,c,part+0xe8,&again,8)&&again==head;
}
int PSFindProfilePart(uintptr_t scene,uintptr_t native,uintptr_t shadow,const PSProfile *p,PSRead r,void *c,uintptr_t *part,uintptr_t *control){
 if(part)*part=0;if(control)*control=0;
 if(!r||!p||!p->native_count||p->native_count>PS_MAX_NATIVE||!p->route_count||p->route_count>PS_MAX_ROUTES)return 0;
 uintptr_t span[2],again[2],list[1024];if(!get(r,c,scene+0x5d0,span,16)||span[1]<=span[0]||span[1]-span[0]>sizeof(list)||(span[1]-span[0])%16)return 0;
 size_t n=span[1]-span[0];if(!get(r,c,span[0],list,n))return 0;
 unsigned found=0;uintptr_t result=0,owner=0;
 for(size_t i=0;i<n/8;i+=2)if(match(list[i],native,shadow,p,r,c)){if(++found>1)return 0;result=list[i];owner=list[i+1];}
 if(found!=1||!get(r,c,scene+0x5d0,again,16)||memcmp(span,again,16))return 0;
 if(part)*part=result;if(control)*control=owner;return 1;
}
