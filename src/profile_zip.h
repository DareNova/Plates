#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "profile.h"
/* Standard unencrypted Store/Deflate entries. Bounded ZIP lookup and raw
   Deflate decoding; no game loader hook or external zip dependency. */
int PSZipRead(const wchar_t*,const char*,void**,size_t*,size_t);
int PSProfileLoad(const wchar_t*,PSProfile*);
int PSProfileBind(const wchar_t*,const PSProfile*);
