param([string]$ZigPath='zig')
$ErrorActionPreference='Stop'
$common=@('-target','x86_64-windows-gnu','-std=c11','-Oz','-Wall','-Wextra','-Werror','-DNDEBUG','-DUNICODE','-D_UNICODE','-DMINIZ_NO_ZLIB_APIS','-DMINIZ_NO_ARCHIVE_APIS','-DMINIZ_NO_DEFLATE_APIS','-DMINIZ_NO_STDIO','-DMINIZ_NO_MALLOC','-ffunction-sections','-fdata-sections')
$output=Join-Path (Split-Path -Parent $PSScriptRoot) 'search_test.exe'
& $ZigPath cc @common (Join-Path $PSScriptRoot 'search_test.c') (Join-Path $PSScriptRoot 'profile_scene.c') (Join-Path $PSScriptRoot 'profile.c') (Join-Path $PSScriptRoot 'profile_zip.c') (Join-Path $PSScriptRoot 'miniz/miniz_tinfl.c') -lbcrypt -luser32 -o $output
if($LASTEXITCODE -ne 0){throw 'Search test build failed'}
& $output
if($LASTEXITCODE -ne 0){throw 'Search regression test failed'}
