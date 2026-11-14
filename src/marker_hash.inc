static BOOL file_sha256(const wchar_t *path,BYTE digest[32]){
 BCRYPT_ALG_HANDLE algorithm=NULL;BCRYPT_HASH_HANDLE hash=NULL;
 BYTE *object=NULL,*buffer=NULL;DWORD object_size=0,got=0,property_size=0;
 HANDLE file=INVALID_HANDLE_VALUE;BOOL ok=FALSE;
 if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,NULL,0)<0)goto done;
 if(BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,(PUCHAR)&object_size,sizeof(object_size),&property_size,0)<0||!object_size||object_size>65536)goto done;
 object=HeapAlloc(GetProcessHeap(),0,object_size);buffer=HeapAlloc(GetProcessHeap(),0,65536);
 if(!object||!buffer)goto done;
 if(BCryptCreateHash(algorithm,&hash,object,object_size,NULL,0,0)<0)goto done;
 file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
 if(file==INVALID_HANDLE_VALUE)goto done;
 for(;;){
  if(!ReadFile(file,buffer,65536,&got,NULL))goto done;
  if(!got)break;
  if(BCryptHashData(hash,buffer,got,0)<0)goto done;
 }
 ok=BCryptFinishHash(hash,digest,32,0)>=0;
done:
 if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
 if(hash)BCryptDestroyHash(hash);
 if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);
 if(object)HeapFree(GetProcessHeap(),0,object);
 if(buffer)HeapFree(GetProcessHeap(),0,buffer);
 return ok;
}
