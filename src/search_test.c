/* SPDX-License-Identifier: MIT. Synthetic memory only; never attaches to a game. */
#define main profile_regression_main
#include "profile_test.c"
#undef main
#include "bench_fixture.h"
static void quiet_search(const char *message,...){(void)message;}
int main(void){
 PSProfile p={0};p.ordinal=1150;p.selector=32;p.native_count=1;p.native_ids[0]=552000;p.route_count=1;
 strcpy(p.carbin_entry,"SYNTH.carbin");p.route[0]=(PSRoute){PS_ANY,PS_ANY,552000,1800294400};
 base=0x100000000ULL;prepare_bench(&p);
 /* Reserve address space, not 512 MiB of RAM. Commit two pages only. */
 BYTE *arena=VirtualAlloc(NULL,512ULL*1024*1024,MEM_RESERVE,PAGE_NOACCESS);CHECK(arena);
 BYTE *distant=VirtualAlloc(arena+0x100000,65536,MEM_COMMIT,PAGE_READWRITE);CHECK(distant);
 memcpy(distant,bench_part,sizeof(bench_part));put(distant,0,original_vt);bench_vector[0]=(uintptr_t)distant;
 uintptr_t token=(uintptr_t)arena+162ULL*1024*1024,low=(token&~(uintptr_t)0xfffff)-0x4000000;
 CHECK(!census(low,low+0x8000000,&p));
 uintptr_t scene=0;unsigned steps=0;
 while(!(scene=lookup_scene(token,123,&p,quiet_search))&&lookup.busy&&steps++<10000){}
 CHECK(scene==(uintptr_t)bench_scene&&steps<10000&&!lookup.busy);
 LONG before_steps=search_steps;unsigned long long before_bytes=search_bytes;
 for(unsigned i=0;i<1000;i++)CHECK(lookup_scene(token,123,&p,quiet_search)==scene);
 CHECK(search_steps==before_steps&&search_bytes==before_bytes);
 puts("PASS distant setup beyond old +/-64MiB window; cached discovery performs no scans");
 put(bench_scene,0,0);CHECK(!lookup_scene(token,123,&p,quiet_search));CHECK(!lookup.scene);
 put(bench_scene,0,base+0x6a9dae0);
 do{scene=lookup_scene(token,123,&p,quiet_search);}while(!scene&&lookup.busy);CHECK(scene==(uintptr_t)bench_scene);
 puts("PASS stale cached scene type rejected and rediscovered");
 /* A new owner must not reuse a valid scene from the previous owner. */
 lookup.retry=GetTickCount64()+10000;lookup.owner=456;lookup.scene=scene;
 CHECK(lookup_scene(token,123,&p,quiet_search)==scene);CHECK(lookup.owner==123);
 /* Check exact profile rejection and multiple-scene rejection in a sparse scope. */
 put32(distant,0x20,30);SearchCursor scan;search_begin(&scan,(uintptr_t)distant,(uintptr_t)distant+65536);
 int result;do{result=search_slice(&scan,&p);}while(!result);CHECK(result==-1);
 put32(distant,0x20,32);
 static BYTE other_scene[0x600];static uintptr_t other_vector[2];memcpy(other_scene,bench_scene,sizeof(other_scene));
 BYTE *second=distant+0x1000;memcpy(second,distant,sizeof(bench_part));other_vector[0]=(uintptr_t)second;
 put(other_scene,0x5d0,(uintptr_t)other_vector);put(other_scene,0x5d8,(uintptr_t)(other_vector+2));put(second,0x60,(uintptr_t)other_scene);
 search_begin(&scan,(uintptr_t)distant,(uintptr_t)distant+65536);do{result=search_slice(&scan,&p);}while(!result);CHECK(result==-2);
 puts("PASS wrong selector and ambiguous matching scenes refused");
 /* A matching candidate near the start is not accepted before the scope ends. */
 memset(second,0,sizeof(bench_part));search_begin(&scan,(uintptr_t)distant,(uintptr_t)distant+65536+SEARCH_BYTES);
 result=search_slice(&scan,&p);CHECK(result==1||result==0);if(!result)CHECK(scan.cursor<scan.high);
 CHECK(VirtualFree(arena,0,MEM_RELEASE));
 puts("PASS sparse/unreadable pages and resumable scope traversal");
 return 0;
}
