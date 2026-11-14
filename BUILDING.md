# Building PlateSync

## Requirements

- 64-bit Windows and PowerShell 5.1 or later.
- **Zig 0.14.1** for Windows x86-64, available from https://ziglang.org/download/.

Extract Zig anywhere on your computer. Visual Studio, a separate Windows SDK and
game files are not required to compile. The miniz source is included under
`src/miniz/`, together with its license and provenance.

## Compile

Open PowerShell in the repository folder and run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\src\build.ps1 -ZigPath "C:\Tools\Zig-0.14.1\zig.exe"
```

Replace the example path with your compiler's `zig.exe`. If Zig is already on
PATH, omit `-ZigPath`. The script shows the complete compiler/linker invocation
and stops on a compiler failure.

The output is **`platesync-version.dll`** in the repository folder. Car packs may
ship the same bytes under `version.dll`; use the companion name with Multiverse.
No third-party loader is included in this repository.

## Verify the output

The working **0.17.8** DLL is **49,664 bytes**, with this SHA-256:

```text
b947c4441cf5031343c50ed395a245a2115cf53167df2f07a088bcb0bd149b86
```

```powershell
Get-FileHash .\platesync-version.dll -Algorithm SHA256
```

The documented build was tested with Zig 0.14.1 on Windows and reproduced that
binary byte-for-byte. Different compilers or flags may produce a different
binary. Matching a hash identifies the file; it does not establish software safety.

## Installer scripts

`installer/Install.ps1` and `installer/mod/installer.py` are plain source and do
not need compiling. The adjacent launch scripts invoke them. They require a
complete car pack, including its runtime manifest and per-car patches, to install
anything; those game-derived files are not part of this source repository.

The checked-in installer source matches the FH6 Front Plates 0.01 download,
built with Workshop 0.28.6. It checks baseline ZIP hashes, prepares car overlays
in parallel, preserves backups and records installed hashes. Repeat installs
can reuse unchanged cars.

## Runtime notes

The DLL reads compatible car profiles, updates plate-related visual selection
and writes diagnostics under `PlateSync/diagnostics` in the game folder. It
requires the supported executable and native-layout checks to pass. Building
the DLL does not require or launch the game. In-game testing is a separate step.
