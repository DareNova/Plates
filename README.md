# PlateSync

PlateSync is the DLL used by **Front Plates Overhaul**. It keeps the player's front
number plate placement in sync with the selected bumper, body kit and plate-position
upgrade. Car-specific placements are read from profiles inside the modded car ZIPs.

## Installation

Use the Front Plates Overhaul car pack and follow its included instructions.

1. Close Forza Horizon 6.
2. Set up [Multiverse](https://www.nexusmods.com/forzahorizon6/mods/539), or use
   its built-in support in CZT Mod Manager.
3. Place `platesync-version.dll` beside `forzahorizon6.exe`. If the car pack calls
   this DLL `version.dll`, rename it first. **Keep Multiverse's `version.dll`.**
4. Run the car pack's `Enable mod.cmd`, then start the game.

The DLL needs compatible car exports; installing it alone does not add plates.
This repository contains source code, not the car pack or game assets.

## Build

Use **Zig 0.14.1** on 64-bit Windows. From the repository folder:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\src\build.ps1 -ZigPath "C:\Tools\Zig-0.14.1\zig.exe"
```

Change the compiler path to your `zig.exe`. The output is `platesync-version.dll`.
See [BUILDING.md](BUILDING.md) for build details and the verified DLL hash.

## Source layout

- `src/` — PlateSync 0.17.8, native tests and the bundled miniz decoder.
- `installer/` — the Windows and portable installer scripts used by the car pack.
  Car patches are supplied separately in the mod download.

## Compatibility

Currently validated against one Steam executable build. Other builds, including
Xbox app/Microsoft Store, are unverified. The DLL checks the executable before
activating. Automatic syncing follows the player's car; a visual refresh can
occasionally be delayed after changing cars or parts.

## Online play

Using modified game files online may lead to account restrictions, suspension or
a ban. Cosmetic changes are not guaranteed safe. Use at your own risk.

Not affiliated with Microsoft or Playground Games. See [LICENSE.txt](LICENSE.txt)
and [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt) for licensing.
