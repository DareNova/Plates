/* SPDX-License-Identifier: MIT */
#pragma once
#include "profile.h"
typedef int (*PSRead)(uintptr_t,void*,size_t,void*);
int PSFindProfilePart(uintptr_t,uintptr_t,uintptr_t,const PSProfile*,PSRead,void*,uintptr_t*,uintptr_t*);
