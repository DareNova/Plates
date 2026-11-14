# Plate Workshop release installer. Plain PowerShell 5.1+, built-in .NET only.
param([ValidateSet('enable','disable','status')][string]$Command='enable',[string]$Game='', [switch]$NoPause,[ValidateRange(1,4)][int]$Workers=2)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$Package=$PSScriptRoot
function ShowInstallProgress([int]$Done,[int]$Total,[string]$Detail) {
 $percent=if ($Total -gt 0) {[Math]::Min(100,[int](100.0*$Done/$Total))} else {0}
 $status="$Done of $Total cars | $($report.installed.Count) installed | $($report.unchanged.Count) up to date | $($report.skipped.Count) skipped"
 Write-Progress -Id 1 -Activity 'Installing FH6 Front Plates' -Status $status -CurrentOperation $Detail -PercentComplete $percent
}
function Safe([string]$Root,[string]$Name) {
 if (!$Name -or $Name.Contains('\') -or $Name.Contains(':') -or $Name.StartsWith('/') -or @($Name.Split('/') | Where-Object {$_ -in @('','.','..')}).Count) {throw 'Unsafe relative path'}
 $rootFull=[IO.Path]::GetFullPath($Root).TrimEnd('\','/');$p=[IO.Path]::GetFullPath((Join-Path $rootFull $Name))
 if (!$p.StartsWith($rootFull+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {throw 'Path escapes install folder'}
 $q=$p
 while ($q -and $q -ne $rootFull) {
  if ((Test-Path -LiteralPath $q) -and ((Get-Item -LiteralPath $q -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {throw 'Linked target refused'}
  $q=Split-Path $q -Parent
 }
 return $p
}
function Hash([byte[]]$Data) {$h=[Security.Cryptography.SHA256]::Create();try {return ([BitConverter]::ToString($h.ComputeHash($Data))).Replace('-','').ToLowerInvariant()} finally {$h.Dispose()}}
function FileHash([string]$Path) {$stream=[IO.File]::OpenRead($Path);$h=[Security.Cryptography.SHA256]::Create();try {return ([BitConverter]::ToString($h.ComputeHash($stream))).Replace('-','').ToLowerInvariant()} finally {$h.Dispose();$stream.Dispose()}}
function Put([string]$Path,[byte[]]$Data) {
 [IO.Directory]::CreateDirectory((Split-Path $Path -Parent)) | Out-Null
 $tmp=$Path+'.tmp-'+[guid]::NewGuid().ToString('N')
 try {[IO.File]::WriteAllBytes($tmp,$Data);if ([IO.File]::Exists($Path)) {[IO.File]::Replace($tmp,$Path,[NullString]::Value)} else {[IO.File]::Move($tmp,$Path)}} finally {if ([IO.File]::Exists($tmp)) {[IO.File]::Delete($tmp)}}
}
function Save([string]$Path,$Value) {Put $Path ([Text.Encoding]::UTF8.GetBytes(($Value | ConvertTo-Json -Depth 100)))}
function ReadEntry($Zip,[string]$Name) {
 $e=$Zip.GetEntry($Name);if (!$e) {throw "Missing archive entry: $Name"};if ($e.Length -gt 268435456) {throw 'Asset exceeds size limit'}
 $inputStream=$e.Open();$memory=New-Object IO.MemoryStream
 try {$inputStream.CopyTo($memory);return ,$memory.ToArray()} finally {$inputStream.Dispose();$memory.Dispose()}
}
function Delta([byte[]]$Base,[byte[]]$Patch) {
 $stream=New-Object IO.MemoryStream(,$Patch);$reader=New-Object IO.BinaryReader($stream)
 try {
  if ([Text.Encoding]::ASCII.GetString($reader.ReadBytes(8)) -ne 'PWDELTA1') {throw 'Invalid delta header'}
  $size=$reader.ReadUInt32();if ($size -gt 268435456) {throw 'Delta exceeds size limit'}
  $output=New-Object byte[] $size;$cursor=0
  while ($stream.Position -lt $stream.Length) {
   $tag=$reader.ReadByte()
   if ($tag -eq 0) {$offset=$reader.ReadUInt32();$count=$reader.ReadUInt32();if ([long]$offset+$count -gt $Base.Length -or [long]$cursor+$count -gt $size) {throw 'Delta copy outside bounds'};[Buffer]::BlockCopy($Base,$offset,$output,$cursor,$count)}
   elseif ($tag -eq 1) {$count=$reader.ReadUInt32();if ([long]$cursor+$count -gt $size -or $stream.Position+$count -gt $stream.Length) {throw 'Truncated delta'};$bytes=$reader.ReadBytes($count);[Buffer]::BlockCopy($bytes,0,$output,$cursor,$count)}
   else {throw 'Unknown delta operation'}
   $cursor+=$count
  }
  if ($cursor -ne $size) {throw 'Delta size mismatch'}
  return ,$output
 } finally {$reader.Dispose();$stream.Dispose()}
}
function Original($Record) {
 if ($Record.backup) {$p=Safe $Game $Record.backup;if ((FileHash $p) -ne $Record.original_hash) {throw 'Backup changed or damaged'};return $p}
 return (Safe $Game $Record.base_path)
}
function InstallFile([string]$Relative,[string]$Staged,[string]$BasePath='',[string]$PatchHash='') {
 $target=Safe $Game $Relative;$old=$State.files[$Relative]
 if ($old -and (!(Test-Path -LiteralPath $target) -or (FileHash $target) -ne $old.installed_hash)) {throw "Installed file changed later; preserving $Relative"}
 if ($old) {$record=@{backup=$old.backup;original_hash=$old.original_hash;base_path=$old.base_path}}
 else {
  $record=@{backup=$null;original_hash=$null;base_path=$null}
  if (Test-Path -LiteralPath $target) {
   $backupRelative=$State.backup_root+'/'+[guid]::NewGuid().ToString('N')+'.bak';$backup=Safe $Game $backupRelative
   [IO.Directory]::CreateDirectory((Split-Path $backup -Parent)) | Out-Null;[IO.File]::Copy($target,$backup,$false)
   $record.backup=$backupRelative;$record.original_hash=FileHash $backup
  }
 }
 if ($BasePath) {$record.base_path=$BasePath};$record.installed_hash=FileHash $Staged
 if ($PatchHash) {$record.patch_hash=$PatchHash}
 $State.pending=@{relative=$Relative;previous=$old;next=$record};Save $StateFile $State
 $previousBytes=$null;if (Test-Path -LiteralPath $target) {$previousBytes=[IO.File]::ReadAllBytes($target)}
 try {Put $target ([IO.File]::ReadAllBytes($Staged));if ((FileHash $target) -ne $record.installed_hash) {throw 'Installed hash mismatch'}}
 catch {if ($null -ne $previousBytes) {Put $target $previousBytes} elseif (Test-Path -LiteralPath $target) {[IO.File]::Delete($target)};$State.Remove('pending');Save $StateFile $State;throw}
 $State.files[$Relative]=$record;$State.Remove('pending');Save $StateFile $State
}
function CheckBase([string]$Base,$Manifest) {
 $expectedHash=$Manifest.base_zip_sha256
 if ($expectedHash -isnot [string] -or $expectedHash -cnotmatch '^[0-9a-f]{64}$') {throw 'Missing or invalid base ZIP SHA-256; use an updated patch'}
 if ($Manifest.base_hash_policy -and $Manifest.base_hash_policy -ne 'exact-zip') {throw 'Unsupported base hash policy'}
 $actualHash=FileHash $Base
 if ($actualHash -cne $expectedHash) {throw "Base ZIP hash mismatch: expected $expectedHash, found $actualHash. Different mod/game version or repacked ZIP; skipped without changes."}
}
function Rebuild([string]$Base,$Manifest,[string]$PatchFolder,[string]$Destination) {
 CheckBase $Base $Manifest
 $zip=[IO.Compression.ZipFile]::OpenRead($Base)
 try {
  $expected=@{};foreach ($p in $Manifest.base_entries.PSObject.Properties) {$expected[$p.Name]=$p.Value}
  if ($zip.Entries.Count -ne $expected.Count) {throw 'Base ZIP file list mismatch'}
  $seen=@{};$total=0
  foreach ($entry in $zip.Entries) {
   [void](Safe $Package $entry.FullName);$total+=$entry.Length
   if ($total -gt 536870912 -or $seen.ContainsKey($entry.FullName)) {throw 'Oversize or duplicate archive entry'};$seen[$entry.FullName]=$true
   if (!$expected.ContainsKey($entry.FullName) -or (Hash (ReadEntry $zip $entry.FullName)) -ne $expected[$entry.FullName]) {throw "Base asset mismatch: $($entry.FullName)"}
  }
  $outputs=@{}
  foreach ($record in $Manifest.outputs) {
   [void](Safe $Package $record.entry);if ($outputs.ContainsKey($record.entry)) {throw 'Duplicate patch output'}
   $data=New-Object byte[] 0;$reference=$record.base
   if ($reference) {
    if ($reference.archive.Contains('/') -or !$reference.archive.EndsWith('.zip',[StringComparison]::OrdinalIgnoreCase)) {throw 'Invalid donor archive'}
    if ($reference.location -eq 'active') {$source=$Base}
    elseif ($reference.location -in @('media','mediapc')) {$source=Safe $Game ($reference.location+'/Cars/'+$reference.archive)}
    else {throw 'Unsupported dependency location'}
    $donor=[IO.Compression.ZipFile]::OpenRead($source);try {$data=ReadEntry $donor $reference.entry} finally {$donor.Dispose()}
    if ((Hash $data) -ne $reference.sha256) {throw "Donor asset version mismatch: $($reference.archive)"}
   }
   $patch=[IO.File]::ReadAllBytes((Safe $PatchFolder $record.delta));if ((Hash $patch) -ne $record.delta_sha256) {throw 'Patch payload damaged'}
   $new=Delta $data $patch;if ($new.Length -ne $record.size -or (Hash $new) -ne $record.sha256) {throw 'Reconstructed asset hash mismatch'}
   $outputs[$record.entry]=$new
  }
  $out=[IO.Compression.ZipFile]::Open($Destination,[IO.Compression.ZipArchiveMode]::Create)
  try {
   foreach ($entry in $zip.Entries) {
    $name=$entry.FullName;[byte[]]$bytes=$null;if ($outputs.ContainsKey($name)) {$bytes=$outputs[$name]} else {$bytes=ReadEntry $zip $name}
    $level=if ($name.ToLowerInvariant() -in @('buildnumber.txt','carbuildtime.txt')) {[IO.Compression.CompressionLevel]::NoCompression} else {[IO.Compression.CompressionLevel]::Optimal}
    $newEntry=$out.CreateEntry($name,$level);$stream=$newEntry.Open();try {$stream.Write($bytes,0,$bytes.Length)} finally {$stream.Dispose()};$outputs.Remove($name)
   }
   foreach ($name in $outputs.Keys) {$e=$out.CreateEntry($name,[IO.Compression.CompressionLevel]::Optimal);$stream=$e.Open();try {$bytes=$outputs[$name];$stream.Write($bytes,0,$bytes.Length)} finally {$stream.Dispose()}}
  } finally {$out.Dispose()}
 } finally {$zip.Dispose()}
 $check=[IO.Compression.ZipFile]::OpenRead($Destination)
 try {
  foreach ($p in $Manifest.base_entries.PSObject.Properties) {
   $record=@($Manifest.outputs | Where-Object {$_.entry -ceq $p.Name});$hash=if ($record.Count) {$record[0].sha256} else {$p.Value}
   if ((Hash (ReadEntry $check $p.Name)) -ne $hash) {throw 'Built ZIP verification failure'}
  }
  foreach ($r in $Manifest.outputs) {if ((Hash (ReadEntry $check $r.entry)) -ne $r.sha256) {throw 'Added asset verification failure'}}
 } finally {$check.Dispose()}
}
$ExitCode=0
Write-Host 'FH6 Front Plates'
try {
 if (!$Game) {$Game=Split-Path $Package -Parent};$Game=[IO.Path]::GetFullPath($Game).TrimEnd('\','/')
 if (!(Test-Path -LiteralPath (Join-Path $Game 'forzahorizon6.exe'))) {throw 'Extract FH6FrontPlates inside the folder containing forzahorizon6.exe first'}
 if ($Command -ne 'status' -and (Get-Process -Name forzahorizon6 -ErrorAction SilentlyContinue)) {throw 'Close Forza before installing or disabling the mod'}
 $prefix=$Game+[IO.Path]::DirectorySeparatorChar
 if (!$Package.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)) {throw 'Extract FH6FrontPlates inside the game folder first'}
 $relativePackage=$Package.Substring($prefix.Length).Replace('\','/')
 $StateFile=Safe $Package 'install-state/state.json';[IO.Directory]::CreateDirectory((Split-Path $StateFile -Parent)) | Out-Null
 $State=@{schema=1;game=$Game;backup_root=$relativePackage+'/install-state/backups';files=@{}}
 if (Test-Path -LiteralPath $StateFile) {
  $saved=Get-Content -Raw -LiteralPath $StateFile | ConvertFrom-Json
  if ($saved.game -ne $Game -or $saved.schema -ne 1) {throw 'Installation record belongs to another folder or version'}
  $State.backup_root=$saved.backup_root;foreach ($p in $saved.files.PSObject.Properties) {$State.files[$p.Name]=$p.Value}
  if ($saved.pending) {
   $p=$saved.pending;$target=Safe $Game $p.relative;$current=if (Test-Path -LiteralPath $target) {FileHash $target} else {$null}
   if ($current -eq $p.next.installed_hash) {$State.files[$p.relative]=$p.next}
   elseif ($current -and $current -ne $p.previous.installed_hash -and $current -ne $p.next.original_hash) {throw 'Interrupted installation has an unexpected file; preserving it'}
   Save $StateFile $State
  }
 }
 $Data=if (Test-Path -LiteralPath (Join-Path $Package 'mod')) {Safe $Package 'mod'} else {$Package}
 $RuntimeDir=if (Test-Path -LiteralPath (Join-Path $Data 'runtime')) {Safe $Data 'runtime'} else {$Data}
 $runtime=Get-Content -Raw -LiteralPath (Join-Path $Data 'runtime.json') | ConvertFrom-Json
 $report=@{installed=@();unchanged=@();skipped=@();restored=@()}
 if ($Command -eq 'status') {
  foreach ($n in $State.files.Keys) {$target=Safe $Game $n;if ((Test-Path -LiteralPath $target) -and (FileHash $target) -eq $State.files[$n].installed_hash) {$report.installed+=,$n} else {$report.skipped+=,"$n : changed or missing"}}
 } elseif ($Command -eq 'disable') {
  $ordered=@($State.files.Keys | Sort-Object @{Expression={if ($_.StartsWith('mediapc/Cars/')) {0} else {1}}},@{Expression={$_}})
  foreach ($n in $ordered) {
   if (!$n.StartsWith('mediapc/Cars/') -and @($State.files.Keys | Where-Object {$_.StartsWith('mediapc/Cars/')}).Count) {$report.skipped+=,($n+': retained because a car could not be restored');continue}
   $record=$State.files[$n];$target=Safe $Game $n
   if (!(Test-Path -LiteralPath $target) -or (FileHash $target) -ne $record.installed_hash) {$report.skipped+=,"$n : changed after installation";continue}
   if ($record.backup) {Put $target ([IO.File]::ReadAllBytes((Original $record)))} else {[IO.File]::Delete($target)}
   $State.files.Remove($n);Save $StateFile $State;$report.restored+=,$n
  }
 } else {
  if ($runtime.game_sha256 -and (FileHash (Join-Path $Game 'forzahorizon6.exe')) -ne $runtime.game_sha256) {throw 'Unsupported game version; nothing installed'}
  $loader=Safe $Game 'version.dll';$helper=Safe $Game 'platesync-version.dll'
  $expected=$runtime.files.'version.dll'
  $standalone=(Test-Path -LiteralPath $loader) -and (FileHash $loader) -eq $expected
  $companion=(Test-Path -LiteralPath $helper) -and (FileHash $helper) -eq $expected
  if ($runtime.require_loader -and $standalone) {throw 'PlateSync requires Multiverse. Rename this pack DLL platesync-version.dll and keep Multiverse version.dll in the game folder.'}
  if ($standalone -and (Test-Path -LiteralPath $helper) -and (FileHash $helper) -in $runtime.compatible_helpers) {throw 'Two PlateSync DLL copies detected. Keep only version.dll standalone OR platesync-version.dll with Multiverse.'}
  if (!$standalone -and !$companion) {throw 'PlateSync DLL missing or wrong version. Copy the included version.dll beside the game executable, or rename it platesync-version.dll when using Multiverse.'}
  if (!$standalone -and (!(Test-Path -LiteralPath $loader) -or (FileHash $loader) -notin $runtime.compatible_loaders)) {throw 'Install a compatible Multiverse loader first. Existing version.dll is preserved.'}
  # Resolve and validate destinations serially; workers never touch live files or install records.
  $jobs=New-Object 'System.Collections.Generic.List[object]';$targets=@{}
  $folders=@(Get-ChildItem -LiteralPath (Join-Path $Data 'patches') -Directory);$total=$folders.Count
  ShowInstallProgress 0 $total 'Reading car patches...'
  foreach ($folder in $folders) {
   try {
    $manifestPath=Join-Path $folder.FullName 'manifest.json';$manifest=Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
    if ($manifest.schema -ne 1) {throw 'Unsupported patch schema'}
    $filename=$manifest.filename;if ($filename.Contains('/') -or !$filename.EndsWith('.zip',[StringComparison]::OrdinalIgnoreCase)) {throw 'Invalid car filename'}
    $relative='mediapc/Cars/'+$filename;[void](Safe $Game $relative)
    $jobs.Add(@{folder=$folder.FullName;name=$folder.Name;manifest=$manifest;filename=$filename;relative=$relative;patchHash=(FileHash $manifestPath)})
    if (!$targets.ContainsKey($relative)) {$targets[$relative]=0};$targets[$relative]++
   } catch {$report.skipped+=,($folder.Name+': '+$_.Exception.Message)}
  }
  $workerCount=[Math]::Min($Workers,[Math]::Max(1,[Environment]::ProcessorCount));if (![Environment]::Is64BitProcess) {$workerCount=1}
  $report.workers=$workerCount
  $initial=[System.Management.Automation.Runspaces.InitialSessionState]::CreateDefault()
  foreach ($fn in @('Safe','Hash','FileHash','ReadEntry','Delta','CheckBase','Rebuild')) {
   $definition=(Get-Command $fn).Definition
   $entry=New-Object System.Management.Automation.Runspaces.SessionStateFunctionEntry -ArgumentList @($fn,$definition)
   $initial.Commands.Add($entry)
  }
  $pool=[System.Management.Automation.Runspaces.RunspaceFactory]::CreateRunspacePool(1,$workerCount,$initial,$Host);$pool.Open()
  $done=$report.skipped.Count
  ShowInstallProgress $done $total 'Checking files...'
  try {
   for ($start=0;$start -lt $jobs.Count;$start+=$workerCount) {
    $batch=@($jobs | Select-Object -Skip $start -First $workerCount);$running=New-Object 'System.Collections.Generic.List[object]'
    try {
     foreach ($job in $batch) {
      try {
       if ($targets[$job.relative] -gt 1) {throw 'Multiple patches target the same car ZIP; no variant was selected'}
       $target=Safe $Game $job.relative;$prior=$State.files[$job.relative]
       $job.targetHash=if (Test-Path -LiteralPath $target) {FileHash $target} else {$null}
       if ($prior) {if ($job.targetHash -ne $prior.installed_hash) {throw 'Installed ZIP changed later'};$base=Original $prior}
       else {$base=$target;if (!(Test-Path -LiteralPath $base)) {$base=Safe $Game ('media/Cars/'+$job.filename)}}
       $actual=if (($prior -and $prior.backup) -or (!$prior -and (Split-Path (Split-Path (Split-Path $base -Parent) -Parent) -Leaf) -eq 'mediapc')) {'mediapc'} else {'media'}
       if ($job.manifest.base_location -notin @('either',$actual)) {throw "Patch expects $($job.manifest.base_location) base"}
       if ($prior -and $prior.patch_hash -eq $job.patchHash) {
        CheckBase $base $job.manifest
        foreach ($r in $job.manifest.outputs) {if ((FileHash (Safe $job.folder $r.delta)) -ne $r.delta_sha256) {throw 'Patch payload damaged'}}
        $report.unchanged+=,$job.filename;$done++;ShowInstallProgress $done $total "Already up to date: $($job.filename)";continue
       }
       $job.baseRelative=$base.Substring($prefix.Length).Replace('\','/')
       $job.temp=Safe $Package ('install-state/build-'+[guid]::NewGuid().ToString('N')+'.zip')
       $shell=[PowerShell]::Create();$shell.RunspacePool=$pool
       [void]$shell.AddScript('param($g,$p,$b,$m,$f,$d) $ErrorActionPreference="Stop"; $Game=$g; $Package=$p; Rebuild $b $m $f $d').AddArgument($Game).AddArgument($Package).AddArgument($base).AddArgument($job.manifest).AddArgument($job.folder).AddArgument($job.temp)
       $job.shell=$shell;$job.handle=$shell.BeginInvoke();$running.Add($job)
       ShowInstallProgress $done $total "Preparing $($running.Count) car(s)..."
      } catch {$report.skipped+=,($job.name+': '+$_.Exception.Message);$done++}
     }
     # Keep the single progress display alive while the read-only workers finish.
     while (@($running | Where-Object {!$_.handle.IsCompleted}).Count -gt 0) {
      ShowInstallProgress $done $total 'Preparing and checking car files...'
      Start-Sleep -Milliseconds 200
     }
     # Finish the bounded batch before committing: donor ZIP readers cannot collide with replacements.
     foreach ($job in $running) {
      try {[void]$job.shell.EndInvoke($job.handle)} catch {$job.error=$_.Exception.Message} finally {$job.shell.Dispose()}
     }
     foreach ($job in $running) {
      try {
       if ($job.error) {throw $job.error}
       $target=Safe $Game $job.relative;$currentHash=if (Test-Path -LiteralPath $target) {FileHash $target} else {$null}
       if ($currentHash -ne $job.targetHash) {throw 'Car ZIP changed while preparing; preserving the later change'}
       InstallFile $job.relative $job.temp $job.baseRelative $job.patchHash
       $report.installed+=,$job.filename;$done++;ShowInstallProgress $done $total "Installed $($job.filename)"
      } catch {$report.skipped+=,($job.name+': '+$_.Exception.Message);$done++}
     }
    } finally {
     foreach ($job in $running) {
      if ($job.shell -and $job.shell.InvocationStateInfo.State -eq 'Running') {$job.shell.Stop();$job.shell.Dispose()}
      if ($job.temp -and (Test-Path -LiteralPath $job.temp)) {[IO.File]::Delete($job.temp)}
     }
    }
    ShowInstallProgress $done $total 'Finishing car installation...'
   }
  } finally {$pool.Close();$pool.Dispose()}
  ShowInstallProgress $done $total 'Finishing setup...'
  if ($report.installed.Count -or $report.unchanged.Count) {
   $config=Safe $Game 'PlateSync/diagnostic.ini'
   $text=if (Test-Path -LiteralPath $config) {[IO.File]::ReadAllText($config)} else {''}
   $lines=New-Object 'System.Collections.Generic.List[string]';$inside=$false;$found=$false;$keys=@{}
   foreach ($lineValue in ($text -split '\r?\n')) {
    $line=$lineValue
    if ($line -match '^\s*\[([^]]+)\]') {
     $section=$Matches[1]
     if ($inside) {foreach ($key in @('Enabled','AutoAttach')) {if (!$keys.ContainsKey($key)) {$lines.Add($key+'=1')}}}
     $inside=$section -eq 'profiles';if ($inside) {$found=$true}
    }
    if ($inside -and $line -match '^\s*(Enabled|AutoAttach)\s*=') {$key=$Matches[1];$keys[$key]=$true;$line=$key+'=1'}
    $lines.Add($line)
   }
   if ($inside) {foreach ($key in @('Enabled','AutoAttach')) {if (!$keys.ContainsKey($key)) {$lines.Add($key+'=1')}}}
   if (!$found) {$lines.Add('[profiles]');$lines.Add('Enabled=1');$lines.Add('AutoAttach=1')}
   $desired=($lines -join "`r`n").TrimEnd("`r","`n")+"`r`n"
   if (!(Test-Path -LiteralPath $config) -or [IO.File]::ReadAllText($config) -cne $desired) {
    $default=Safe $Package 'install-state/default.ini';[IO.File]::WriteAllText($default,$desired);InstallFile 'PlateSync/diagnostic.ini' $default
   }
  }
 }
 Write-Progress -Id 1 -Activity 'Installing FH6 Front Plates' -Completed
 Save (Safe $Package 'last-install-report.json') $report
 if ($Command -eq 'enable') {Write-Host "Finished: $($report.installed.Count) cars installed, $($report.unchanged.Count) already up to date, $($report.skipped.Count) skipped."}
 elseif ($Command -eq 'disable') {Write-Host "Finished: $($report.restored.Count) files restored, $($report.skipped.Count) skipped."}
 else {Write-Host "Checked: $($report.installed.Count) files correct, $($report.skipped.Count) changed or missing."}
 foreach ($message in @($report.skipped | Select-Object -First 5)) {Write-Host "  SKIPPED: $message"}
 if ($report.skipped.Count -gt 5) {Write-Host "  Plus $($report.skipped.Count-5) more; see last-install-report.json."}
 if ($report.skipped.Count) {$ExitCode=1}
 Write-Host 'Details: last-install-report.json. Keep FH6FrontPlates while installed; it contains your backups.'
} catch {Write-Host "ERROR: $($_.Exception.Message)";$ExitCode=1} finally {Write-Progress -Id 1 -Activity 'Installing FH6 Front Plates' -Completed}
if (!$NoPause) {[void](Read-Host 'Press Enter to close')}
exit $ExitCode

