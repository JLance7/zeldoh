# Zeldoh: Lantern of the Vale

A small, original top-down tile adventure written in **C++17** for **Windows 10/11**. Explore nine connected screens, fight monsters with a sword, open guarded chests, collect three Sunseals, and face the Stonewing Warden. Art, names and world are original; no Nintendo assets are included.

## Play

Double-click **Play.bat**, or run **build/Zeldoh.exe**. The included executable is ready to play, with no installation, Python, accounts or third-party runtime downloads needed. Keep the executable inside `build/` so saves are stored in this project. The window can be resized; the playfield keeps its aspect ratio.

| Action | Keys |
| --- | --- |
| Move | WASD or arrow keys |
| Swing sword in facing direction | Space, J, or Z |
| Open a chest / rest at lantern / inspect room | E |
| Toggle discovered world map | M or Tab |
| Pause / resume / close map | Escape |
| Return to village while paused | R |
| Save and quit while paused | Q |
| Choose title menu option | Up / Down, W / S; Enter to select |

Defeat every enemy in a shrine to unbind its chest, then stand close and press E. Search the northwest **Fern Sanctuary**, northeast **Tideglass Ruins**, and southwest **Ember Chapel** for Sunseals. All three unlock the southeast vault. Additional heart vessels are hidden in Willow Reach and Ashen Causeway.

Walk through the openings in the middle of screen edges to travel. Trees, rocks and water block movement. Diagonal movement has the same speed as straight movement. Sword swings can destroy enemy projectiles. Watch for the Warden's warning before its charge or ring attack; below half health it attacks more quickly. After victory, the world remains open for exploration.

## Saves

Rest beside the village lantern with E to heal and save. Opening chests, winning, closing an active game, and Q from pause also save. Progress is stored in **saves/progress.txt**, with an atomic temporary-file replacement. Continue returns you to the village with full health, retaining collected chests, Sunseals, amber, discovery and victory. Enemies respawn when you reenter a room. Death returns to the village without removing collected upgrades. Map and pause screens suspend gameplay; switching away from the window pauses the game.

New Journey starts fresh and replaces the previous save when that run next saves. Invalid saves show a message instead of crashing. The automated checks use a separate file under `build/test-save/`.

## Build

The supplied **Build.bat** / **build.ps1** uses MinGW-w64 GCC from PATH or a standard MSYS2 installation. No package manager dependencies are needed: only the C++ standard library and the Windows GDI, User32 and GDI+ system libraries.

```powershell
.\build.ps1
```

Alternative: CMake with Visual Studio 2022 and the Desktop Development with C++ workload:

```powershell
cmake -S . -B build-vs
cmake --build build-vs --config Release
```

For a CMake build, place the resulting executable in this project's `build/` folder before playing so it uses the documented save location. Source is in `src/main.cpp`. The game is Windows-native; it does not currently target Linux or macOS.

## Verify

```powershell
$process = Start-Process -FilePath .\build\Zeldoh.exe -ArgumentList '--self-test' -WindowStyle Hidden -Wait -PassThru
$process.ExitCode
Get-Content .\self-test-results.txt
```

Exit code zero means the movement, walls, locked vault, chest conditions, Sunseal and heart collection, sword combat, damage invulnerability, respawn, boss attacks/victory, save round-trip, and room-clearance checks passed. Test mode also renders PNG previews of gameplay, map, boss and title into `screenshots/`. It does not change your player save. There is no telemetry, network access, sound, or external asset dependency.

## Understanding the source

See [CODE_GUIDE.md](CODE_GUIDE.md) for the code structure, coordinate system, gameplay flow, saves, and Windows rendering. Source uses descriptive names, explanatory comments, and the formatting rules in .clang-format.
