param([string]$ZigPath='zig')
$ErrorActionPreference='Stop'
$common=@('-target','x86_64-windows-gnu','-std=c11','-Wall','-Wextra','-Werror','-DNDEBUG','-DUNICODE','-D_UNICODE','-DMINIZ_NO_ZLIB_APIS','-DMINIZ_NO_ARCHIVE_APIS','-DMINIZ_NO_DEFLATE_APIS','-DMINIZ_NO_STDIO','-DMINIZ_NO_MALLOC','-ffunction-sections','-fdata-sections')
$librarySources=@((Join-Path $PSScriptRoot 'profile_scene.c'),(Join-Path $PSScriptRoot 'profile.c'),(Join-Path $PSScriptRoot 'profile_zip.c'),(Join-Path $PSScriptRoot 'miniz/miniz_tinfl.c'))
& $ZigPath cc @common -Oz -shared -s (Join-Path $PSScriptRoot 'platesync_diagnostic.c') (Join-Path $PSScriptRoot 'profile_monitor.c') @librarySources -lbcrypt -luser32 '-Wl,--gc-sections' -o (Join-Path (Split-Path -Parent $PSScriptRoot) 'platesync-version.dll')
if($LASTEXITCODE -ne 0){throw 'PlateSync DLL build failed'}
