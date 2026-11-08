# Reading the C++ code

Start in `src/main.cpp`. It is deliberately arranged in reading order: geometry and world data, gameplay, drawing, regression checks, Windows presentation, then application startup. A file header describes the coordinate system and each major function explains its purpose.

## The main pieces

| Piece | Responsibility |
| --- | --- |
| `Vec2`, `Rect` | Positions, velocities and collision boxes |
| `RoomId`, `Direction`, `BossState` | Named values used instead of unexplained numeric states |
| `WorldRooms` | Names, hints and biome IDs for the nine rooms |
| `Game` | Journey state, collision, combat, chests, boss and persistence |
| `Painter` | Basic GDI shapes and text, with cached brushes and fonts |
| `drawHero`, `drawWorld`, `render` | Sprites, world drawing, HUD and menu overlays |
| `Presenter` | Combines the scene and letterbox bars off-screen, then presents one completed frame |
| `handleWindowMessage` | Windows events, discrete actions and `WM_PAINT` |
| `wWinMain` | Startup, message handling, continuous movement and the frame loop |

## Coordinates and time

A room is 24 columns by 14 rows, with 40-pixel tiles. Gameplay positions start at the room's upper-left corner; Y increases downward. Drawing adds `HudHeight` to Y because the status bar sits above the room. World room IDs run left-to-right and top-to-bottom through the 3-by-3 map. Keep their numeric values stable so existing saves remain valid.

`deltaTime` and all countdowns are in seconds. Most velocities are pixels per second. The frame loop splits elapsed time into small simulation steps so movement cannot skip a solid tile. Diagonal input is normalized so it does not increase movement speed.

## Following one frame

1. Windows messages handle presses such as sword, interact, map and pause.
2. The loop polls held keys for movement.
3. `Game::update` advances countdowns and particles, handles respawn, moves the player, checks doors, updates enemies and the boss, then checks projectiles.
4. `render` draws a complete scene into a memory bitmap. Overlays select the title, map or pause screen without changing game state.
5. `WM_PAINT` asks `Presenter` to compose the client-sized frame off-screen and copy it to the window once. Do not clear the visible window before that copy; doing so brings back flickering.

## Gameplay rules to look for

- `movePlayer` resolves horizontal and vertical collision separately, allowing sliding along a wall.
- `tryRoomTransition` checks the three-seal gate and boss arena lock before changing rooms.
- `swingSword` starts a limited attack window. `hitTargets` ensures each enemy takes damage once per swing. Removing defeated enemies waits until the swing ends so their indices remain stable.
- `damagePlayer` applies knockback and temporary invulnerability, then starts the respawn countdown if health reaches zero.
- `interact` handles healing at the village lantern, guarded chests, heart vessels and hints.
- `updateBoss` alternates a telegraphed charge and projectile ring. The second phase shortens warnings and adds projectiles.

## Saves and resources

`save` writes a temporary file and replaces `saves/progress.txt` atomically. `load` validates local values before changing the active journey. Sunseals are derived from opened shrine chests rather than a separately saved count. Enemy positions and projectiles are room-local and reset on entry.

GDI objects must not be deleted while selected into a device context. `Painter` restores temporarily selected objects; `Presenter` owns its client bitmap and restores the original object before cleanup. Its bitmap is recreated only when the client size changes.

## Build, format and check

- Run `Build.bat` to rebuild the executable.
- `.clang-format` defines four-space indentation, a 100-column limit, explicit braces and expanded control flow. If clang-format is installed, run `clang-format -i src/main.cpp` after edits. A formatter is a development tool, not a runtime dependency.
- Run `build/Zeldoh.exe --self-test` as documented in `README.md`. The tests use `build/test-save/`, then record gameplay and rendering results in `self-test-results.txt`.
- The presentation check covers 100 resizes, complete letterbox coverage, minimized dimensions and stable GDI resource use.
