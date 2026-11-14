/* SPDX-License-Identifier: MIT. Profile-driven native-instance callback. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>
#include <string.h>
#include "render_monitor.h"
#include "build_fingerprint.h"
#include "marker_hash.inc"
#include "profile_scene.h"
#include "profile_zip.h"
#include "auto_policy.h"
#define MAX_PROFILES 1024
#define MAX_HOOKS 32
typedef uint32_t (*Getter)(void*);
typedef struct {PSProfile profile;wchar_t path[1024];WIN32_FILE_ATTRIBUTE_DATA stamp;int blocked,bound;} Stored;
typedef struct {uintptr_t part,control;const PSProfile *profile;uintptr_t scene,token,owner;volatile LONG lease,refresh_busy,refresh_failed;unsigned ever_used;} Registration;
static Stored stored[MAX_PROFILES];static unsigned stored_count;
static Registration records[MAX_HOOKS];static volatile LONG record_count;
static uintptr_t base,original_vt,shadow[33],expected_return,initial_return;
static Getter original_getter;
static volatile LONG enabled,calls,routes,skipped,last_ordinal,last_selector,last_body,last_bumper,last_input,last_visual;
static volatile LONG initial_calls,initial_routes,unchanged;
static uintptr_t observed_callers[16];static volatile LONG observed_counts[16];
typedef void (*SceneSetup)(void*,const void*,void*);
static SceneSetup scene_setup;
static uintptr_t draw_return,cache_return;
static int load_refresh_enabled;
static volatile LONG draw_calls,refresh_requests,refresh_applied,refresh_failed_count,refresh_deferred;
static volatile LONG refresh_car,refresh_input,refresh_visual,refresh_thread;
static volatile LONG cache_calls,refresh_stage,refresh_state,refresh_reason,refresh_selected;
static void observe_caller(uintptr_t caller){
 for(unsigned i=0;i<16;i++){
  uintptr_t prior=(uintptr_t)InterlockedCompareExchangePointer((void*volatile*)(observed_callers+i),(void*)caller,NULL);
  if(!prior||prior==caller){InterlockedIncrement(observed_counts+i);return;}
 }
}
#ifdef PS_AUDIT_READS
static unsigned long long audit_reads;
#endif
static int read_local(uintptr_t p,void *out,size_t n,void *unused){
#ifdef PS_AUDIT_READS
 audit_reads++;
#endif
 (void)unused;SIZE_T got=0;return p>=65536&&ReadProcessMemory(GetCurrentProcess(),(void*)p,out,n,&got)&&got==n;
}
static uintptr_t ptr(uintptr_t p){uintptr_t v=0;read_local(p,&v,8,NULL);return v;}
#include "instance_lifetime.h"
static const int32_t order[49]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,45,46,47,48,42,43,49};
static int same_file(const Stored *s){WIN32_FILE_ATTRIBUTE_DATA now;return GetFileAttributesExW(s->path,GetFileExInfoStandard,&now)&&now.nFileSizeHigh==s->stamp.nFileSizeHigh&&now.nFileSizeLow==s->stamp.nFileSizeLow&&!memcmp(&now.ftLastWriteTime,&s->stamp.ftLastWriteTime,sizeof(FILETIME));}
static void index_profiles(const wchar_t *directory,PlateSyncLog log){
 wchar_t pattern[1024];if(swprintf(pattern,1024,L"%ls\\mediapc\\Cars\\*.zip",directory)<0)return;
 WIN32_FIND_DATAW entry;HANDLE find=FindFirstFileW(pattern,&entry);if(find==INVALID_HANDLE_VALUE){log("profile_index=no_override_car_folder");return;}
 do{
  if(entry.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)continue;
  wchar_t path[1024];if(swprintf(path,1024,L"%ls\\mediapc\\Cars\\%ls",directory,entry.cFileName)<0)continue;
  PSProfile p;if(!PSProfileLoad(path,&p))continue;
  int duplicate=0;for(unsigned i=0;i<stored_count;i++)if(stored[i].profile.ordinal==p.ordinal){stored[i].blocked=1;duplicate=1;log("profile_index=ambiguous_ordinal ordinal=%lu; all_duplicates_disabled",(unsigned long)p.ordinal);}
  if(duplicate)continue;
  if(stored_count==MAX_PROFILES){log("profile_index=capacity_reached");break;}
  Stored *s=stored+stored_count;s->profile=p;wcscpy(s->path,path);
  if(!GetFileAttributesExW(path,GetFileExInfoStandard,&s->stamp))continue;
  stored_count++;log("profile_index=loaded ordinal=%lu selector=%lu routes=%lu native_levels=%lu source=embedded_car_zip",(unsigned long)p.ordinal,(unsigned long)p.selector,(unsigned long)p.route_count,(unsigned long)p.native_count);
 }while(FindNextFileW(find,&entry));FindClose(find);log("profile_index_complete count=%u",stored_count);
}
static Stored *by_ordinal(uint32_t ordinal){for(unsigned i=0;i<stored_count;i++)if(stored[i].profile.ordinal==ordinal&&!stored[i].blocked)return stored+i;return NULL;}
static uintptr_t locator_resource(uintptr_t holder){
 /* Native base destructor 0x298d78b destroys five 16-byte shared slots
    beginning at +0x80. The selected resource is not always in slot zero. */
 for(unsigned i=0;i<5;i++){
  uintptr_t object=ptr(holder+0x80+16*i),control=ptr(holder+0x88+16*i);
  if(object&&ptr(object)==base+0x64da9f8&&ptr(control)==base+0x6a71d60&&ptr(control+0x10)==object)return object;
 }
 return 0;
}
static uintptr_t current_resource(uint32_t *ordinal,uintptr_t *owner){
 *ordinal=0;if(owner)*owner=0;uintptr_t loc=ptr(base+0xa91d6b8);if(ptr(loc)!=base+0x6817928)return 0;
 BYTE mode=0;if(!read_local(loc+0x70,&mode,1,NULL)||mode!=1)return 0;
 uintptr_t service=ptr(ptr(ptr(loc+0x78))+0x10);if(ptr(service)!=base+0x6817128)return 0;
 uintptr_t ctrl=ptr(service+0x180),car=ptr(ctrl+0xc0);uint32_t id=0;
 if(ptr(car)!=base+0x680af90||!read_local(ptr(car+0x48),&id,4,NULL)||!id)return 0;
 uintptr_t holder=ptr(service+0x170),object=locator_resource(holder);
 if(!object)return 0;
 if(ptr(service+0x180)!=ctrl||ptr(ctrl+0xc0)!=car)return 0;
 *ordinal=id;if(owner)*owner=car;return object;
}
static uintptr_t census(uintptr_t low,uintptr_t high,const PSProfile *p){
 BYTE *buffer=HeapAlloc(GetProcessHeap(),0,1048576);if(!buffer)return 0;
 ULONGLONG start=GetTickCount64();uintptr_t cursor=low,found=0;int ambiguous=0;
 while(cursor<high&&GetTickCount64()-start<3000){
  MEMORY_BASIC_INFORMATION info;if(!VirtualQuery((void*)cursor,&info,sizeof(info)))break;
  uintptr_t end=(uintptr_t)info.BaseAddress+info.RegionSize;if(end>high)end=high;if(end<=cursor)break;DWORD protection=info.Protect&0xff;
  if(info.State==MEM_COMMIT&&!(info.Protect&PAGE_GUARD)&&(protection==PAGE_READWRITE||protection==PAGE_WRITECOPY||protection==PAGE_EXECUTE_READWRITE||protection==PAGE_EXECUTE_WRITECOPY)){
   for(uintptr_t chunk=cursor;chunk<end&&GetTickCount64()-start<3000;){
    size_t bytes=end-chunk;if(bytes>1048576)bytes=1048576;
    if(read_local(chunk,buffer,bytes,NULL))for(size_t off=0;off+8<=bytes;off+=8){uintptr_t table;memcpy(&table,buffer+off,8);if(table!=original_vt&&table!=(uintptr_t)(shadow+1))continue;
     uintptr_t scene=ptr(chunk+off+0x60);if(scene==found||ptr(scene)!=base+0x6a9dae0)continue;
     if(PSFindProfilePart(scene,original_vt,(uintptr_t)(shadow+1),p,read_local,NULL,NULL,NULL)){if(found){ambiguous=1;break;}found=scene;}
    }
    if(ambiguous)break;chunk+=bytes;
   }
  }
  if(ambiguous||GetTickCount64()-start>=3000)break;cursor=end;
 }
 HeapFree(GetProcessHeap(),0,buffer);return !ambiguous&&cursor==high?found:0;
}
static void refresh_on_draw(Registration *r,uintptr_t scene,int stage){
 InterlockedExchange(&refresh_stage,stage);
 if(!load_refresh_enabled||r->refresh_failed||InterlockedCompareExchange(&r->refresh_busy,1,0)){InterlockedExchange(&refresh_reason,1);return;}
 BYTE state[2],input[404],output[404];uintptr_t part=0;
 if(scene!=r->scene||ptr(scene)!=base+0x6a9dae0){InterlockedExchange(&refresh_reason,2);InterlockedIncrement(&refresh_deferred);goto done;}
 if(!read_local(scene+0xa9,state,2,NULL)){InterlockedExchange(&refresh_reason,3);InterlockedIncrement(&refresh_deferred);goto done;}
 InterlockedExchange(&refresh_state,state[0]|state[1]<<8);
 /* Same range accepted by the native setup routine, conservatively excluding
    teardown/error states. A complete profile part/vector is still mandatory. */
 if(state[0]<1||state[0]>4||state[1]<1||state[1]>4){InterlockedExchange(&refresh_reason,3);InterlockedIncrement(&refresh_deferred);goto done;}
 if(!read_local(scene+0x224,input,sizeof(input),NULL)||PSRouteProfile(r->profile,input,order,output)!=1){InterlockedExchange(&refresh_reason,5);InterlockedIncrement(&refresh_deferred);goto done;}
 unsigned offset=4+4*order[r->profile->selector];uint32_t before,desired;memcpy(&before,input+offset,4);memcpy(&desired,output+offset,4);
 uint32_t current_level=0;uintptr_t current=ptr(r->part+0x100);if(!current)current=ptr(r->part+0xf8);
 int level_read=read_local(current+0x20,&current_level,4,NULL);InterlockedExchange(&refresh_selected,current_level);
 if(before==desired&&level_read&&current_level==desired){InterlockedExchange(&refresh_reason,6);goto done;}
 /* A no-op needs no scene mutation. Every engine setup still requires the
    complete current part/vector/level signature immediately before the call. */
 if(!PSFindProfilePart(scene,original_vt,(uintptr_t)(shadow+1),r->profile,read_local,NULL,&part,NULL)||part!=r->part){InterlockedExchange(&refresh_reason,4);InterlockedIncrement(&refresh_deferred);goto done;}
 InterlockedExchange(&refresh_car,r->profile->ordinal);InterlockedExchange(&refresh_input,before);InterlockedExchange(&refresh_visual,desired);InterlockedExchange(&refresh_thread,(LONG)GetCurrentThreadId());InterlockedIncrement(&refresh_requests);
 /* Both guarded native paths query the selector before obtaining this part's
    active models. Never run engine setup from the helper worker. */
 uintptr_t completion=0;scene_setup((void*)scene,output,&completion);
 uint32_t after=0,selected=0;uintptr_t level=ptr(part+0x100);if(!level)level=ptr(part+0xf8);
 if(read_local(scene+0x224+offset,&after,4,NULL)&&after==desired&&read_local(level+0x20,&selected,4,NULL)&&selected==desired){InterlockedIncrement(&refresh_applied);InterlockedExchange(&refresh_reason,7);}
 else{InterlockedExchange(&r->refresh_failed,1);InterlockedIncrement(&refresh_failed_count);InterlockedExchange(&refresh_reason,8);}
done:InterlockedExchange(&r->refresh_busy,0);
}
__attribute__((used,noinline)) static uint32_t ps_dispatch(void *part,void *scene,void *initial_scene,void *draw_scene){
 uint32_t value=original_getter(part);InterlockedIncrement(&calls);
 uintptr_t caller=(uintptr_t)__builtin_return_address(0);
 if(!InterlockedCompareExchange(&enabled,0,0))return value;
 if(caller!=expected_return&&caller!=initial_return&&caller!=draw_return&&caller!=cache_return){observe_caller(caller);return value;}
 Registration *registration=registration_acquire((uintptr_t)part);
 if(!registration)return value;
 const PSProfile *p=registration->profile;
 if(!p||value!=p->selector)goto done;
 if(caller==draw_return){
  /* Native 0x3637660 saves input RCX at entry RSP+8. Its established RBP is
     entry RSP-0x38, hence saved Scene at RBP+0x40. R14 is a secondary check. */
  uintptr_t saved=ptr((uintptr_t)scene+0x40);InterlockedIncrement(&draw_calls);InterlockedExchange(&refresh_stage,1);
  if(saved!=(uintptr_t)draw_scene){InterlockedExchange(&refresh_reason,9);InterlockedIncrement(&refresh_deferred);goto done;}
  refresh_on_draw(registration,saved,1);goto done;
 }
 if(caller==cache_return){
  /* Native 0x362dd60 saves input RCX at entry RSP+8; RBP=entry RSP-0xe8. */
  InterlockedIncrement(&cache_calls);refresh_on_draw(registration,ptr((uintptr_t)scene+0xf0),2);goto done;
 }
 int initial=caller==initial_return;if(initial){scene=initial_scene;InterlockedIncrement(&initial_calls);}
 BYTE input[404],output[404];uintptr_t verified=0;
 if((uintptr_t)scene!=registration->scene||!read_local((uintptr_t)scene+0x224,input,404,NULL))goto skip;
 if(PSRouteProfile(p,input,order,output)!=1)goto skip;
 uint32_t visual,body,bumper,selected;memcpy(&visual,output+4+4*order[p->selector],4);memcpy(&body,input+4+4*order[2],4);memcpy(&bumper,input+4+4*order[34],4);memcpy(&selected,input+4+4*order[p->selector],4);
 if(visual==selected){InterlockedIncrement(&unchanged);goto done;}
 if(!PSFindProfilePart((uintptr_t)scene,original_vt,(uintptr_t)(shadow+1),p,read_local,NULL,&verified,NULL)||verified!=(uintptr_t)part)goto skip;
 InterlockedExchange((volatile LONG*)((BYTE*)scene+0x228+4*order[p->selector]),(LONG)visual);
 InterlockedExchange(&last_ordinal,p->ordinal);InterlockedExchange(&last_selector,p->selector);InterlockedExchange(&last_body,body);InterlockedExchange(&last_bumper,bumper);InterlockedExchange(&last_input,selected);InterlockedExchange(&last_visual,visual);if(initial)InterlockedIncrement(&initial_routes);InterlockedIncrement(&routes);goto done;
skip:InterlockedIncrement(&skipped);
done:registration_release(registration);return value;
}
__attribute__((naked)) static uint32_t bridge(void *part __attribute__((unused))){__asm__("mov %r14, %r9\n\tmov %r15, %r8\n\tmov %rbp, %rdx\n\tjmp ps_dispatch");}
static int attach(Stored *s,PlateSyncLog log){
 const PSProfile *p=&s->profile;uint32_t ordinal=0;uintptr_t owner=0,token=current_resource(&ordinal,&owner);
 if(!token||ordinal!=p->ordinal)return -1;
 if(!same_file(s)||(!s->bound&&!PSProfileBind(s->path,p))){s->blocked=1;log("profile_binding=failed ordinal=%lu; profile_disabled",(unsigned long)p->ordinal);return -10;}s->bound=1;
 int32_t current[49];if(!read_local(ptr(base+0x8f79818),current,sizeof(current),NULL)||memcmp(current,order,sizeof(order)))return -3;
 uint32_t converted=0;if(!read_local(ptr(base+0x8f79810)+4*p->selector,&converted,4,NULL)||converted!=p->selector)return -3;
 static const BYTE boundary[9]={0x49,0x8b,0x07,0x49,0x8b,0xcf,0xff,0x50,0x28};BYTE actual[9];if(!read_local(base+0x3645783,actual,9,NULL)||memcmp(actual,boundary,9))return -3;
 static const BYTE initial_boundary[9]={0x49,0x8b,0x06,0x49,0x8b,0xce,0xff,0x50,0x28};if(!read_local(base+0x363c924,actual,9,NULL)||memcmp(actual,initial_boundary,9))return -3;
 static const BYTE draw_boundary[9]={0x48,0x8b,0x03,0x48,0x8b,0xcb,0xff,0x50,0x28};if(!read_local(base+0x363776c,actual,9,NULL)||memcmp(actual,draw_boundary,9))return -3;
 if(!read_local(base+0x362df06,actual,9,NULL)||memcmp(actual,draw_boundary,9))return -3;
 static const BYTE cache_entry[10]={0x48,0x8b,0xc4,0x89,0x50,0x10,0x48,0x89,0x48,0x08};BYTE cache_actual[10];if(!read_local(base+0x362dd60,cache_actual,10,NULL)||memcmp(cache_actual,cache_entry,10))return -3;
 static const BYTE draw_entry[15]={0x48,0x89,0x5c,0x24,0x10,0x4c,0x89,0x44,0x24,0x18,0x48,0x89,0x4c,0x24,0x08};BYTE draw_actual[15];if(!read_local(base+0x3637660,draw_actual,15,NULL)||memcmp(draw_actual,draw_entry,15))return -3;
 static const BYTE cache_frame[7]={0x48,0x8d,0xa8,0x18,0xff,0xff,0xff};BYTE frame_actual[18];if(!read_local(base+0x362dd76,frame_actual,7,NULL)||memcmp(frame_actual,cache_frame,7))return -3;
 static const BYTE draw_frame[18]={0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8b,0xec,0x48,0x83,0xec,0x70};if(!read_local(base+0x363766f,frame_actual,18,NULL)||memcmp(frame_actual,draw_frame,18))return -3;
 static const BYTE setup_entry[15]={0x48,0x89,0x5c,0x24,0x10,0x4c,0x89,0x44,0x24,0x18,0x55,0x56,0x57,0x41,0x54};BYTE setup_actual[15];if(!read_local(base+0x3645630,setup_actual,15,NULL)||memcmp(setup_actual,setup_entry,15))return -3;
 if(token<0x4010000)return -2;uintptr_t low=(token&~(uintptr_t)0xfffff)-0x4000000,scene=census(low,low+0x8000000,p),part=0,control=0;
 if(!scene||!PSFindProfilePart(scene,original_vt,(uintptr_t)(shadow+1),p,read_local,NULL,&part,&control))return -2;
 for(LONG i=0;i<InterlockedCompareExchange(&record_count,0,0);i++){
  Registration *r=records+i;
  if(atomic_part(r)!=part)continue;
  if(!(lease_state(r)&REG_LIVE))return -11; /* same object still draining */
  if(r->profile!=p||r->scene!=scene||r->control!=control)return -4;
  if(ptr(part)==(uintptr_t)(shadow+1)){InterlockedExchange(&r->refresh_failed,0);return 0;}
 }
 if(part!=control+0x10||ptr(control)!=base+0x6a9f0c0||ptr(part)!=original_vt||!references_ready)return -5;
 int slot=free_registration_slot();if(slot<0){log("profile_attach=transient_callback_slots_busy; retry=automatic");return -9;}
 /* Take a temporary strong reference only while publishing the hook. The
    durable registration owns a weak reference, never a retained engine part. */
 if(!reference_add(control+8))return -6;
 if(!reference_add(control+12)){strong_release(control);return -6;}
 uint32_t again=0;uintptr_t owner_again=0;int result=-7;
 if(current_resource(&again,&owner_again)!=token||again!=ordinal||owner_again!=owner||!PSFindProfilePart(scene,original_vt,(uintptr_t)(shadow+1),p,read_local,NULL,NULL,NULL)){
  weak_release(control);strong_release(control);return result;
 }
 publish_registration((unsigned)slot,part,control,scene,token,owner,p);
 InterlockedExchange(&enabled,1);
 if(InterlockedCompareExchangePointer((void*volatile*)part,(void*)(shadow+1),(void*)original_vt)!=(void*)original_vt){
  retire_registration(records+slot);collect_registrations();result=-8;
 }else{
  log("profile_attached ordinal=%lu selector=%lu routes=%lu part=%p slot=%d; ownership=weak; carbin_hash=verified profile_source=car_zip",(unsigned long)p->ordinal,(unsigned long)p->selector,(unsigned long)p->route_count,(void*)part,slot);result=0;
 }
 strong_release(control);return result;
}
static void detach_all(PlateSyncLog log){
 InterlockedExchange(&enabled,0);
 sweep_registrations(0,0,0,1);
 log("profile_detach remaining_rundown_slots=%ld",(long)record_count);
}
static int prepare_reference_ops(void){
 /* Exact-build std::_Ref_count_obj2<UpgradablePart>: +8 strong, +12 weak.
    Destroy calls the in-place part destructor with delete=false; Delete_this
    frees only the expired control allocation after the final weak owner. */
 references_ready=0;native_destroy=NULL;native_delete=NULL;
 static const BYTE destroy_bytes[]={0x48,0x8b,0x41,0x10,0x48,0x83,0xc1,0x10,0x33,0xd2,0x48,0xff,0x20};
 static const BYTE delete_bytes[]={0x48,0x85,0xc9,0x74,0x0c,0x48,0x8b,0x01,0xba,0x01,0,0,0,0x48,0xff,0x60,0x10,0xc3};
 BYTE actual[sizeof(delete_bytes)];uintptr_t methods[3];
 if(!read_local(base+0x6a9f0c0,methods,sizeof(methods),NULL)||methods[0]!=base+0x433b00||methods[1]!=base+0x41c500||methods[2]!=base+0x362a6b0)return 0;
 if(!read_local(methods[0],actual,sizeof(destroy_bytes),NULL)||memcmp(actual,destroy_bytes,sizeof(destroy_bytes)))return 0;
 if(!read_local(methods[1],actual,sizeof(delete_bytes),NULL)||memcmp(actual,delete_bytes,sizeof(delete_bytes)))return 0;
 native_destroy=(NativeControlOp)methods[0];native_delete=(NativeControlOp)methods[1];references_ready=1;return 1;
}
void PlateSyncRunRender(HMODULE game,const wchar_t *host,const wchar_t *directory,PlateSyncLog log){
 wchar_t ini[32768];swprintf(ini,32768,L"%ls\\PlateSync\\diagnostic.ini",directory);
 if(GetPrivateProfileIntW(L"profiles",L"Enabled",0,ini)!=1){log("profiles=disabled_by_config");return;}
 BYTE hash[32];static const BYTE expected[32]=RESEARCH_FILE_SHA256;if(!file_sha256(host,hash)||memcmp(hash,expected,32)){log("profiles=unsupported_game_build");return;}
 base=(uintptr_t)game;original_vt=base+0x6a88ec8;expected_return=base+0x364578c;initial_return=base+0x363c92d;draw_return=base+0x3637775;cache_return=base+0x362df0f;scene_setup=(SceneSetup)(base+0x3645630);int prepared=0;
 if(!prepare_reference_ops()){log("profiles=unsupported_reference_lifetime_signature");return;}
 index_profiles(directory,log);if(!stored_count){log("profiles=no_embedded_profiles; ordinary_exports_are_not_supported");return;}
 int automatic=GetPrivateProfileIntW(L"profiles",L"AutoAttach",1,ini)!=0;BOOL prior[3]={0};ULONGLONG beat=GetTickCount64();LONG last=-1;PSAutoState policy={0};uintptr_t last_token=0;uint32_t last_car=0;int last_error=999;
 load_refresh_enabled=GetPrivateProfileIntW(L"profiles",L"LoadRefresh",1,ini)!=0;
 log("profiles=waiting; automatic=%d; F6=retry F7=mark F8=restore_all_and_stop; game_code_patch=NONE; reusable_callback_slots=%u; ownership=weak",automatic,MAX_HOOKS);
 for(;;){
  DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);const int keys[3]={VK_F6,VK_F7,VK_F8};int pressed[3]={0};
  for(unsigned i=0;i<3;i++){BOOL down=(GetAsyncKeyState(keys[i])&0x8000)!=0;pressed[i]=down&&!prior[i]&&foreground==GetCurrentProcessId();prior[i]=down;}if(pressed[2])break;
  uint32_t ordinal=0;uintptr_t owner=0,token=current_resource(&ordinal,&owner);Stored *s=by_ordinal(ordinal);if(!s)token=0;
  int has_part=sweep_registrations(token,owner,ordinal,0);if(!has_part){last_token=0;last_car=0;}
  int due=auto_due(&policy,GetTickCount64(),token,automatic,has_part&&token==last_token&&ordinal==last_car);
  if(s&&(pressed[0]||due)){
   if(!prepared){if(read_local(original_vt-8,shadow,sizeof(shadow),NULL)&&shadow[6]==base+0x352d6f0){original_getter=(Getter)shadow[6];shadow[6]=(uintptr_t)&bridge;prepared=1;}}
   int result=prepared?attach(s,log):-3;
   if(!result){last_token=token;last_car=ordinal;}
   if(pressed[0]||result!=last_error||!result)log("profile_attach_result=%d ordinal=%lu source=%s",result,(unsigned long)ordinal,pressed[0]?"F6":"automatic");last_error=result;
  }
  if(pressed[1])log("manual_marker tick=%llu",(unsigned long long)GetTickCount64());
  if(routes!=last){last=routes;if(routes)log("profile_route count=%ld ordinal=%ld selector=%ld body=%ld bumper=%ld input=%ld visual=%ld",(long)routes,(long)last_ordinal,(long)last_selector,(long)last_body,(long)last_bumper,(long)last_input,(long)last_visual);}
  if(GetTickCount64()-beat>=5000){log("lifetime_heartbeat slots_high_water=%ld retired=%ld weak_releases=%ld reused=%ld faults=%ld",(long)record_count,(long)retired_count,(long)weak_released_count,(long)slot_reuse_count,(long)lifetime_faults);log("profile_heartbeat current_car=%lu registered=%ld calls=%ld routed=%ld skipped=%ld initial_calls=%ld initial_routes=%ld unchanged=%ld",(unsigned long)ordinal,(long)record_count,(long)calls,(long)routes,(long)skipped,(long)initial_calls,(long)initial_routes,(long)unchanged);log("load_refresh_heartbeat draw_calls=%ld cache_calls=%ld requests=%ld applied=%ld failed=%ld deferred=%ld car=%ld input=%ld visual=%ld native_thread=%ld stage=%ld state=0x%lx reason=%ld selected=%ld",(long)draw_calls,(long)cache_calls,(long)refresh_requests,(long)refresh_applied,(long)refresh_failed_count,(long)refresh_deferred,(long)refresh_car,(long)refresh_input,(long)refresh_visual,(long)refresh_thread,(long)refresh_stage,(unsigned long)refresh_state,(long)refresh_reason,(long)refresh_selected);for(unsigned i=0;i<16;i++)if(observed_callers[i])log("profile_other_caller rva=0x%llx count=%ld",(unsigned long long)(observed_callers[i]-base),(long)observed_counts[i]);beat=GetTickCount64();}Sleep(33);
 }detach_all(log);while(pending_registrations()){collect_registrations();Sleep(1);}
 log("profiles=finished; ordinary_weak_registrations_released; quarantined_slots=%ld; tables_remain_pinned_until_exit",(long)record_count);
}
