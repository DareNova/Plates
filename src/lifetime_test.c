/* SPDX-License-Identifier: MIT. Synthetic native refcount/rundown stress; no game access. */
#define main profile_regression_main
#include "profile_test.c"
#undef main
#include <stddef.h>
typedef struct {uintptr_t table;volatile LONG strong,weak;BYTE part[0x118];} TestControl;
_Static_assert(offsetof(TestControl,part)==16,"native control layout");
static uintptr_t control_vt[3],part_vt[32];
static volatile LONG destroys,deletes,thread_stop,thread_faults,thread_leases;
static uintptr_t threaded_part;
static void quiet(const char *format,...){(void)format;}
static void fake_destroy(void *pointer){TestControl *c=pointer;InterlockedIncrement(&destroys);put(c->part,0,original_vt);}
static void fake_delete(void *pointer){TestControl *c=pointer;if(c->strong||c->weak)InterlockedIncrement(&thread_faults);InterlockedIncrement(&deletes);}
static void reset_lifetime(void){
 memset(records,0,sizeof(records));record_count=0;destroys=deletes=0;retired_count=weak_released_count=slot_reuse_count=lifetime_faults=0;
 base=(uintptr_t)control_vt-0x6a9f0c0;original_vt=(uintptr_t)part_vt;native_destroy=fake_destroy;native_delete=fake_delete;references_ready=1;enabled=1;
}
static void initialize_control(TestControl *c){memset(c,0,sizeof(*c));c->table=(uintptr_t)control_vt;c->strong=c->weak=1;put(c->part,0,original_vt);}
static int publish_fake(TestControl *c,PSProfile *p,uintptr_t token){
 int slot=free_registration_slot();if(slot<0)return -1;
 if(!reference_add((uintptr_t)c+8))return -2;
 if(!reference_add((uintptr_t)c+12)){strong_release((uintptr_t)c);return -3;}
 publish_registration((unsigned)slot,(uintptr_t)c->part,(uintptr_t)c,(uintptr_t)p,token,token+17,p);
 if(InterlockedCompareExchangePointer((void*volatile*)c->part,(void*)(shadow+1),(void*)original_vt)!=(void*)original_vt)return -4;
 strong_release((uintptr_t)c);return slot;
}
static int sequential(PSProfile *p){
 reset_lifetime();TestControl c;
 for(unsigned i=0;i<10000;i++){
  initialize_control(&c);CHECK(publish_fake(&c,p,100+i)>=0);CHECK(c.strong==1&&c.weak==2);
  CHECK(sweep_registrations(100+i,117+i,p->ordinal,0)==1);
  /* Game last owner can die naturally despite an installed hook. */
  strong_release((uintptr_t)&c);CHECK(c.strong==0&&c.weak==1);
  CHECK(!sweep_registrations(0,0,0,0));CHECK(!record_count&&c.weak==0);
 }
 CHECK(destroys==10000&&deletes==10000&&slot_reuse_count==9999&&!lifetime_faults);
 puts("PASS 10,000 instance deaths/address reuses: strong counts restored immediately, game destruction proceeds, slots recycle without a session cap");return 0;
}
static int cached(PSProfile *p){
 reset_lifetime();TestControl c;initialize_control(&c);
 for(unsigned i=0;i<2000;i++){
  CHECK(publish_fake(&c,p,200)>=0);CHECK(c.strong==1&&c.weak==2);
  CHECK(!sweep_registrations(201,218,p->ordinal,0));CHECK(c.strong==1&&c.weak==1&&!record_count&&ptr((uintptr_t)c.part)==original_vt);
 }
 CHECK(!destroys&&!deletes);strong_release((uintptr_t)&c);CHECK(destroys==1&&deletes==1);
 puts("PASS 2,000 cached-car detach/re-attach cycles: no retained owning references, original callbacks restored, no premature destruction");return 0;
}
static int inflight(PSProfile *p){
 reset_lifetime();TestControl c;initialize_control(&c);CHECK(publish_fake(&c,p,200)==0);
 Registration *r=registration_acquire((uintptr_t)c.part);CHECK(r&&lease_state(r)==REG_LIVE+1);
 retire_registration(r);collect_registrations();CHECK(lease_state(r)==REG_CLOSING+1&&c.weak==2);
 CHECK(!registration_acquire((uintptr_t)c.part));CHECK(free_registration_slot()!=0);
 /* Last game owner may disappear after callback work, before lease epilogue. */
 strong_release((uintptr_t)&c);CHECK(destroys==1&&!deletes&&c.weak==1);
 collect_registrations();CHECK(c.weak==1&&r->profile==p);
 registration_release(r);collect_registrations();CHECK(!record_count&&deletes==1&&c.weak==0);
 /* An already fetched bridge can arrive after retirement. It must call the
    original getter, then decline routing when there is no admitted record. */
 original_getter=getter;getter_value=p->selector;expected_return=(uintptr_t)&ps_test_return;
 unsigned before=getter_calls;CHECK(native_style(c.part,NULL,(void*)&bridge)==p->selector);CHECK(getter_calls==before+1);
 puts("PASS in-flight and late callback rundown: no slot reuse/free until admitted callback returns; late bridge declines routing");return 0;
}
static int simultaneous(PSProfile *p){
 reset_lifetime();TestControl c[33];Registration *held[32];
 for(unsigned i=0;i<32;i++){initialize_control(c+i);CHECK(publish_fake(c+i,p,100+i)==(int)i);held[i]=registration_acquire((uintptr_t)c[i].part);CHECK(held[i]);}
 CHECK(free_registration_slot()==-1);
 sweep_registrations(0,0,0,1);CHECK(free_registration_slot()==-1);
 for(unsigned i=0;i<32;i++)registration_release(held[i]);collect_registrations();CHECK(!record_count);
 initialize_control(c+32);CHECK(publish_fake(c+32,p,999)==0);sweep_registrations(0,0,0,1);
 for(unsigned i=0;i<33;i++)strong_release((uintptr_t)(c+i));
 CHECK(destroys==33&&deletes==33&&!lifetime_faults);
 puts("PASS all 32 slots temporarily busy: pending callbacks defer collection, then the next instance attaches without restart");return 0;
}
static DWORD WINAPI lease_reader(void *unused){
 (void)unused;
 while(!InterlockedCompareExchange(&thread_stop,0,0)){
  Registration *r=registration_acquire(threaded_part);if(!r){SwitchToThread();continue;}
  if((uintptr_t)r->profile!=r->scene||atomic_part(r)!=threaded_part)InterlockedIncrement(&thread_faults);
  InterlockedIncrement(&thread_leases);SwitchToThread();
  if((uintptr_t)r->profile!=r->scene)InterlockedIncrement(&thread_faults);
  registration_release(r);
 }
 return 0;
}
static int concurrent(PSProfile *p){
 reset_lifetime();thread_stop=thread_faults=thread_leases=0;TestControl c;initialize_control(&c);threaded_part=(uintptr_t)c.part;
 HANDLE threads[4];for(unsigned i=0;i<4;i++){threads[i]=CreateThread(NULL,0,lease_reader,NULL,0,NULL);CHECK(threads[i]);}
 PSProfile alternate=*p;
 for(unsigned i=0;i<20000;i++){
  CHECK(publish_fake(&c,i&1?p:&alternate,200+i)==0);
  SwitchToThread();retire_registration(records);
  do{collect_registrations();if(record_count)SwitchToThread();}while(record_count);
  CHECK(c.strong==1&&c.weak==1);
 }
 InterlockedExchange(&thread_stop,1);CHECK(WaitForMultipleObjects(4,threads,TRUE,10000)==WAIT_OBJECT_0);
 for(unsigned i=0;i<4;i++)CloseHandle(threads[i]);
 CHECK(thread_leases>0&&!thread_faults&&!lifetime_faults);strong_release((uintptr_t)&c);CHECK(destroys==1&&deletes==1);
 printf("PASS four callback threads through 20,000 publish/retire cycles: leases=%ld immutable fields preserved; zero lifecycle faults\n",(long)thread_leases);return 0;
}
static int refused_counts(void){
 volatile LONG count=0;CHECK(!reference_add((uintptr_t)&count)&&!count);count=-1;CHECK(!reference_add((uintptr_t)&count)&&count==-1);count=1000000;CHECK(!reference_add((uintptr_t)&count)&&count==1000000);
 puts("PASS zero/expired/invalid reference counts are never resurrected or incremented");return 0;
}
static int quarantine_guard(PSProfile *p){
 reset_lifetime();TestControl c;initialize_control(&c);CHECK(publish_fake(&c,p,200)==0);
 c.table=0;retire_registration(records);collect_registrations();
 CHECK(lease_state(records)==REG_QUARANTINED&&lifetime_faults==1&&c.weak==2&&!deletes&&!pending_registrations());
 collect_registrations();CHECK(lifetime_faults==1&&c.weak==2);
 c.table=(uintptr_t)control_vt;CHECK(weak_release((uintptr_t)&c));strong_release((uintptr_t)&c);CHECK(deletes==1);
 puts("PASS unexpected control type is quarantined once: no blind release, slot reuse, repeated busy cleanup or F8 drain hang");return 0;
}
/* Synthetic image/scene for exercising the real attach path and native guards. */
static int guarded_attach_cycles(PSProfile *p,const wchar_t *path){
 BYTE *image=VirtualAlloc(NULL,0xa920000,MEM_RESERVE,PAGE_NOACCESS);
 BYTE *arena=VirtualAlloc(NULL,0x8200000,MEM_RESERVE,PAGE_NOACCESS);CHECK(image&&arena);
 const uintptr_t pages[]={0x6a9f000,0x6a88000,0x433000,0x41c000,0x3645000,0x363c000,0x3637000,0x362d000,0x8f79000,0xa91d000};
 for(unsigned i=0;i<sizeof(pages)/sizeof(pages[0]);i++)CHECK(VirtualAlloc(image+pages[i],4096,MEM_COMMIT,PAGE_READWRITE));
 reset_lifetime();base=(uintptr_t)image;original_vt=base+0x6a88ec8;
 uintptr_t methods[3]={base+0x433b00,base+0x41c500,base+0x362a6b0};memcpy(image+0x6a9f0c0,methods,sizeof(methods));
 const BYTE destroy[]={0x48,0x8b,0x41,0x10,0x48,0x83,0xc1,0x10,0x33,0xd2,0x48,0xff,0x20};
 const BYTE deletion[]={0x48,0x85,0xc9,0x74,0x0c,0x48,0x8b,0x01,0xba,1,0,0,0,0x48,0xff,0x60,0x10,0xc3};
 memcpy(image+0x433b00,destroy,sizeof(destroy));memcpy(image+0x41c500,deletion,sizeof(deletion));CHECK(prepare_reference_ops());
 image[0x433b00]^=1;CHECK(!prepare_reference_ops()&&!references_ready);image[0x433b00]^=1;CHECK(prepare_reference_ops());
 native_destroy=fake_destroy;native_delete=fake_delete;
 const BYTE menu[]={0x49,0x8b,0x07,0x49,0x8b,0xcf,0xff,0x50,0x28};memcpy(image+0x3645783,menu,sizeof(menu));
 const BYTE initial[]={0x49,0x8b,0x06,0x49,0x8b,0xce,0xff,0x50,0x28};memcpy(image+0x363c924,initial,sizeof(initial));
 const BYTE drawing[]={0x48,0x8b,0x03,0x48,0x8b,0xcb,0xff,0x50,0x28};memcpy(image+0x363776c,drawing,sizeof(drawing));memcpy(image+0x362df06,drawing,sizeof(drawing));
 const BYTE cache_entry[]={0x48,0x8b,0xc4,0x89,0x50,0x10,0x48,0x89,0x48,0x08};memcpy(image+0x362dd60,cache_entry,sizeof(cache_entry));
 const BYTE draw_entry[]={0x48,0x89,0x5c,0x24,0x10,0x4c,0x89,0x44,0x24,0x18,0x48,0x89,0x4c,0x24,0x08};memcpy(image+0x3637660,draw_entry,sizeof(draw_entry));
 const BYTE cache_frame[]={0x48,0x8d,0xa8,0x18,0xff,0xff,0xff};memcpy(image+0x362dd76,cache_frame,sizeof(cache_frame));
 const BYTE draw_frame[]={0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8b,0xec,0x48,0x83,0xec,0x70};memcpy(image+0x363766f,draw_frame,sizeof(draw_frame));
 const BYTE setup[]={0x48,0x89,0x5c,0x24,0x10,0x4c,0x89,0x44,0x24,0x18,0x55,0x56,0x57,0x41,0x54};memcpy(image+0x3645630,setup,sizeof(setup));
 int32_t converted[49];for(unsigned i=0;i<49;i++)converted[i]=(int32_t)i;
 put(image,0x8f79818,(uintptr_t)order);put(image,0x8f79810,(uintptr_t)converted);
 uintptr_t low=((uintptr_t)arena+0xfffff)&~(uintptr_t)0xfffff;
 BYTE *chunk=(BYTE*)(low+0x4000000);CHECK(VirtualAlloc(chunk,0x10000,MEM_COMMIT,PAGE_READWRITE));
 BYTE *resource=chunk+0x1000,*scene=chunk+0x3000,*nodes=chunk+0x5000;
 TestControl *control=(TestControl*)(chunk+0x2000);uintptr_t *vector=(uintptr_t*)(chunk+0x4000);
 BYTE loc[0x100]={0},hop[32]={0},hop2[32]={0},service[0x200]={0},owner_ctrl[0x100]={0},car[0x80]={0},holder[0xe0]={0},resource_ctrl[0x30]={0};
 uint32_t ordinal=p->ordinal;put(image,0xa91d6b8,(uintptr_t)loc);put(loc,0,base+0x6817928);loc[0x70]=1;
 put(loc,0x78,(uintptr_t)hop);put(hop,0,(uintptr_t)hop2);put(hop2,0x10,(uintptr_t)service);
 put(service,0,base+0x6817128);put(service,0x180,(uintptr_t)owner_ctrl);put(owner_ctrl,0xc0,(uintptr_t)car);put(car,0,base+0x680af90);put(car,0x48,(uintptr_t)&ordinal);
 put(service,0x170,(uintptr_t)holder);put(holder,0x80,(uintptr_t)resource);put(holder,0x88,(uintptr_t)resource_ctrl);put(resource,0,base+0x64da9f8);put(resource_ctrl,0,base+0x6a71d60);put(resource_ctrl,0x10,(uintptr_t)resource);
 put(scene,0,base+0x6a9dae0);put(scene,0x5d0,(uintptr_t)vector);put(scene,0x5d8,(uintptr_t)(vector+2));vector[0]=(uintptr_t)control->part;vector[1]=(uintptr_t)control;
 unsigned count=p->native_count+p->route_count;
 for(unsigned i=0;i<=count;i++){BYTE *n=nodes+i*0x40;put(n,0,(uintptr_t)(nodes+((i+1)%(count+1))*0x40));put(n,8,(uintptr_t)(nodes+((i+count)%(count+1))*0x40));if(i)put32(n,0x30,i<=p->native_count?p->native_ids[i-1]:p->route[i-1-p->native_count].visual);}
 Stored s={.profile=*p};wcscpy(s.path,path);CHECK(GetFileAttributesExW(path,GetFileExInfoStandard,&s.stamp));
 for(unsigned i=0;i<128;i++){
  initialize_control(control);control->table=base+0x6a9f0c0;put(control->part,0x60,(uintptr_t)scene);put32(control->part,0x20,p->selector);control->part[0x24]=1;put(control->part,0xe8,(uintptr_t)nodes);
  CHECK(!attach(&s,quiet));CHECK(control->strong==1&&control->weak==2&&record_count==1);
  /* Repeated retry of the current attachment must not acquire extra references. */
  CHECK(!attach(&s,quiet));CHECK(control->strong==1&&control->weak==2);
  CHECK(!sweep_registrations(0,0,0,0));CHECK(control->strong==1&&control->weak==1&&!record_count);
  strong_release((uintptr_t)control);CHECK(control->strong==0&&control->weak==0);
 }
 CHECK(destroys==128&&deletes==128&&slot_reuse_count==127&&!lifetime_faults);
 CHECK(VirtualFree(arena,0,MEM_RELEASE));CHECK(VirtualFree(image,0,MEM_RELEASE));
 puts("PASS real attach path for 128 instance lifetimes: exact-build guards, profile binding, scene census, cached retry, weak ownership and slot reuse");return 0;
}

int main(int argc,char **argv){
 int result=profile_regression_main(argc,argv);if(result)return result;
 wchar_t path[2048];CHECK(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,argv[1],-1,path,2048));PSProfile p;CHECK(PSProfileLoad(path,&p));
 CHECK(!sequential(&p));CHECK(!cached(&p));CHECK(!inflight(&p));CHECK(!simultaneous(&p));CHECK(!concurrent(&p));CHECK(!refused_counts());CHECK(!quarantine_guard(&p));CHECK(!guarded_attach_cycles(&p,path));
 return 0;
}
