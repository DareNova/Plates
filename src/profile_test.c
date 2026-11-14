/* SPDX-License-Identifier: MIT. End-to-end embedded profile and ABI tests. */
#include "profile_monitor.c"
#define CHECK(x) do{if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
extern void ps_test_return(void);
extern void ps_test_initial_return(void);
extern void ps_test_draw_return(void);
extern void ps_test_cache_return(void);
__attribute__((naked)) static uint32_t native_style(void *p __attribute__((unused)),void *s __attribute__((unused)),void *fn __attribute__((unused))){__asm__("push %rbp\nsub $32,%rsp\nmov %rdx,%rbp\ncall *%r8\n.globl ps_test_return\nps_test_return:\nadd $32,%rsp\npop %rbp\nret");}
__attribute__((naked)) static uint32_t native_initial(void *p __attribute__((unused)),void *s __attribute__((unused)),void *fn __attribute__((unused))){__asm__("push %rbp\npush %r15\nsub $40,%rsp\nmov %rdx,%r15\nmov $65536,%ebp\ncall *%r8\n.globl ps_test_initial_return\nps_test_initial_return:\nadd $40,%rsp\npop %r15\npop %rbp\nret");}
__attribute__((naked)) static uint32_t native_draw(void *p __attribute__((unused)),void *s __attribute__((unused)),void *fn __attribute__((unused))){__asm__("push %rbp\npush %r15\npush %r14\nsub $128,%rsp\nlea 32(%rsp),%rbp\nmov %rdx,64(%rbp)\nmov %rdx,%r14\nmov $65536,%r15d\ncall *%r8\n.globl ps_test_draw_return\nps_test_draw_return:\nadd $128,%rsp\npop %r14\npop %r15\npop %rbp\nret");}
__attribute__((naked)) static uint32_t native_cache(void *p __attribute__((unused)),void *s __attribute__((unused)),void *fn __attribute__((unused))){__asm__("push %rbp\npush %r15\npush %r14\nsub $320,%rsp\nlea 32(%rsp),%rbp\nmov %rdx,240(%rbp)\nmov $65536,%r14d\nmov $65536,%r15d\ncall *%r8\n.globl ps_test_cache_return\nps_test_cache_return:\nadd $320,%rsp\npop %r14\npop %r15\npop %rbp\nret");}
static unsigned getter_calls;static uint32_t getter_value;
static uint32_t getter(void *p){(void)p;getter_calls++;return getter_value;}
static void put(void *p,size_t off,uintptr_t v){memcpy((BYTE*)p+off,&v,8);}
static void put32(void *p,size_t off,uint32_t v){memcpy((BYTE*)p+off,&v,4);}
static unsigned setup_count,completion_ok;static int mock_fail;static BYTE mock_level[0x40];static const PSProfile *mock_profile;static BYTE *mock_part;
static void mock_setup(void *scene,const void *setup,void *completion){
 setup_count++;completion_ok=completion&&*(uintptr_t*)completion==0;
 if(mock_fail)return;
 memcpy((BYTE*)scene+0x224,setup,404);uint32_t selected;memcpy(&selected,(const BYTE*)setup+4+4*order[mock_profile->selector],4);put32(mock_level,0x20,selected);
 /* Native setup re-enters the regular selector callback. It must not recurse
    into another refresh or change the already routed copy. */
 native_style(mock_part,scene,(void*)&bridge);
}
static int exercise_load_refresh(PSProfile *p,BYTE *scene,BYTE *part){
 put(scene,0,base+0x6a9dae0);scene[0xa9]=scene[0xaa]=3;put(part,0x100,(uintptr_t)mock_level);
 mock_profile=p;mock_part=part;mock_fail=0;scene_setup=mock_setup;draw_return=(uintptr_t)&ps_test_draw_return;cache_return=(uintptr_t)&ps_test_cache_return;load_refresh_enabled=1;
 BYTE input[404],expected[404];memset(input,0xa5,404);put32(input,4+4*order[2],p->route[0].body==PS_ANY?555:p->route[0].body);put32(input,4+4*order[34],p->route[0].bumper==PS_ANY?666:p->route[0].bumper);put32(input,4+4*order[p->selector],p->route[0].input);CHECK(PSRouteProfile(p,input,order,expected)==1);memcpy(scene+0x224,input,404);
 unsigned before=setup_count,getters=getter_calls;LONG applied=refresh_applied;
 scene[0xa9]=0;CHECK(native_draw(part,scene,(void*)&bridge)==p->selector);CHECK(setup_count==before&&!memcmp(scene+0x224,input,404));scene[0xa9]=3;
 CHECK(native_draw(part,scene,(void*)&bridge)==p->selector);CHECK(setup_count==before+1&&completion_ok&&refresh_applied==applied+1);CHECK(getter_calls==getters+3);CHECK(!memcmp(scene+0x224,expected,404));
 native_draw(part,scene,(void*)&bridge);CHECK(setup_count==before+1);
 /* A later native reset, e.g. returning to a cached car, can be refreshed again. */
 memcpy(scene+0x224,input,404);native_draw(part,scene,(void*)&bridge);CHECK(setup_count==before+2&&!memcmp(scene+0x224,expected,404));
 memcpy(scene+0x224,input,404);load_refresh_enabled=0;native_draw(part,scene,(void*)&bridge);CHECK(setup_count==before+2&&!memcmp(scene+0x224,input,404));load_refresh_enabled=1;
 native_draw(part,(void*)65536,(void*)&bridge);CHECK(setup_count==before+2);
 /* Failed postcheck suppresses repeat attempts rather than spamming native setup. */
 mock_fail=1;LONG failed=refresh_failed_count;native_draw(part,scene,(void*)&bridge);CHECK(setup_count==before+3&&records[0].refresh_failed&&refresh_failed_count==failed+1);native_draw(part,scene,(void*)&bridge);CHECK(setup_count==before+3);
 records[0].refresh_failed=0;mock_fail=0;put(part,0x100,0);
 put(part,0x100,(uintptr_t)mock_level);scene[0xa9]=4;scene[0xaa]=3;memcpy(scene+0x224,expected,404);put32(mock_level,0x20,p->route[0].input);before=setup_count;
 native_cache(part,scene,(void*)&bridge);CHECK(setup_count==before+1&&refresh_stage==2&&refresh_reason==7&&!memcmp(scene+0x224,expected,404));
 /* A correct render selector with an old actual level must still replay. */
 scene[0xa9]=scene[0xaa]=3;put32(mock_level,0x20,p->route[0].input);before=setup_count;native_cache(part,scene,(void*)&bridge);CHECK(setup_count==before+1);
 for(unsigned i=0;i<p->route_count;i++){
  memset(input,0xa5,404);put32(input,4+4*order[2],p->route[i].body==PS_ANY?555:p->route[i].body);put32(input,4+4*order[34],p->route[i].bumper==PS_ANY?666:p->route[i].bumper);put32(input,4+4*order[p->selector],p->route[i].input);CHECK(PSRouteProfile(p,input,order,expected)==1);memcpy(scene+0x224,input,404);put32(mock_level,0x20,0);before=setup_count;getters=getter_calls;
  CHECK(native_cache(part,scene,(void*)&bridge)==p->selector);CHECK(setup_count==before+1&&getter_calls==getters+2&&!memcmp(scene+0x224,expected,404));
 }
 put(part,0x100,0);printf("PASS load/cache refresh: saved-frame ABI, all profile routes, pending-ready state, actual-level mismatch, idempotence/reentry/reset/disabled/wrong-scene/failure guards\n");return 0;
}
static int exercise_auto_policy(void){
 PSAutoState state={0};CHECK(!auto_due(&state,1000,111,1,0));CHECK(!auto_due(&state,1099,111,1,0));CHECK(auto_due(&state,1100,111,1,0));CHECK(!auto_due(&state,1349,111,1,0));CHECK(auto_due(&state,1350,111,1,0));CHECK(!auto_due(&state,1400,222,1,0));CHECK(auto_due(&state,1500,222,1,0));CHECK(!auto_due(&state,2000,222,1,1));CHECK(!auto_due(&state,2010,0,1,0));CHECK(!auto_due(&state,2100,222,1,0));CHECK(auto_due(&state,2200,222,1,0));CHECK(!auto_due(&state,3000,333,0,0));
 puts("PASS activation policy: 100ms eligibility, bounded fast retries, new-token retry reset, attached/unsupported/disabled states");return 0;
}
static int exercise(PSProfile *p){
 static BYTE scene[0x600],part[0x118],nodes[PS_MAX_LEVELS+1][0x40];static uintptr_t vector[2];
 memset(scene,0,sizeof(scene));memset(part,0,sizeof(part));memset(nodes,0,sizeof(nodes));vector[0]=(uintptr_t)part;vector[1]=0;
 put(scene,0x5d0,(uintptr_t)vector);put(scene,0x5d8,(uintptr_t)(vector+2));put(part,0,(uintptr_t)(shadow+1));put32(part,0x20,p->selector);part[0x24]=1;put(part,0xe8,(uintptr_t)nodes);
 unsigned count=p->native_count+p->route_count;
 for(unsigned i=0;i<=count;i++){put(nodes[i],0,(uintptr_t)nodes[(i+1)%(count+1)]);put(nodes[i],8,(uintptr_t)nodes[(i+count)%(count+1)]);if(i)put32(nodes[i],0x30,i<=p->native_count?p->native_ids[i-1]:p->route[i-1-p->native_count].visual);}
 original_getter=getter;getter_value=p->selector;enabled=1;record_count=1;records[0]=(Registration){.lease=REG_LIVE,.part=(uintptr_t)part,.profile=p,.scene=(uintptr_t)scene};expected_return=(uintptr_t)&ps_test_return;initial_return=(uintptr_t)&ps_test_initial_return;
 CHECK(PSFindProfilePart((uintptr_t)scene,original_vt,(uintptr_t)(shadow+1),p,read_local,NULL,NULL,NULL));
 for(unsigned i=0;i<p->route_count;i++){
  BYTE input[404],expected[404];memset(input,0xa5,404);put32(input,4+4*order[2],p->route[i].body==PS_ANY?555:p->route[i].body);put32(input,4+4*order[34],p->route[i].bumper==PS_ANY?666:p->route[i].bumper);put32(input,4+4*order[p->selector],p->route[i].input);memcpy(expected,input,404);put32(expected,4+4*order[p->selector],p->route[i].visual);memcpy(scene+0x224,input,404);unsigned before=getter_calls;
  CHECK(native_style(part,scene,(void*)&bridge)==p->selector);CHECK(getter_calls==before+1);CHECK(!memcmp(scene+0x224,expected,404));
  memcpy(scene+0x224,input,404);before=getter_calls;
  CHECK(native_initial(part,scene,(void*)&bridge)==p->selector);CHECK(getter_calls==before+1);CHECK(!memcmp(scene+0x224,expected,404));
  LONG routes_before=routes;CHECK(native_initial(part,scene,(void*)&bridge)==p->selector);CHECK(routes==routes_before);CHECK(!memcmp(scene+0x224,expected,404));
 }
 /* Previously generated IDs can be rerouted if bodywork changes independently. */
 if(p->route_count>1){BYTE input[404],expected[404];memset(input,0xa5,404);put32(input,4+4*order[2],p->route[0].body==PS_ANY?555:p->route[0].body);put32(input,4+4*order[34],p->route[0].bumper==PS_ANY?666:p->route[0].bumper);put32(input,4+4*order[p->selector],p->route[0].visual);CHECK(PSRouteProfile(p,input,order,expected)==1);CHECK(!memcmp(input,expected,404));}
 CHECK(!exercise_load_refresh(p,scene,part));
 BYTE saved[404];memcpy(saved,scene+0x224,404);expected_return++;CHECK(native_style(part,scene,(void*)&bridge)==p->selector);CHECK(!memcmp(saved,scene+0x224,404));expected_return--;
 put32(nodes[1],0x30,0x7fffffff);CHECK(native_style(part,scene,(void*)&bridge)==p->selector);CHECK(!memcmp(saved,scene+0x224,404));
 enabled=0;CHECK(native_style(part,scene,(void*)&bridge)==p->selector);CHECK(!memcmp(saved,scene+0x224,404));
 printf("PASS ABI/scene/routes: ordinal=%lu selector=%lu routes=%lu native=%lu; RBP menu and R15 initialization bridges, original getter once, repeated lifecycle calls idempotent, wrong caller/signature/off untouched\n",(unsigned long)p->ordinal,(unsigned long)p->selector,(unsigned long)p->route_count,(unsigned long)p->native_count);return 0;
}
static void quiet_log(const char *format,...){(void)format;}
int main(int argc,char **argv){
 BYTE holder[0xf0]={0},objects[2][16]={0},controls[2][32]={0};
 put(objects[0],0,base+0x64da9f8);put(objects[1],0,base+0x64da9f8);put(controls[0],0,base+0x6a71d60);put(controls[1],0,base+0x6a71d60);put(controls[0],0x10,(uintptr_t)objects[0]);put(controls[1],0x10,(uintptr_t)objects[1]);
 CHECK(!locator_resource((uintptr_t)holder));
 for(unsigned i=0;i<5;i++){memset(holder,0,sizeof(holder));put(holder,0x80+16*i,(uintptr_t)objects[0]);put(holder,0x88+16*i,(uintptr_t)controls[0]);CHECK(locator_resource((uintptr_t)holder)==(uintptr_t)objects[0]);}
 put(controls[0],0x10,65536);CHECK(!locator_resource((uintptr_t)holder));put(controls[0],0x10,(uintptr_t)objects[0]);put(holder,0x80,(uintptr_t)objects[1]);put(holder,0x88,(uintptr_t)controls[1]);CHECK(locator_resource((uintptr_t)holder)==(uintptr_t)objects[1]);
 memset(holder,0,sizeof(holder));put(holder,0xd0,(uintptr_t)objects[0]);put(holder,0xd8,(uintptr_t)controls[0]);CHECK(!locator_resource((uintptr_t)holder));
 puts("PASS all five locator slots, empty/bad-control rejection, first-slot compatibility and array bounds");
 CHECK(!exercise_auto_policy());
 CHECK(argc==3||argc==4);wchar_t path[1024];CHECK(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,argv[1],-1,path,1024));PSProfile actual;
 CHECK(PSProfileLoad(path,&actual));CHECK(PSProfileBind(path,&actual));CHECK(actual.ordinal==1282&&actual.route_count==42);
 void *data=NULL;size_t n=0;CHECK(PSZipRead(path,PS_PROFILE_ENTRY,&data,&n,8192));PSProfile parsed;CHECK(PSProfileParse(data,n,&parsed));CHECK(!PSProfileParse(data,n-1,&parsed));((BYTE*)data)[0]^=1;CHECK(!PSProfileParse(data,n,&parsed));HeapFree(GetProcessHeap(),0,data);
 PSProfile bad=actual;bad.carbin_sha256[0]^=1;CHECK(!PSProfileBind(path,&bad));bad=actual;bad.native_ids[1]=bad.native_ids[0];CHECK(!PSProfileValidate(&bad));bad=actual;bad.route[1]=bad.route[0];CHECK(!PSProfileValidate(&bad));bad=actual;strcpy(bad.carbin_entry,"../evil.carbin");CHECK(!PSProfileValidate(&bad));bad=actual;bad.selector=99;CHECK(!PSProfileValidate(&bad));
 puts("PASS real exported ZIP profile: parser, CRC, carbin SHA binding; bad length/schema, duplicate IDs/routes, traversal and selector refused");
 CHECK(!exercise(&actual));
 for(unsigned selector=20;selector<=32;selector+=selector==20?10:2){
  PSProfile p={0};p.ordinal=99999+selector;p.selector=selector;p.route_count=3;p.native_count=2;strcpy(p.carbin_entry,"SYNTH.carbin");p.native_ids[0]=900000+selector;p.native_ids[1]=900100+selector;uint32_t first=1800000000u+p.ordinal*256u;
  p.route[0]=(PSRoute){11,21,p.native_ids[0],first};p.route[1]=(PSRoute){11,22,p.native_ids[0],first+1};p.route[2]=(PSRoute){PS_ANY,PS_ANY,p.native_ids[1],first+2};CHECK(PSProfileValidate(&p));CHECK(!exercise(&p));
 }
 CHECK(routes==102);CHECK(initial_routes==51);puts("PASS no compiled 240SX mapping required: independent ordinal/count/native IDs and flywheel/clutch/driveline selectors");
 wchar_t root[1024],fixture[1024];CHECK(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,argv[2],-1,root,1024));
 const wchar_t *invalid[]={L"bad_deflate.zip",L"duplicate_entry.zip",L"bad_crc.zip",L"bad_eocd.zip"};
 for(unsigned i=0;i<4;i++){swprintf(fixture,1024,L"%ls\\%ls",root,invalid[i]);CHECK(!PSProfileLoad(fixture,&parsed));}
 index_profiles(root,quiet_log);CHECK(stored_count==2);CHECK(!by_ordinal(1282));CHECK(by_ordinal(999));CHECK(PSProfileBind(by_ordinal(999)->path,&by_ordinal(999)->profile));
 puts("PASS ZIP/index guards: malformed Deflate/duplicate/CRC/EOCD errors rejected; duplicate ordinals blocked, independent valid profile indexed");
 if(argc==4){CHECK(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,argv[3],-1,path,1024));PSProfile second;CHECK(PSProfileLoad(path,&second));CHECK(PSProfileBind(path,&second));CHECK(second.ordinal!=actual.ordinal);CHECK(!exercise(&second));puts("PASS second real workshop export: independent car ZIP loads, binds and routes without DLL rebuild");}
 return 0;
}
