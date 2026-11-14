/* Standalone steady-state benchmark. No game attachment or game writes. */
#define PS_AUDIT_READS 1
#define main profile_regression_main
#include "profile_test.c"
#undef main
#include "bench_fixture.h"
int main(int argc,char **argv){
 if(argc!=2)return 2;wchar_t path[2048];if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,argv[1],-1,path,2048))return 2;
 PSProfile profile;if(!PSProfileLoad(path,&profile)||!PSProfileBind(path,&profile))return 3;
 LARGE_INTEGER frequency;QueryPerformanceFrequency(&frequency);
 for(unsigned mode=0;mode<3;mode++){
  prepare_bench(&profile);double best=1e99;unsigned long long reads=0;const unsigned iterations=20000;
  for(unsigned pass=0;pass<3;pass++){
   audit_reads=0;LARGE_INTEGER begin,end;QueryPerformanceCounter(&begin);
   for(unsigned i=0;i<iterations;i++){
    if(mode==0)native_draw(bench_part,bench_scene,(void*)&bridge);
    else if(mode==1)native_cache(bench_part,bench_scene,(void*)&bridge);
    else native_style(bench_part,bench_scene,(void*)&bridge);
   }
   QueryPerformanceCounter(&end);double us=1e6*(double)(end.QuadPart-begin.QuadPart)/(double)frequency.QuadPart/iterations;
   if(us<best){best=us;reads=audit_reads;}
  }
  if(setup_count)return 4;
  printf("{\"case\":\"%s\",\"iterations\":%u,\"best_of\":3,\"us_per_call\":%.6f,\"memory_reads_per_call\":%.3f}\n",mode==0?"draw_no_change":mode==1?"cache_no_change":"menu_no_change",iterations,best,(double)reads/iterations);
 }
 return 0;
}
