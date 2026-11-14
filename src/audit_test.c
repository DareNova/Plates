/* SPDX-License-Identifier: MIT. Audit regressions; synthetic objects only. */
#define main profile_regression_main
#include "profile_test.c"
#undef main
#include "bench_fixture.h"
int main(int argc,char **argv){
 int original=profile_regression_main(argc,argv);if(original)return original;
 wchar_t path[2048];CHECK(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,argv[1],-1,path,2048));PSProfile p;CHECK(PSProfileLoad(path,&p));
 PSProfile bad=p;
 uint32_t replaced=bad.native_ids[0];bad.native_ids[0]=0;
 for(unsigned i=0;i<bad.route_count;i++)if(bad.route[i].input==replaced)bad.route[i].input=0;
 CHECK(!PSProfileValidate(&bad));
 PSProfile reordered=p;PSRoute saved=reordered.route[0];reordered.route[0]=reordered.route[reordered.route_count-1];reordered.route[reordered.route_count-1]=saved;
 CHECK(PSProfileValidate(&reordered));
 for(unsigned i=0;i<reordered.route_count;i++)CHECK(PSProfileLevel(&reordered,reordered.route[i].visual)==(int)(reordered.native_count+i));
 BYTE input[404]={0},output[404],sentinel[404];int32_t mapped[49];memcpy(mapped,order,sizeof(mapped));
 mapped[1]=65;mapped[2]=99;
 put32(input,4+4*mapped[2],p.route[0].body==PS_ANY?555:p.route[0].body);put32(input,4+4*mapped[34],p.route[0].bumper==PS_ANY?666:p.route[0].bumper);put32(input,4+4*mapped[p.selector],p.route[0].input);
 CHECK(PSRouteProfile(&p,input,mapped,output)==1);
 memset(sentinel,0x5a,sizeof(sentinel));memcpy(output,sentinel,sizeof(output));mapped[0]=mapped[1];
 CHECK(PSRouteProfile(&p,input,mapped,output)==-1&&!memcmp(output,sentinel,sizeof(output)));
 mapped[0]=-1;CHECK(PSRouteProfile(&p,input,mapped,output)==-1);mapped[0]=100;CHECK(PSRouteProfile(&p,input,mapped,output)==-1);
 puts("PASS zero-ID rejection, reordered profile compatibility and bounded duplicate-order checks");

 prepare_bench(&p);
 uintptr_t low=(uintptr_t)bench_part,high=low+sizeof(bench_part);
 CHECK(census(low,high,&p)==(uintptr_t)bench_scene);
 put(bench_part,0,original_vt);CHECK(census(low,high,&p)==(uintptr_t)bench_scene);
 put(bench_part,0,(uintptr_t)(shadow+1));put32(bench_nodes[1],0x30,0x7fffffff);
 CHECK(!census(low,high,&p));
 puts("PASS cached shadow-table census and corrupted-scene rejection");

 for(unsigned mode=0;mode<3;mode++){
  prepare_bench(&p);put32(bench_nodes[1],0x30,0x7fffffff);
  /* A repair would be required, but the full signature is invalid. */
  put32(bench_scene,0x228+4*order[p.selector],p.route[0].input);put32(mock_level,0x20,p.route[0].input);
  BYTE before[404];memcpy(before,bench_scene+0x224,sizeof(before));
  if(mode==0)native_draw(bench_part,bench_scene,(void*)&bridge);
  else if(mode==1)native_cache(bench_part,bench_scene,(void*)&bridge);
  else native_style(bench_part,bench_scene,(void*)&bridge);
  CHECK(setup_count==0&&!memcmp(before,bench_scene+0x224,sizeof(before)));
 }
 puts("PASS every write/refresh path still requires the full current profile signature");
 return 0;
}
