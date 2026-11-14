/* SPDX-License-Identifier: MIT. Profiles contain data, never executable addresses. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#define PS_MAX_ROUTES 128
#define PS_MAX_NATIVE 32
#define PS_MAX_LEVELS (PS_MAX_ROUTES+PS_MAX_NATIVE)
#define PS_ANY 0xffffffffu
#define PS_PROFILE_ENTRY "PlateWorkshop/platesync.profile"
typedef struct {uint32_t body,bumper,input,visual;} PSRoute;
typedef struct {
 uint32_t ordinal,selector,route_count,native_count;
 unsigned char carbin_sha256[32];char carbin_entry[128];
 uint32_t native_ids[PS_MAX_NATIVE];PSRoute route[PS_MAX_ROUTES];
} PSProfile;
int PSProfileParse(const void*,size_t,PSProfile*);
int PSProfileValidate(const PSProfile*);
int PSRouteProfile(const PSProfile*,const void*,const int32_t[49],void*);
int PSProfileLevel(const PSProfile*,uint32_t);
