/* SPDX-License-Identifier: MIT */
#include "profile.h"
#include <string.h>
static uint32_t u32(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
int PSProfileLevel(const PSProfile *p,uint32_t id){
 if(!p||p->native_count>PS_MAX_NATIVE||p->route_count>PS_MAX_ROUTES)return -1;
 for(unsigned i=0;i<p->native_count;i++)if(p->native_ids[i]==id)return (int)i;
 uint64_t first=1800000000ULL+256ULL*p->ordinal;
 if(id>=first&&(uint64_t)id-first<p->route_count){unsigned i=(unsigned)((uint64_t)id-first);if(p->route[i].visual==id)return (int)(p->native_count+i);}
 for(unsigned i=0;i<p->route_count;i++)if(p->route[i].visual==id)return (int)(p->native_count+i);
 return -1;
}
int PSProfileValidate(const PSProfile *p){
 if(!p||!p->ordinal||p->ordinal>1000000||(p->selector!=20&&p->selector!=30&&p->selector!=32)||!p->route_count||p->route_count>PS_MAX_ROUTES||!p->native_count||p->native_count>PS_MAX_NATIVE)return 0;
 size_t n=0;while(n<128&&p->carbin_entry[n])n++;
 if(!n||n>=128||strstr(p->carbin_entry,"..")||strchr(p->carbin_entry,'\\')||strchr(p->carbin_entry,':')||p->carbin_entry[0]=='/'||n<7||strcmp(p->carbin_entry+n-7,".carbin"))return 0;
 for(size_t i=0;i<n;i++)if((unsigned char)p->carbin_entry[i]<32||(unsigned char)p->carbin_entry[i]>126)return 0;
 for(unsigned i=0;i<p->native_count;i++){
  if(!p->native_ids[i]||p->native_ids[i]>=1800000000u)return 0;
  for(unsigned j=0;j<i;j++)if(p->native_ids[i]==p->native_ids[j])return 0;
 }
 uint64_t first=1800000000ULL+256ULL*p->ordinal;
 if(first+255>0x7fffffffULL)return 0;
 for(unsigned i=0;i<p->route_count;i++){
  const PSRoute *r=p->route+i;int native=0;
  for(unsigned j=0;j<p->native_count;j++)if(r->input==p->native_ids[j])native=1;
  if(!native||r->visual<first||r->visual>=first+256||(r->body!=PS_ANY&&r->body>=1800000000u)||(r->bumper!=PS_ANY&&r->bumper>=1800000000u))return 0;
  for(unsigned j=0;j<i;j++){
   const PSRoute *s=p->route+j;
   if(r->visual==s->visual)return 0;
   if(r->input==s->input&&(r->body==s->body||r->body==PS_ANY||s->body==PS_ANY)&&(r->bumper==s->bumper||r->bumper==PS_ANY||s->bumper==PS_ANY))return 0;
  }
 }
 return 1;
}
int PSProfileParse(const void *data,size_t size,PSProfile *p){
 if(!data||!p||size<184)return 0;const unsigned char *b=data;
 if(memcmp(b,"PLSYNC03",8))return 0;
 memset(p,0,sizeof(*p));p->ordinal=u32(b+8);p->selector=u32(b+12);p->route_count=u32(b+16);p->native_count=u32(b+20);
 if(p->route_count>PS_MAX_ROUTES||p->native_count>PS_MAX_NATIVE||size!=184+4ULL*p->native_count+16ULL*p->route_count)return 0;
 memcpy(p->carbin_sha256,b+24,32);memcpy(p->carbin_entry,b+56,128);size_t off=184;
 for(unsigned i=0;i<p->native_count;i++,off+=4)p->native_ids[i]=u32(b+off);
 for(unsigned i=0;i<p->route_count;i++,off+=16)p->route[i]=(PSRoute){u32(b+off),u32(b+off+4),u32(b+off+8),u32(b+off+12)};
 return PSProfileValidate(p);
}
int PSRouteProfile(const PSProfile *p,const void *source,const int32_t order[49],void *dest){
 if(!p||!source||!dest||!order||!p->native_count||p->native_count>PS_MAX_NATIVE||!p->route_count||p->route_count>PS_MAX_ROUTES||(p->selector!=20&&p->selector!=30&&p->selector!=32))return -1;
 uint64_t seen[2]={0,0};
 for(unsigned i=0;i<49;i++){if(order[i]<0||order[i]>=100)return -1;unsigned bit=(unsigned)order[i];uint64_t mask=1ULL<<(bit&63);if(seen[bit>>6]&mask)return -1;seen[bit>>6]|=mask;}
 unsigned char tmp[404];memcpy(tmp,source,404);uint32_t body,bumper,input;memcpy(&body,tmp+4+4*order[2],4);memcpy(&bumper,tmp+4+4*order[34],4);memcpy(&input,tmp+4+4*order[p->selector],4);int found=-1;
 /* A natural scene lifecycle pass may reuse the previously routed render copy. */
 for(unsigned i=0;i<p->route_count;i++)if(p->route[i].visual==input){input=p->route[i].input;break;}
 for(unsigned i=0;i<p->route_count;i++){const PSRoute *r=p->route+i;if((r->body==PS_ANY||r->body==body)&&(r->bumper==PS_ANY||r->bumper==bumper)&&r->input==input){if(found>=0)return -1;found=(int)i;}}
 if(found>=0)memcpy(tmp+4+4*order[p->selector],&p->route[found].visual,4);
 memcpy(dest,tmp,404);return found>=0?1:0;
}
