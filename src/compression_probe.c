/* SPDX-License-Identifier: MIT. Standalone ZIP reader test; no game attachment. */
#include "profile_zip.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>
int main(int argc,char **argv){
 if(argc!=3&&argc!=4)return 3;
 wchar_t path[2048];if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,argv[1],-1,path,2048))return 3;
 if(argc==3){PSProfile p;return !strcmp(argv[2],"--profile")&&PSProfileLoad(path,&p)&&PSProfileBind(path,&p)?0:2;}
 void *data=NULL;size_t n=0,cap=(size_t)strtoull(argv[3],NULL,10);
 if(!PSZipRead(path,argv[2],&data,&n,cap))return 2;
 _setmode(_fileno(stdout),_O_BINARY);int ok=fwrite(data,1,n,stdout)==n;
 HeapFree(GetProcessHeap(),0,data);return ok?0:3;
}
