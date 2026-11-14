/* SPDX-License-Identifier: MIT. Callback rundown and typed native weak ownership.
   One worker publishes/retires slots; game callbacks take bounded shared leases.
   No engine object is held alive between attachment attempts. */
#pragma once
#define REG_LIVE 0x40000000L
#define REG_CLOSING 0x20000000L
#define REG_QUARANTINED 0x10000000L
#define REG_READERS 0xffffL
static volatile LONG retired_count,weak_released_count,slot_reuse_count,lifetime_faults;
typedef void (*NativeControlOp)(void*);
static NativeControlOp native_destroy,native_delete;
static int references_ready;
static uintptr_t atomic_part(const Registration *r){return (uintptr_t)InterlockedCompareExchangePointer((void*volatile*)&r->part,NULL,NULL);}
static LONG lease_state(const Registration *r){return InterlockedCompareExchange((volatile LONG*)&r->lease,0,0);}
static Registration *registration_acquire(uintptr_t part){
 LONG count=InterlockedCompareExchange(&record_count,0,0);
 for(LONG i=0;i<count;i++){
  Registration *r=records+i;if(atomic_part(r)!=part)continue;
  LONG state=lease_state(r);
  while((state&REG_LIVE)&&((state&REG_READERS)<REG_READERS)){
   LONG prior=InterlockedCompareExchange(&r->lease,state+1,state);
   if(prior==state){if(atomic_part(r)==part)return r;InterlockedDecrement(&r->lease);break;}state=prior;
  }
 }
 return NULL;
}
static void registration_release(Registration *r){InterlockedDecrement(&r->lease);}
static int reference_add(uintptr_t address){
 LONG value=InterlockedCompareExchange((volatile LONG*)address,0,0);
 for(unsigned attempt=0;attempt<128&&value>0&&value<1000000;attempt++){
  LONG prior=InterlockedCompareExchange((volatile LONG*)address,value+1,value);if(prior==value)return 1;value=prior;
 }
 return 0;
}
static int weak_release(uintptr_t control){
 if(!references_ready||ptr(control)!=base+0x6a9f0c0)return 0;
 LONG count=InterlockedDecrement((volatile LONG*)(control+12));
 if(count==0)native_delete((void*)control);
 else if(count<0){InterlockedIncrement(&lifetime_faults);return 0;}
 InterlockedIncrement(&weak_released_count);return 1;
}
static void strong_release(uintptr_t control){
 LONG count=InterlockedDecrement((volatile LONG*)(control+8));
 if(count==0){native_destroy((void*)control);weak_release(control);}
 else if(count<0)InterlockedIncrement(&lifetime_faults);
}
static void retire_registration(Registration *r){
 LONG state=lease_state(r);
 while(state&REG_LIVE){
  LONG prior=InterlockedCompareExchange(&r->lease,(state&REG_READERS)|REG_CLOSING,state);
  if(prior==state){
   /* The owned weak reference keeps the in-place part's storage allocated even
      after destruction. Never overwrite a destructor/other mod's table. */
   InterlockedCompareExchangePointer((void*volatile*)atomic_part(r),(void*)original_vt,(void*)(shadow+1));
   InterlockedIncrement(&retired_count);return;
  }state=prior;
 }
}
static void collect_registrations(void){
 LONG count=InterlockedCompareExchange(&record_count,0,0);
 for(LONG i=0;i<count;i++){
  Registration *r=records+i;
  if(lease_state(r)!=REG_CLOSING)continue;
  /* Closing rejects late admissions. Existing callbacks must have completed
     before fields, native weak ownership, or the slot can be recycled. */
  if(r->control&&!weak_release(r->control)){InterlockedIncrement(&lifetime_faults);InterlockedExchange(&r->lease,REG_QUARANTINED);continue;}
  InterlockedExchangePointer((void*volatile*)&r->part,NULL);r->control=0;r->profile=NULL;r->scene=0;r->token=0;r->owner=0;
  r->refresh_busy=0;r->refresh_failed=0;InterlockedExchange(&r->lease,0);
 }
 while(count>0&&lease_state(records+count-1)==0)count--;
 InterlockedExchange(&record_count,count);
}
static int pending_registrations(void){
 LONG count=InterlockedCompareExchange(&record_count,0,0);
 for(LONG i=0;i<count;i++)if(lease_state(records+i)&(REG_LIVE|REG_CLOSING))return 1;
 return 0;
}
static int sweep_registrations(uintptr_t token,uintptr_t owner,uint32_t ordinal,int stop){
 int current=0;LONG count=InterlockedCompareExchange(&record_count,0,0);
 for(LONG i=0;i<count;i++){
  Registration *r=records+i;if(!(lease_state(r)&REG_LIVE))continue;
  LONG strong=0;
  int alive=r->control&&read_local(r->control+8,&strong,4,NULL)&&strong>0;
  int selected=!stop&&alive&&token&&r->token==token&&r->owner==owner&&r->profile->ordinal==ordinal&&ptr(atomic_part(r))==(uintptr_t)(shadow+1);
  if(selected)current=1;else retire_registration(r);
 }
 collect_registrations();return current;
}
static int free_registration_slot(void){
 for(unsigned i=0;i<MAX_HOOKS;i++)if(!lease_state(records+i)&&!atomic_part(records+i))return (int)i;
 return -1;
}
static void publish_registration(unsigned slot,uintptr_t part,uintptr_t control,uintptr_t scene,uintptr_t token,uintptr_t owner,const PSProfile *profile){
 Registration *r=records+slot;
 r->control=control;r->profile=profile;r->scene=scene;r->token=token;r->owner=owner;r->refresh_busy=0;r->refresh_failed=0;
 if(r->ever_used)InterlockedIncrement(&slot_reuse_count);r->ever_used=1;
 InterlockedExchangePointer((void*volatile*)&r->part,(void*)part);
 LONG count=InterlockedCompareExchange(&record_count,0,0);if((LONG)slot>=count)InterlockedExchange(&record_count,(LONG)slot+1);
 InterlockedExchange(&r->lease,REG_LIVE);
}
