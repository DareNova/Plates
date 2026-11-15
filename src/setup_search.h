/* SPDX-License-Identifier: MIT. Worker-only, resumable setup discovery.
   Cached attachments do not scan. Expanded searches use private writable pages,
   finish a complete scope before accepting a unique exact profile signature,
   and retain their cursor across small byte/time slices. */
#pragma once
#define SEARCH_BYTES (8u*1024u*1024u)
#define SEARCH_SCRATCH (256u*1024u)
typedef struct {uintptr_t low,high,cursor,found;int ambiguous;} SearchCursor;
typedef struct {uintptr_t token,owner,scene;const PSProfile *profile;SearchCursor scan;unsigned phase;int busy;ULONGLONG retry;} SetupLookup;
static SetupLookup lookup;
static BYTE *search_scratch;
static volatile LONG search_steps,search_expansions;
static unsigned long long search_bytes;

static void search_begin(SearchCursor *s,uintptr_t low,uintptr_t high){
 *s=(SearchCursor){.low=low,.high=high,.cursor=low};
}
/* 0 pending, 1 unique, -1 exhausted/unavailable, -2 ambiguous. */
static int search_slice(SearchCursor *s,const PSProfile *p){
 if(!search_scratch)search_scratch=HeapAlloc(GetProcessHeap(),0,SEARCH_SCRATCH);
 if(!search_scratch)return -1;
 size_t spent=0;unsigned queries=0;ULONGLONG started=GetTickCount64();InterlockedIncrement(&search_steps);
 while(s->cursor<s->high&&spent<SEARCH_BYTES&&queries++<128&&GetTickCount64()-started<8){
  MEMORY_BASIC_INFORMATION info;if(!VirtualQuery((void*)s->cursor,&info,sizeof(info)))return -1;
  uintptr_t end=(uintptr_t)info.BaseAddress+info.RegionSize;if(end>s->high)end=s->high;if(end<=s->cursor)return -1;
  DWORD protection=info.Protect&0xff;
  int readable=info.State==MEM_COMMIT&&info.Type==MEM_PRIVATE&&!(info.Protect&PAGE_GUARD)&&
   (protection==PAGE_READWRITE||protection==PAGE_WRITECOPY||protection==PAGE_EXECUTE_READWRITE||protection==PAGE_EXECUTE_WRITECOPY);
  if(!readable){s->cursor=end;continue;}
  size_t bytes=end-s->cursor;if(bytes>SEARCH_SCRATCH)bytes=SEARCH_SCRATCH;if(bytes>SEARCH_BYTES-spent)bytes=SEARCH_BYTES-spent;
  if(read_local(s->cursor,search_scratch,bytes,NULL)){
   search_bytes+=bytes;
   for(size_t off=0;off+8<=bytes;off+=8){
    uintptr_t table;memcpy(&table,search_scratch+off,8);if(table!=original_vt&&table!=(uintptr_t)(shadow+1))continue;
    uintptr_t scene=ptr(s->cursor+off+0x60);if(scene==s->found||ptr(scene)!=base+0x6a9dae0)continue;
    if(PSFindProfilePart(scene,original_vt,(uintptr_t)(shadow+1),p,read_local,NULL,NULL,NULL)){
     if(s->found){s->ambiguous=1;return -2;}s->found=scene;
    }
   }
  }
  spent+=bytes;s->cursor+=bytes;
 }
 if(s->cursor<s->high)return 0;
 return s->found?1:-1;
}
static void lookup_scope(SetupLookup *l){
 static const uintptr_t radii[]={0x10000000ULL,0x40000000ULL,0x100000000ULL};
 uintptr_t low,high;
 if(l->phase<3){
  uintptr_t center=l->token&~(uintptr_t)0xfffff,radius=radii[l->phase];
  low=center>radius?center-radius:0x10000;high=center+radius;
 }else{SYSTEM_INFO info;GetSystemInfo(&info);low=(uintptr_t)info.lpMinimumApplicationAddress;high=(uintptr_t)info.lpMaximumApplicationAddress+1;}
 search_begin(&l->scan,low,high);
}
static uintptr_t lookup_scene(uintptr_t token,uintptr_t owner,const PSProfile *p,PlateSyncLog log){
 if(lookup.token!=token||lookup.owner!=owner||lookup.profile!=p)lookup=(SetupLookup){.token=token,.owner=owner,.profile=p};
 if(lookup.scene&&ptr(lookup.scene)==base+0x6a9dae0&&PSFindProfilePart(lookup.scene,original_vt,(uintptr_t)(shadow+1),p,read_local,NULL,NULL,NULL))return lookup.scene;
 lookup.scene=0;
 if(!lookup.busy){
  if(GetTickCount64()<lookup.retry)return 0;
  if(token<0x4010000)return 0;
  uintptr_t low=(token&~(uintptr_t)0xfffff)-0x4000000,scene=census(low,low+0x8000000,p);
  if(scene){lookup.scene=scene;return scene;}
  lookup.phase=0;lookup_scope(&lookup);lookup.busy=1;
  log("setup_search=expanded ordinal=%lu; slices=8MiB/8ms; scope=private_writable",(unsigned long)p->ordinal);
 }
 int result=search_slice(&lookup.scan,p);
 if(!result)return 0;
 if(result==1){
  lookup.scene=lookup.scan.found;lookup.busy=0;
  log("setup_search=found ordinal=%lu phase=%u; profile_signature=unique",(unsigned long)p->ordinal,lookup.phase);return lookup.scene;
 }
 if(result==-1&&lookup.phase<3){lookup.phase++;lookup_scope(&lookup);InterlockedIncrement(&search_expansions);return 0;}
 log("setup_search=%s ordinal=%lu; retry=automatic",result==-2?"ambiguous":"exhausted",(unsigned long)p->ordinal);
 lookup.busy=0;lookup.retry=GetTickCount64()+10000;return 0;
}
