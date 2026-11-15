/* SPDX-License-Identifier: MIT. Opt-in, exact-build renderer proof. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>
#include "build_fingerprint.h"
#include "render_monitor.h"
static HMODULE self_module;
static volatile LONG status_code;
static HANDLE log_file=INVALID_HANDLE_VALUE;
__declspec(dllexport) DWORD WINAPI PlateSyncGetStatus(void){return (DWORD)InterlockedCompareExchange(&status_code,0,0);}
__declspec(dllexport) DWORD WINAPI PlateSyncDiagnosticVersion(void){return 0x00110009;}
static void line(const char *format,...){
 char buffer[1600];va_list args;va_start(args,format);int n=vsnprintf(buffer,sizeof(buffer)-3,format,args);va_end(args);
 if(n<0)return;if((size_t)n>sizeof(buffer)-3)n=(int)sizeof(buffer)-3;
 buffer[n++]='\r';buffer[n++]='\n';DWORD written;
 if(log_file!=INVALID_HANDLE_VALUE)WriteFile(log_file,buffer,(DWORD)n,&written,NULL);
}
static BOOL read_self(const void *address,void *out,SIZE_T bytes){SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),address,out,bytes,&got)&&got==bytes;}
static DWORD WINAPI inspect(LPVOID unused){
 (void)unused;InterlockedExchange(&status_code,1);HMODULE pinned=NULL;
 if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,(LPCWSTR)(const void*)&inspect,&pinned)){InterlockedExchange(&status_code,5);return 0;}
 wchar_t dir[32768],path[32768],host[32768]={0};DWORD length=GetModuleFileNameW(self_module,dir,32768);
 if(!length||length>=32700){InterlockedExchange(&status_code,4);return 0;}wchar_t *last=wcsrchr(dir,L'\\');if(!last){InterlockedExchange(&status_code,4);return 0;}*last=0;
 swprintf(path,32768,L"%ls\\PlateSync",dir);if(!CreateDirectoryW(path,NULL)&&GetLastError()!=ERROR_ALREADY_EXISTS){InterlockedExchange(&status_code,4);return 0;}
 swprintf(path,32768,L"%ls\\PlateSync\\diagnostics",dir);if(!CreateDirectoryW(path,NULL)&&GetLastError()!=ERROR_ALREADY_EXISTS){InterlockedExchange(&status_code,4);return 0;}
 swprintf(path,32768,L"%ls\\PlateSync\\diagnostics\\platesync-%lu-%llu.log",dir,(unsigned long)GetCurrentProcessId(),(unsigned long long)GetTickCount64());
 log_file=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);if(log_file==INVALID_HANDLE_VALUE){InterlockedExchange(&status_code,4);return 0;}
 SYSTEMTIME utc;GetSystemTime(&utc);line("PlateSync 0.17.9 -- bounded adaptive setup discovery");
 line("utc=%04u-%02u-%02uT%02u:%02u:%02uZ",utc.wYear,utc.wMonth,utc.wDay,utc.wHour,utc.wMinute,utc.wSecond);
 line("scope=embedded_zip_profiles; automatic_instance_callback_NO_CODE_PATCH; no_debugger; no_database_or_profile_writes; no_network");
 DWORD host_len=GetModuleFileNameW(NULL,host,32768);const wchar_t *name=wcsrchr(host,L'\\');name=name?name+1:host;
 BOOL correct=host_len&&host_len<32768&&CompareStringOrdinal(name,-1,L"forzahorizon6.exe",-1,TRUE)==CSTR_EQUAL;
 DWORD finish=2;
 if(!correct){line("result=wrong-host; engine_not_inspected");finish=3;}
 else {
  HMODULE game=GetModuleHandleW(NULL);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
  BOOL valid=read_self(game,&dos,sizeof(dos))&&dos.e_magic==IMAGE_DOS_SIGNATURE&&dos.e_lfanew>0&&dos.e_lfanew<1048576;
  if(valid)valid=read_self((BYTE*)game+dos.e_lfanew,&nt,sizeof(nt))&&nt.Signature==IMAGE_NT_SIGNATURE&&nt.FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64&&nt.OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC;
  if(!valid)line("result=PE_header_unavailable; hook_disabled");
  else if(nt.FileHeader.TimeDateStamp!=RESEARCH_PE_TIMESTAMP||nt.OptionalHeader.SizeOfImage!=RESEARCH_IMAGE_SIZE)line("result=unsupported_build_header; hook_disabled");
  else PlateSyncRunRender(game,host,dir,line);
 }
 line("diagnostic_complete=yes; worker_exits; DLL_pinned_until_process_exit");FlushFileBuffers(log_file);CloseHandle(log_file);log_file=INVALID_HANDLE_VALUE;
 InterlockedExchange(&status_code,finish);return 0;
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID reserved){
 (void)reserved;if(reason==DLL_PROCESS_ATTACH){self_module=instance;DisableThreadLibraryCalls(instance);
  HANDLE thread=CreateThread(NULL,0,inspect,NULL,0,NULL);if(thread)CloseHandle(thread);else InterlockedExchange(&status_code,5);
 }return TRUE;
}
