/* SPDX-License-Identifier: MIT. Bounded activation eligibility; no process access. */
#pragma once
typedef struct {uintptr_t token;ULONGLONG stable_since,next_attempt;unsigned attempts;} PSAutoState;
static int auto_due(PSAutoState *s,ULONGLONG now,uintptr_t token,int automatic,int has_part){
 if(!automatic||has_part)return 0;
 if(!token||token!=s->token){s->token=token;s->stable_since=now;s->next_attempt=0;s->attempts=0;return 0;}
 if(now<s->stable_since||now-s->stable_since<100||now<s->next_attempt)return 0;
 s->attempts++;s->next_attempt=now+(s->attempts<=8?250:now-s->stable_since<30000?1000:5000);return 1;
}
