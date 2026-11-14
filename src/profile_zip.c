/* SPDX-License-Identifier: MIT */
#include "profile_zip.h"
#include <bcrypt.h>
#include <string.h>
#include "miniz/miniz.h"
static uint16_t u16(const BYTE *p){return p[0]|(uint16_t)p[1]<<8;}
static uint32_t u32(const BYTE *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static int read_at(HANDLE f,uint64_t off,void *p,DWORD n){LARGE_INTEGER at;at.QuadPart=(LONGLONG)off;DWORD got=0;return SetFilePointerEx(f,at,NULL,FILE_BEGIN)&&ReadFile(f,p,n,&got,NULL)&&got==n;}
static uint32_t crc32(const BYTE *p,size_t n){uint32_t crc=~0u;for(size_t i=0;i<n;i++){crc^=p[i];for(unsigned j=0;j<8;j++)crc=(crc>>1)^(0xedb88320u&((uint32_t)-(int)(crc&1)));}return ~crc;}
int PSZipRead(const wchar_t *path,const char *name,void **out,size_t *length,size_t cap){
 *out=NULL;*length=0;int ok=0;BYTE *tail=NULL,*cd=NULL,*payload=NULL,*compressed=NULL;tinfl_decompressor *decoder=NULL;HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);if(f==INVALID_HANDLE_VALUE)return 0;
 LARGE_INTEGER size;if(!GetFileSizeEx(f,&size)||size.QuadPart<22||size.QuadPart>536870912)goto done;
 DWORD tail_size=(DWORD)(size.QuadPart<65557?size.QuadPart:65557);tail=HeapAlloc(GetProcessHeap(),0,tail_size);if(!tail||!read_at(f,size.QuadPart-tail_size,tail,tail_size))goto done;
 BYTE *eocd=NULL;for(int i=(int)tail_size-22;i>=0;i--)if(u32(tail+i)==0x06054b50u&&(DWORD)i+22u+u16(tail+i+20)==tail_size){eocd=tail+i;break;}
 if(!eocd||u16(eocd+4)||u16(eocd+6)||u16(eocd+8)!=u16(eocd+10)||u16(eocd+10)==65535)goto done;
 uint32_t cd_size=u32(eocd+12),cd_off=u32(eocd+16);uint64_t eocd_off=(uint64_t)size.QuadPart-tail_size+(eocd-tail);
 if(cd_size>8388608||cd_off+(uint64_t)cd_size!=eocd_off)goto done;
 cd=HeapAlloc(GetProcessHeap(),0,cd_size?cd_size:1);if(!cd||!read_at(f,cd_off,cd,cd_size))goto done;
 size_t off=0;unsigned matches=0;uint32_t local=0,bytes=0,packed=0,crc=0,method=0,flags=0;
 for(unsigned i=0;i<u16(eocd+10);i++){
  if(off+46>cd_size||u32(cd+off)!=0x02014b50u)goto done;BYTE *r=cd+off;size_t names=u16(r+28),extra=u16(r+30),comment=u16(r+32),step=46+names+extra+comment;
  if(step>cd_size-off||u16(r+34))goto done;
  if(names==strlen(name)&&!memcmp(r+46,name,names)){
   if(++matches>1)goto done;flags=u16(r+8);method=u16(r+10);bytes=u32(r+24);local=u32(r+42);crc=u32(r+16);
   packed=u32(r+20);
  }off+=step;
 }
 if(off!=cd_size||matches!=1||(flags&~0x0806u)||(method!=0&&method!=8)||!bytes||bytes>cap||bytes>8388608||!packed||packed>cap+65536||packed>8454144||(method==0&&packed!=bytes))goto done;
 BYTE header[30];if(local+(uint64_t)30>cd_off||!read_at(f,local,header,30)||u32(header)!=0x04034b50u||u16(header+6)!=flags||u16(header+8)!=method||u32(header+14)!=crc||u32(header+18)!=packed||u32(header+22)!=bytes||u16(header+26)!=strlen(name))goto done;
 BYTE local_name[128];size_t name_len=strlen(name);if(name_len>=sizeof(local_name)||!read_at(f,local+30,local_name,(DWORD)name_len)||memcmp(local_name,name,name_len))goto done;
 uint64_t data_off=local+30ULL+u16(header+26)+u16(header+28);if(data_off+packed>cd_off)goto done;
 payload=HeapAlloc(GetProcessHeap(),0,bytes);if(!payload)goto done;
 if(method==0){if(!read_at(f,data_off,payload,bytes))goto done;}
 else{
  compressed=HeapAlloc(GetProcessHeap(),0,packed);decoder=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*decoder));
  if(!compressed||!decoder||!read_at(f,data_off,compressed,packed))goto done;
  tinfl_init(decoder);size_t input_size=packed,output_size=bytes;
  tinfl_status status=tinfl_decompress(decoder,compressed,&input_size,payload,payload,&output_size,TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
  if(status!=TINFL_STATUS_DONE||input_size!=packed||output_size!=bytes)goto done;
 }
 if(crc32(payload,bytes)!=crc)goto done;
 *out=payload;*length=bytes;payload=NULL;ok=1;
done:if(decoder)HeapFree(GetProcessHeap(),0,decoder);if(compressed)HeapFree(GetProcessHeap(),0,compressed);if(payload)HeapFree(GetProcessHeap(),0,payload);if(cd)HeapFree(GetProcessHeap(),0,cd);if(tail)HeapFree(GetProcessHeap(),0,tail);CloseHandle(f);return ok;
}
int PSProfileLoad(const wchar_t *path,PSProfile *p){void *bytes=NULL;size_t n=0;if(!PSZipRead(path,PS_PROFILE_ENTRY,&bytes,&n,8192))return 0;int result=PSProfileParse(bytes,n,p);HeapFree(GetProcessHeap(),0,bytes);return result;}
int PSProfileBind(const wchar_t *path,const PSProfile *p){
 void *bytes=NULL;size_t n=0;if(!PSZipRead(path,p->carbin_entry,&bytes,&n,8388608))return 0;
 BCRYPT_ALG_HANDLE alg=NULL;BCRYPT_HASH_HANDLE hash=NULL;BYTE *object=NULL,digest[32];DWORD size=0,got=0;int ok=0;
 if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,NULL,0)<0)goto done;
 if(BCryptGetProperty(alg,BCRYPT_OBJECT_LENGTH,(PUCHAR)&size,sizeof(size),&got,0)<0||!size||size>65536)goto done;
 object=HeapAlloc(GetProcessHeap(),0,size);if(!object||BCryptCreateHash(alg,&hash,object,size,NULL,0,0)<0)goto done;
 if(BCryptHashData(hash,bytes,(ULONG)n,0)<0||BCryptFinishHash(hash,digest,32,0)<0)goto done;
 ok=!memcmp(digest,p->carbin_sha256,32);
done:if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);if(object)HeapFree(GetProcessHeap(),0,object);HeapFree(GetProcessHeap(),0,bytes);return ok;
}
