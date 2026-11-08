#ifndef UNICODE
#define UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <gdiplus.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

// Zeldoh: Lantern of the Vale.
//
// Reading order:
//   1. Geometry and world definitions
//   2. Game state, movement, combat and saving
//   3. Drawing helpers and screen layouts
//   4. Gameplay and presentation regression checks
//   5. Flicker-free presentation and Windows input
//   6. Application startup and the frame loop
//
// Coordinates: gameplay positions are pixels inside a room, with (0, 0) at
// its upper-left corner. Drawing adds HudHeight to Y to make room for the HUD.
// Room IDs count left-to-right, top-to-bottom across the 3-by-3 world map.
// Timers and deltaTime/deltaTime values are measured in seconds.
// Artwork is original, drawn from rectangles rather than external assets.
// The engine has no third-party libraries and no network or account dependency.
namespace fs = std::filesystem;
constexpr int WindowWidth = 960, WindowHeight = 640, TileSize = 40, RoomColumns = 24, RoomRows = 14,
              HudHeight = 80;
constexpr double Pi = 3.141592653589793;
const COLORREF Ink = RGB(17, 25, 37), Cream = RGB(243, 231, 196), Gold = RGB(242, 194, 102);
const COLORREF Mint = RGB(127, 222, 186), Red = RGB(232, 108, 120), Muted = RGB(157, 176, 177);

// Named values keep direction, room and boss logic readable without changing
// the numeric room IDs used by existing progress files.
namespace Direction {
constexpr int Up = 0;
constexpr int Right = 1;
constexpr int Down = 2;
constexpr int Left = 3;
} // namespace Direction
namespace RoomId {
constexpr int FernSanctuary = 0;
constexpr int WillowReach = 1;
constexpr int TideglassRuins = 2;
constexpr int MosswoodTrail = 3;
constexpr int Village = 4;
constexpr int SaffronMarsh = 5;
constexpr int EmberChapel = 6;
constexpr int AshenCauseway = 7;
constexpr int WardenVault = 8;
} // namespace RoomId
namespace BossState {
constexpr int Resting = 0;
constexpr int Warning = 1;
constexpr int Charging = 2;
} // namespace BossState

// A position, direction, or velocity in two-dimensional pixel space.
struct Vec2 {
    double x{}, y{};
};
// Axis-aligned collision rectangle; x/y are the upper-left corner.
struct Rect {
    double x{}, y{}, w{}, h{};
};
bool overlaps(Rect a, Rect b) {
    return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}
double distance(Vec2 a, Vec2 b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}
double bounded(double n, double a, double b) {
    return std::max(a, std::min(b, n));
}
// Enemy types: 0 = pursuing slime, 1 = flying bat, 2 = shooting sentry.
struct Enemy {
    Vec2 position, spawnPosition;
    int type{}, health{3};
    double actionTimer{}, hitFlashTimer{};
};
struct Shot {
    Vec2 position, velocity;
    double life{4};
};
struct Spark {
    Vec2 position, velocity;
    COLORREF color;
    double life;
};
struct Room {
    std::wstring name, hint;
    int biome{};
};
// Fixed room names, hints and biome IDs: 0 forest, 1 marsh, 2 ruins.
const std::array<Room, 9> WorldRooms{
    {{L"Fern Sanctuary", L"A Sunseal sleeps in the ancient chest.", 0},
     {L"Willow Reach", L"Small feet can travel a very long way.", 0},
     {L"Tideglass Ruins", L"The tide has left something precious behind.", 1},
     {L"Mosswood Trail", L"Face a foe, then strike with your sword.", 0},
     {L"Lantern Village", L"The lantern restores your hearts and remembers your journey.", 0},
     {L"Saffron Marsh", L"Three Sunseals will open the eastern vault.", 1},
     {L"Ember Chapel", L"The last seal waits beneath a fallen roof.", 2},
     {L"Ashen Causeway", L"The Warden was built to protect the light.", 2},
     {L"Vault of the Warden", L"The stone wings stir. Bring the dawn home.", 2}}};

// GAMEPLAY: owns the current journey. Drawing reads this state but never
// changes it; update() is the only per-frame simulation entry point.
struct Game {
    // Journey values retained by save()/load().
    fs::path root;
    fs::path saveFile;
    int maxHealth{6};
    int amber{};
    bool won{};
    std::set<int> visited;
    std::set<int> opened;

    // Current screen and player state. Continue always starts in the village.
    int roomIndex{RoomId::Village};
    int facing{Direction::Down};
    int health{6};
    Vec2 player{468, 280};
    Vec2 knockback{};
    bool title{true};
    bool paused{};
    bool mapOpen{};
    int menu{};

    // Room-local objects are rebuilt by enterRoom(). Tile values are
    // 0 = walkable ground, 1 = wall/tree, 2 = impassable water.
    std::array<int, RoomColumns * RoomRows> tiles{};
    std::vector<Enemy> enemies;
    std::vector<Shot> shots;
    std::vector<Spark> sparks;

    // All timers use seconds. elapsedTime increases for animation; the others
    // decrease to zero. hitTargets identifies entities struck by the current swing.
    double elapsedTime{};
    double invulnerabilityTimer{};
    double swordSwingTimer{};
    double swordCooldownTimer{};
    double respawnTimer{};
    double roomBannerTimer{};
    double toastTimer{};
    double transitionTimer{};
    std::set<int> hitTargets;
    std::wstring toast;
    std::wstring saveError;

    // Boss state is active only inside an uncleared Warden vault.
    bool bossActive{};
    Vec2 boss{680, 280};
    Vec2 bossChargeVelocity{};
    int bossHealth{28};
    int bossAttackState{BossState::Resting};
    int bossAttackCount{};
    double bossActionTimer{1.4};
    double bossHitFlashTimer{};
    std::mt19937 rng{15081};

    explicit Game(fs::path directory)
        : root(std::move(directory)), saveFile(root / L"saves" / L"progress.txt") {
        reset();
    }

    // The hero sprite is larger than its collision box to make movement forgiving.
    Rect playerHitbox() const {
        return {player.x - 12, player.y - 10, 24, 24};
    }

    // Derive seals from opened shrine chests, so there is no second count to sync.
    int sealCount() const {
        return int(opened.count(RoomId::FernSanctuary) + opened.count(RoomId::TideglassRuins) +
                   opened.count(RoomId::EmberChapel));
    }
    void showMessage(const std::wstring& s, double duration = 3.8) {
        toast = s;
        toastTimer = duration;
    }

    // Start a fresh journey in memory. Saving happens only at explicit save points.
    void reset() {
        health = maxHealth = 6;
        amber = 0;
        won = false;
        visited.clear();
        opened.clear();
        mapOpen = paused = false;
        invulnerabilityTimer = swordSwingTimer = swordCooldownTimer = respawnTimer = 0;
        menu = 0;
        enterRoom(RoomId::Village, {468, 280});
    }

    // Convert a cardinal map offset into an adjacent room ID, rejecting world edges.
    bool getNeighborRoom(int dx, int dy, int& next) const {
        int x = roomIndex % 3 + dx, y = roomIndex / 3 + dy;
        if (x < 0 || x >= 3 || y < 0 || y >= 3) {
            return false;
        }
        next = y * 3 + x;
        return true;
    }

    // Rebuild a room on entry. Its terrain seed is fixed, while enemies respawn.
    // Persistent discoveries and opened chests belong to the journey, not this room.
    void enterRoom(int target, Vec2 position) {
        roomIndex = target;
        visited.insert(roomIndex);
        player = position;
        knockback = {};
        shots.clear();
        sparks.clear();
        enemies.clear();
        hitTargets.clear();
        swordSwingTimer = 0;
        roomBannerTimer = 3;
        transitionTimer = .35;
        std::mt19937 terrain(1709u + unsigned(roomIndex) * 71u);
        tiles.fill(0);
        for (int y = 0; y < RoomRows; ++y) {
            for (int x = 0; x < RoomColumns; ++x) {
                bool border = x == 0 || x == RoomColumns - 1 || y == 0 || y == RoomRows - 1;
                int value = border ? 1 : 0;
                if (!border && roomIndex != RoomId::Village && roomIndex != RoomId::WardenVault &&
                    x > 2 && x < 21 && y > 1 && y < 12 && terrain() % 12 == 0) {
                    value = WorldRooms[roomIndex].biome == 1 ? 2 : 1;
                }
                // All room exits and chests have guaranteed clear connecting paths.
                if ((x == 11 || x == 12) || (y == 6 || y == 7)) {
                    value = 0;
                }
                if (border) {
                    value = 1;
                    if ((x == 0 || x == 23) && (y == 6 || y == 7)) {
                        int n;
                        if (getNeighborRoom(x == 0 ? -1 : 1, 0, n)) {
                            value = 0;
                        }
                    }
                    if ((y == 0 || y == 13) && (x == 11 || x == 12)) {
                        int n;
                        if (getNeighborRoom(0, y == 0 ? -1 : 1, n)) {
                            value = 0;
                        }
                    }
                }
                tiles[y * RoomColumns + x] = value;
            }
        }
        // A roomy clearing around landmarks and enemy spawn points.
        for (auto center :
             std::array<Vec2, 5>{{{480, 140}, {260, 220}, {700, 380}, {480, 280}, {650, 180}}}) {
            for (int y = int(center.y / TileSize) - 1; y <= int(center.y / TileSize) + 1; ++y) {
                for (int x = int(center.x / TileSize) - 1; x <= int(center.x / TileSize) + 1; ++x) {
                    if (x > 0 && x < 23 && y > 0 && y < 13) {
                        tiles[y * RoomColumns + x] = 0;
                    }
                }
            }
        }
        if (roomIndex != RoomId::Village && roomIndex != RoomId::WardenVault) {
            enemies.push_back({{260, 220}, {260, 220}, 0, 3, 0, 0});
            enemies.push_back({{700, 380}, {700, 380}, 1, 2, 1.5, 0});
            if (roomIndex == RoomId::TideglassRuins || roomIndex == RoomId::EmberChapel ||
                roomIndex == RoomId::AshenCauseway) {
                enemies.push_back({{650, 180}, {650, 180}, 2, 4, 2, 0});
            }
        }
        bossActive = roomIndex == RoomId::WardenVault && !won;
        boss = {680, 280};
        bossHealth = 28;
        bossAttackState = BossState::Resting;
        bossAttackCount = 0;
        bossActionTimer = 1.5;
        bossHitFlashTimer = 0;
    }

    // Test every tile touched by a rectangle, not just its center. Out-of-room
    // coordinates stay open so the transition code can handle door crossings.
    bool isBlocked(Rect b) const {
        int x0 = int(std::floor(b.x / TileSize)),
            x1 = int(std::floor((b.x + b.w - .01) / TileSize));
        int y0 = int(std::floor(b.y / TileSize)),
            y1 = int(std::floor((b.y + b.h - .01) / TileSize));
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                if (x < 0 || x >= RoomColumns || y < 0 || y >= RoomRows) {
                    continue;
                }
                if (tiles[y * RoomColumns + x]) {
                    return true;
                }
            }
        }
        return false;
    }

    // Resolve each axis separately: hitting a wall still lets the hero slide along it.
    void movePlayer(Vec2 delta) {
        player.x += delta.x;
        if (isBlocked(playerHitbox())) {
            player.x -= delta.x;
        }
        player.y += delta.y;
        if (isBlocked(playerHitbox())) {
            player.y -= delta.y;
        }
    }

    // Guard progression before changing rooms, then place the hero just inside
    // the opposite entrance. A short cooldown prevents an immediate bounce back.
    bool tryRoomTransition(int dx, int dy) {
        int target;
        if (!getNeighborRoom(dx, dy, target)) {
            return false;
        }
        if (bossActive) {
            showMessage(L"The vault is sealed. Break the Warden's curse!");
            return false;
        }
        if (target == RoomId::WardenVault && sealCount() < 3) {
            showMessage(L"The vault needs THREE SUNSEALS. Search the three corner shrines.");
            return false;
        }
        Vec2 position = player;
        if (dx < 0) {
            position.x = WindowWidth - 38;
        }
        if (dx > 0) {
            position.x = 38;
        }
        if (dy < 0) {
            position.y = RoomRows * TileSize - 38;
        }
        if (dy > 0) {
            position.y = 38;
        }
        enterRoom(target, position);
        return true;
    }

    // The sword reaches in the facing direction, independent of sprite drawing.
    Rect swordHitbox() const {
        if (facing == Direction::Up) {
            return {player.x - 24, player.y - 66, 48, 60};
        }
        if (facing == Direction::Right) {
            return {player.x + 8, player.y - 26, 60, 48};
        }
        if (facing == Direction::Down) {
            return {player.x - 24, player.y + 4, 48, 60};
        }
        return {player.x - 68, player.y - 26, 60, 48};
    }

    // One press starts one swing; hitTargets prevents repeated damage during it.
    void swingSword() {
        if (swordCooldownTimer <= 0 && respawnTimer <= 0) {
            swordSwingTimer = .20;
            swordCooldownTimer = .30;
            hitTargets.clear();
        }
    }
    double random(double a, double b) {
        return a + (b - a) * std::generate_canonical<double, 16>(rng);
    }

    // Cosmetic particles have no collision and never affect gameplay.
    void spawnParticles(Vec2 p, COLORREF color, int n = 14) {
        for (int i = 0; i < n; ++i) {
            sparks.push_back({p, {random(-120, 120), random(-120, 120)}, color, random(.2, .6)});
        }
    }

    // Brief invulnerability prevents contact damage from draining all hearts at once.
    void damagePlayer(Vec2 source, int amount = 1) {
        if (invulnerabilityTimer > 0 || respawnTimer > 0) {
            return;
        }
        health = std::max(0, health - amount);
        invulnerabilityTimer = 1.1;
        double d = std::max(1., distance(player, source));
        knockback = {(player.x - source.x) / d * 220, (player.y - source.y) / d * 220};
        spawnParticles(player, Red, 22);
        if (health == 0) {
            respawnTimer = 1.4;
        }
    }

    // Write a complete temporary file, then replace the save atomically. Never
    // truncate the previous progress file before the replacement is ready.
    bool save() {
        std::error_code error;
        fs::create_directories(saveFile.parent_path(), error);
        fs::path temporary = saveFile;
        temporary += L".tmp";
        std::ofstream out(temporary);
        out << "ZELDOH 1\n" << amber << ' ' << maxHealth << ' ' << won << '\n';
        for (int i = 0; i < 9; ++i) {
            out << visited.count(i) << ' ' << opened.count(i) << '\n';
        }
        out.close();
        if (!out || !MoveFileExW(temporary.c_str(), saveFile.c_str(),
                                 MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            saveError = L"Could not save. Check the saves folder permissions.";
            showMessage(saveError, 6);
            return false;
        }
        saveError.clear();
        return true;
    }

    // Parse and validate into local variables before replacing the active journey.
    // Continue deliberately returns to the village rather than restoring enemy positions.
    bool load() {
        std::ifstream in(saveFile);
        std::string magic;
        int version{}, money{}, hearts{}, victory{};
        if (!(in >> magic >> version >> money >> hearts >> victory) || magic != "ZELDOH" ||
            version != 1 || money < 0 || hearts < 6 || hearts > 8 ||
            (victory != 0 && victory != 1)) {
            return false;
        }
        std::set<int> seen, chest;
        for (int i = 0; i < 9; ++i) {
            int v{}, c{};
            if (!(in >> v >> c) || v < 0 || v > 1 || c < 0 || c > 1) {
                return false;
            }
            if (v) {
                seen.insert(i);
            }
            if (c) {
                chest.insert(i);
            }
        }
        reset();
        amber = money;
        maxHealth = health = hearts;
        won = victory != 0;
        opened = chest;
        visited = seen;
        enterRoom(RoomId::Village, {468, 280});
        title = false;
        showMessage(L"Welcome back. The lantern remembers.");
        return true;
    }

    // E is contextual: restore/save at the lantern, open a guarded chest, or show lore.
    void interact() {
        if (roomIndex == RoomId::Village && distance(player, {480, 170}) < 78) {
            health = maxHealth;
            if (save()) {
                showMessage(L"LANTERN LIT - Hearts restored. Journey saved.");
                spawnParticles({480, 170}, Gold, 30);
            }
            return;
        }
        bool hasChest = roomIndex == RoomId::FernSanctuary || roomIndex == RoomId::TideglassRuins ||
                        roomIndex == RoomId::EmberChapel || roomIndex == RoomId::WillowReach ||
                        roomIndex == RoomId::AshenCauseway;
        if (hasChest && !opened.count(roomIndex) && distance(player, {480, 140}) < 75) {
            if (!enemies.empty()) {
                showMessage(L"The chest is bound by thorns. Defeat this room's enemies first.");
                return;
            }
            opened.insert(roomIndex);
            amber += 10;
            if (roomIndex == RoomId::FernSanctuary || roomIndex == RoomId::TideglassRuins ||
                roomIndex == RoomId::EmberChapel) {
                showMessage(L"SUNSEAL FOUND! " + std::to_wstring(sealCount()) +
                                L" / 3 - The Warden's vault awaits.",
                            6);
            } else {
                ++maxHealth;
                showMessage(L"HEART VESSEL! Maximum health increased.", 5);
            }
            health = maxHealth;
            spawnParticles({480, 140}, Gold, 42);
            save();
            return;
        }
        showMessage(WorldRooms[roomIndex].hint);
    }

    // Simulation order: timers/particles, respawn, movement/doors, enemies, boss,
    // then projectiles. The outer loop supplies small substeps for solid collisions.
    void update(double deltaTime, Vec2 input) {
        elapsedTime += deltaTime;
        for (double* actionTimer :
             {&invulnerabilityTimer, &swordSwingTimer, &swordCooldownTimer, &roomBannerTimer,
              &toastTimer, &transitionTimer, &bossHitFlashTimer}) {
            *actionTimer = std::max(0., *actionTimer - deltaTime);
        }
        for (auto& s : sparks) {
            s.life -= deltaTime;
            s.position.x += s.velocity.x * deltaTime;
            s.position.y += s.velocity.y * deltaTime;
        }
        sparks.erase(std::remove_if(sparks.begin(), sparks.end(),
                                    [](const Spark& s) { return s.life <= 0; }),
                     sparks.end());
        if (respawnTimer > 0) {
            respawnTimer -= deltaTime;
            if (respawnTimer <= 0) {
                health = maxHealth;
                enterRoom(RoomId::Village, {468, 280});
                invulnerabilityTimer = 1;
                showMessage(L"The lantern guides you home. Your discoveries are safe.");
            }
            return;
        }
        double length = std::hypot(input.x, input.y);
        if (length > 0) {
            if (std::abs(input.x) > std::abs(input.y)) {
                facing = input.x > 0 ? Direction::Right : Direction::Left;
            } else {
                facing = input.y > 0 ? Direction::Down : Direction::Up;
            }
            // Normalize diagonals so pressing two directions does not increase speed.
            input.x /= length;
            input.y /= length;
        }
        double speed = swordSwingTimer > 0 ? 105 : 185;
        movePlayer({(input.x * speed + knockback.x) * deltaTime,
                    (input.y * speed + knockback.y) * deltaTime});
        knockback.x *= std::max(0., 1 - deltaTime * 9);
        knockback.y *= std::max(0., 1 - deltaTime * 9);
        if (transitionTimer <= 0) {
            if (player.x < 4 && !tryRoomTransition(-1, 0)) {
                player.x = 14;
            } else if (player.x > WindowWidth - 4 && !tryRoomTransition(1, 0)) {
                player.x = WindowWidth - 14;
            } else if (player.y < 4 && !tryRoomTransition(0, -1)) {
                player.y = 14;
            } else if (player.y > RoomRows * TileSize - 4 && !tryRoomTransition(0, 1)) {
                player.y = RoomRows * TileSize - 14;
            }
        }
        // The entry cooldown should never let the hero walk out of the playfield.
        player.x = bounded(player.x, 1, WindowWidth - 1);
        player.y = bounded(player.y, 1, RoomRows * TileSize - 1);
        // Delay removing defeated enemies until the swing ends, preserving hit IDs.
        for (int i = int(enemies.size()) - 1; i >= 0; --i) {
            auto& enemy = enemies[size_t(i)];
            if (enemy.health <= 0) {
                continue;
            }
            enemy.hitFlashTimer = std::max(0., enemy.hitFlashTimer - deltaTime);
            enemy.actionTimer -= deltaTime;
            Vec2 delta{player.x - enemy.position.x, player.y - enemy.position.y};
            double distanceToPlayer = std::max(1., std::hypot(delta.x, delta.y));
            double enemySpeed = enemy.type == 0 ? 58 : (enemy.type == 1 ? 83 : 36);
            if (distanceToPlayer < 350 && distanceToPlayer > 25) {
                Vec2 next{enemy.position.x + delta.x / distanceToPlayer * enemySpeed * deltaTime,
                          enemy.position.y + delta.y / distanceToPlayer * enemySpeed * deltaTime};
                if (enemy.type == 1 || !isBlocked({next.x - 13, next.y - 13, 26, 26})) {
                    enemy.position = next;
                }
            }
            if (enemy.type == 2 && enemy.actionTimer <= 0) {
                enemy.actionTimer = 2.2;
                shots.push_back(
                    {enemy.position,
                     {delta.x / distanceToPlayer * 160, delta.y / distanceToPlayer * 160},
                     4});
            }
            Rect box{enemy.position.x - 18, enemy.position.y - 18, 36, 36};
            if (swordSwingTimer > 0 && !hitTargets.count(i) && overlaps(swordHitbox(), box)) {
                hitTargets.insert(i);
                --enemy.health;
                enemy.hitFlashTimer = .15;
                spawnParticles(enemy.position, Gold, 9);
                if (enemy.health <= 0) {
                    spawnParticles(enemy.position, Mint, 22);
                    amber += 3;
                    if (amber % 9 == 0) {
                        health = std::min(maxHealth, health + 1);
                    }
                    // Keep indices stable during a single attack to avoid double hits.
                    enemy.health = 0;
                    enemy.position = {-9000, -9000};
                }
            }
            if (enemy.health > 0 && overlaps(playerHitbox(), box)) {
                damagePlayer(enemy.position);
            }
        }
        if (swordSwingTimer <= 0) {
            enemies.erase(std::remove_if(enemies.begin(), enemies.end(),
                                         [](const Enemy& enemy) { return enemy.health <= 0; }),
                          enemies.end());
        }
        updateBoss(deltaTime);
        for (auto& s : shots) {
            s.life -= deltaTime;
            s.position.x += s.velocity.x * deltaTime;
            s.position.y += s.velocity.y * deltaTime;
            Rect box{s.position.x - 7, s.position.y - 7, 14, 14};
            // A well-timed sword swing can cancel an incoming projectile.
            if (swordSwingTimer > 0 && overlaps(swordHitbox(), box)) {
                s.life = 0;
                spawnParticles(s.position, Mint, 7);
            } else if (overlaps(playerHitbox(), box)) {
                damagePlayer(s.position);
                s.life = 0;
            }
            if (s.position.x < 0 || s.position.x > WindowWidth || s.position.y < 0 ||
                s.position.y > RoomRows * TileSize) {
                s.life = 0;
            }
        }
        shots.erase(
            std::remove_if(shots.begin(), shots.end(), [](const Shot& s) { return s.life <= 0; }),
            shots.end());
    }

    // Boss state machine: Resting -> Warning -> Charging or a projectile ring.
    // Alternate attacks, with shorter warnings and more shots below half health.
    void updateBoss(double deltaTime) {
        if (!bossActive) {
            return;
        }
        bossActionTimer -= deltaTime;
        Rect box{boss.x - 45, boss.y - 40, 90, 80};
        if (swordSwingTimer > 0 && !hitTargets.count(99) && overlaps(swordHitbox(), box)) {
            hitTargets.insert(99);
            --bossHealth;
            bossHitFlashTimer = .15;
            spawnParticles(boss, Gold, 12);
            if (bossHealth <= 0) {
                bossActive = false;
                won = true;
                shots.clear();
                health = maxHealth;
                amber += 50;
                spawnParticles(boss, Gold, 100);
                showMessage(
                    L"THE DAWN RETURNS! The Warden is free. Visit the lantern, or keep exploring.",
                    12);
                save();
                return;
            }
        }
        if (bossAttackState == BossState::Resting && bossActionTimer <= 0) {
            bossAttackState = BossState::Warning;
            bossActionTimer = bossHealth > 14 ? .9 : .6;
            ++bossAttackCount;
        } else if (bossAttackState == BossState::Warning && bossActionTimer <= 0) {
            if (bossAttackCount % 2 == 0) {
                int n = bossHealth > 14 ? 8 : 12;
                for (int i = 0; i < n; ++i) {
                    double angle = i * Pi * 2 / n + elapsedTime * .1;
                    shots.push_back({boss, {std::cos(angle) * 155, std::sin(angle) * 155}, 5});
                }
                bossAttackState = BossState::Resting;
                bossActionTimer = 1.6;
            } else {
                double d = std::max(1., distance(player, boss));
                bossChargeVelocity = {(player.x - boss.x) / d * 340, (player.y - boss.y) / d * 340};
                bossAttackState = BossState::Charging;
                bossActionTimer = .85;
            }
        } else if (bossAttackState == BossState::Charging) {
            boss.x = bounded(boss.x + bossChargeVelocity.x * deltaTime, 95, WindowWidth - 95);
            boss.y =
                bounded(boss.y + bossChargeVelocity.y * deltaTime, 95, RoomRows * TileSize - 95);
            if (bossActionTimer <= 0 || boss.x <= 95 || boss.x >= WindowWidth - 95 ||
                boss.y <= 95 || boss.y >= RoomRows * TileSize - 95) {
                bossAttackState = BossState::Resting;
                bossActionTimer = 1.5;
            }
        }
        if (overlaps(playerHitbox(), box)) {
            damagePlayer(boss);
        }
    }
};

// DRAWING: owns cached GDI brushes/fonts. Each temporary selected object is
// restored before deletion, as required by Windows graphics resource ownership.
struct Painter {
    HDC dc;
    std::map<COLORREF, HBRUSH> brushes;
    std::map<int, HFONT> fonts;
    explicit Painter(HDC target) : dc(target) {
        SetBkMode(dc, TRANSPARENT);
    }
    ~Painter() {
        for (auto [_, b] : brushes) {
            DeleteObject(b);
        }
        for (auto [_, f] : fonts) {
            DeleteObject(f);
        }
    }
    HBRUSH brush(COLORREF c) {
        auto& b = brushes[c];
        if (!b) {
            b = CreateSolidBrush(c);
        }
        return b;
    }
    void rect(int x, int y, int w, int h, COLORREF c) {
        RECT r{x, y, x + w, y + h};
        FillRect(dc, &r, brush(c));
    }
    void line(int x, int y, int x2, int y2, COLORREF c, int width = 1) {
        HPEN pen = CreatePen(PS_SOLID, width, c);
        auto old = SelectObject(dc, pen);
        MoveToEx(dc, x, y, nullptr);
        LineTo(dc, x2, y2);
        SelectObject(dc, old);
        DeleteObject(pen);
    }
    void ellipse(int x, int y, int w, int h, COLORREF c) {
        auto oldB = SelectObject(dc, brush(c));
        auto oldP = SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, x, y, x + w, y + h);
        SelectObject(dc, oldB);
        SelectObject(dc, oldP);
    }
    void text(const std::wstring& s, int x, int y, int size = 18, COLORREF c = Cream,
              bool center = false) {
        auto& font = fonts[size];
        if (!font) {
            font = CreateFontW(-size, 0, 0, 0, size >= 30 ? FW_BOLD : FW_NORMAL, FALSE, FALSE,
                               FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Consolas");
        }
        auto old = SelectObject(dc, font);
        SetTextColor(dc, c);
        SIZE extent{};
        GetTextExtentPoint32W(dc, s.c_str(), int(s.size()), &extent);
        TextOutW(dc, center ? x - extent.cx / 2 : x, y, s.c_str(), int(s.size()));
        SelectObject(dc, old);
    }
    void frame(int x, int y, int w, int h, COLORREF border = RGB(76, 109, 110)) {
        rect(x, y, w, h, border);
        rect(x + 2, y + 2, w - 4, h - 4, Ink);
    }
};

// Draw the original hero with tiny pixel rectangles; the title uses a 4x scale.
void drawHero(Painter& painter, int x, int y, int facing, double t, bool large = false) {
    int scale = large ? 4 : 1;
    auto pixelRect = [&](int dx, int dy, int w, int h, COLORREF c) {
        painter.rect(x + dx * scale, y + dy * scale, w * scale, h * scale, c);
    };
    int step = int(std::sin(t * 10) * 2);
    pixelRect(-12, 9, 25, 8, RGB(27, 42, 42));
    pixelRect(-10, -7, 20, 21, RGB(93, 87, 155));
    pixelRect(-12, -11, 24, 11, RGB(122, 113, 192));
    pixelRect(-8, -5, 16, 12, RGB(238, 191, 139));
    pixelRect(-10, -13, 20, 8, RGB(113, 103, 184));
    pixelRect(-4, -17, 17, 5, RGB(147, 131, 211));
    pixelRect(7, -15, 9, 5, RGB(147, 131, 211));
    pixelRect(-7, 7, 14, 4, Gold);
    pixelRect(-10, 13 + step, 8, 5, RGB(78, 60, 64));
    pixelRect(3, 13 - step, 8, 5, RGB(78, 60, 64));
    if (facing != Direction::Up) {
        pixelRect(-4, -2, 3, 3, Ink);
        pixelRect(5, -2, 3, 3, Ink);
    } else {
        pixelRect(-7, -6, 14, 10, RGB(113, 103, 184));
    }
    pixelRect(-16, 2, 6, 11, RGB(124, 197, 179));
    pixelRect(-15, 3, 4, 7, RGB(50, 96, 103));
}

// Draw back-to-front: terrain, landmarks, enemies, effects, hero, then HUD.
// All gameplay coordinates add HudHeight when converted to screen coordinates.
void drawWorld(Painter& painter, Game& game, bool hud = true) {
    COLORREF ground = WorldRooms[game.roomIndex].biome == 0   ? RGB(45, 80, 66)
                      : WorldRooms[game.roomIndex].biome == 1 ? RGB(53, 83, 91)
                                                              : RGB(75, 58, 65);
    COLORREF fleck = WorldRooms[game.roomIndex].biome == 0   ? RGB(55, 94, 72)
                     : WorldRooms[game.roomIndex].biome == 1 ? RGB(66, 98, 104)
                                                             : RGB(90, 69, 72);
    painter.rect(0, 0, WindowWidth, WindowHeight, Ink);
    for (int y = 0; y < RoomRows; ++y) {
        for (int x = 0; x < RoomColumns; ++x) {
            int a = x * TileSize, b = HudHeight + y * TileSize,
                tile = game.tiles[y * RoomColumns + x];
            painter.rect(a, b, TileSize, TileSize, ground);
            if (tile == 0) {
                if (x == 11 || x == 12 || y == 6 || y == 7) {
                    painter.rect(a, b, TileSize, TileSize,
                                 WorldRooms[game.roomIndex].biome == 2 ? RGB(108, 86, 76)
                                                                       : RGB(119, 119, 83));
                }
                painter.rect(a + (x * 7 + y * 11) % 28 + 4, b + 10, 3, 3, fleck);
                painter.rect(a + 14, b + (y * 17 + x * 5) % 28 + 5, 5, 2, fleck);
            } else if (tile == 2) {
                painter.rect(a, b, TileSize, TileSize, RGB(32, 70, 91));
                int offset = int(game.elapsedTime * 8 + x * 3) % 16;
                painter.rect(a + offset, b + 13, 15, 2, RGB(73, 131, 148));
                painter.rect(a + 5, b + 29, 18, 2, RGB(59, 110, 134));
            } else if (WorldRooms[game.roomIndex].biome == 0) {
                painter.rect(a + 15, b + 20, 12, 19, RGB(80, 62, 51));
                painter.rect(a + 3, b + 9, 34, 22, RGB(27, 57, 53));
                painter.rect(a + 7, b + 4, 27, 23, RGB(43, 105, 73));
                painter.rect(a + 12, b + 3, 15, 7, RGB(75, 138, 91));
            } else {
                painter.rect(a + 2, b + 4, 36, 34, RGB(37, 43, 57));
                painter.rect(a + 3, b + 3, 32, 29, RGB(92, 94, 104));
                painter.rect(a + 6, b + 6, 24, 5, RGB(136, 137, 139));
                painter.line(a + 20, b + 14, a + 26, b + 24, RGB(58, 64, 77), 2);
            }
        }
    }

    // Room landmarks, all in original pixel shapes.
    if (game.roomIndex == RoomId::Village) {
        painter.rect(391, HudHeight + 107, 178, 117, RGB(92, 108, 88));
        for (int x = 398; x < 560; x += 30) {
            for (int y = 114; y < 215; y += 27) {
                painter.rect(x, HudHeight + y, 24, 20, RGB(128, 139, 112));
            }
        }
        painter.rect(473, HudHeight + 142, 14, 51, RGB(105, 81, 56));
        painter.rect(464, HudHeight + 128, 32, 34, Gold);
        painter.rect(469, HudHeight + 133, 22, 24, RGB(255, 231, 155));
        painter.rect(459, HudHeight + 123, 42, 6, RGB(75, 67, 60));
        painter.text(L"E - REST & SAVE", 480, HudHeight + 213, 14, Gold, true);
    }
    bool chest = game.roomIndex == RoomId::FernSanctuary ||
                 game.roomIndex == RoomId::TideglassRuins ||
                 game.roomIndex == RoomId::EmberChapel || game.roomIndex == RoomId::WillowReach ||
                 game.roomIndex == RoomId::AshenCauseway;
    if (chest) {
        int y = HudHeight + 140;
        painter.ellipse(454, y + 9, 54, 13, RGB(30, 43, 46));
        painter.rect(457, y - 17, 46, 34, RGB(121, 76, 58));
        painter.rect(459, y - 17, 42, 12,
                     game.opened.count(game.roomIndex) ? RGB(48, 39, 43) : RGB(189, 133, 75));
        painter.rect(458, y - 4, 44, 3, Gold);
        painter.rect(465, y - 16, 4, 31, Gold);
        painter.rect(491, y - 16, 4, 31, Gold);
        painter.rect(477, y - 5, 7, 10, Gold);
        if (!game.opened.count(game.roomIndex)) {
            painter.text(game.enemies.empty() ? L"E - OPEN" : L"DEFEAT THE GUARDS", 480, y - 44, 14,
                         Gold, true);
        }
    }
    if ((game.roomIndex == RoomId::SaffronMarsh || game.roomIndex == RoomId::AshenCauseway) &&
        game.sealCount() < 3) {
        int x = game.roomIndex == RoomId::AshenCauseway ? WindowWidth - 14 : 480,
            y = game.roomIndex == RoomId::AshenCauseway ? HudHeight + 280 : WindowHeight - 14;
        for (int i = -1; i <= 1; ++i) {
            painter.rect(x + (game.roomIndex == RoomId::AshenCauseway ? 0 : i * 18),
                         y + (game.roomIndex == RoomId::AshenCauseway ? i * 18 : 0), 9, 9, Gold);
        }
    }
    for (const auto& enemy : game.enemies) {
        if (enemy.health <= 0) {
            continue;
        }
        int x = int(enemy.position.x), y = int(enemy.position.y) + HudHeight;
        painter.ellipse(x - 19, y + 10, 38, 11, RGB(24, 37, 41));
        COLORREF c = enemy.hitFlashTimer > 0 ? Cream
                     : enemy.type == 0       ? RGB(135, 183, 112)
                     : enemy.type == 1       ? RGB(187, 132, 165)
                                             : RGB(171, 133, 102);
        if (enemy.type == 0) {
            painter.rect(x - 16, y - 8, 32, 21, c);
            painter.rect(x - 11, y - 15, 22, 12, c);
            painter.rect(x - 12, y + 12, 9, 4, c);
            painter.rect(x + 5, y + 12, 9, 4, c);
        } else if (enemy.type == 1) {
            int flap = int(std::sin(game.elapsedTime * 15) * 8);
            painter.rect(x - 30, y - 12 + flap, 25, 12, c);
            painter.rect(x + 5, y - 12 + flap, 25, 12, c);
            painter.rect(x - 9, y - 14, 18, 29, c);
        } else {
            painter.rect(x - 17, y - 18, 34, 34, c);
            painter.rect(x - 21, y + 11, 42, 9, RGB(88, 85, 88));
            painter.rect(x - 11, y - 10, 22, 8, RGB(76, 64, 78));
        }
        painter.rect(x - 8, y - 4, 4, 5, Ink);
        painter.rect(x + 5, y - 4, 4, 5, Ink);
    }
    if (game.bossActive) {
        int x = int(game.boss.x), y = int(game.boss.y) + HudHeight;
        COLORREF c = game.bossHitFlashTimer > 0 ? Cream
                     : game.bossHealth > 14     ? RGB(150, 147, 140)
                                                : RGB(175, 119, 123);
        painter.ellipse(x - 59, y + 35, 118, 20, RGB(35, 34, 44));
        painter.rect(x - 41, y - 36, 82, 80, c);
        painter.rect(x - 51, y - 26, 20, 62, RGB(97, 98, 106));
        painter.rect(x + 31, y - 26, 20, 62, RGB(97, 98, 106));
        painter.rect(x - 40, y - 46, 18, 24, c);
        painter.rect(x + 22, y - 46, 18, 24, c);
        painter.rect(x - 29, y - 23, 22, 18, Ink);
        painter.rect(x + 7, y - 23, 22, 18, Ink);
        painter.rect(x - 24, y - 17, 12, 8, Gold);
        painter.rect(x + 12, y - 17, 12, 8, Gold);
        painter.rect(x - 6, y - 2, 12, 17, Gold);
        painter.rect(x - 22, y + 22, 44, 5, RGB(91, 89, 93));
        if (game.bossAttackState == BossState::Warning) {
            painter.text(game.bossAttackCount % 2 ? L"CHARGE!" : L"KEEP MOVING!", x, y - 77, 18,
                         Gold, true);
        }
    }
    if (game.won && game.roomIndex == RoomId::WardenVault) {
        painter.ellipse(450, HudHeight + 260, 60, 30, RGB(118, 93, 70));
        painter.rect(475, HudHeight + 235, 10, 36, Gold);
        painter.rect(467, HudHeight + 244, 26, 18, Gold);
        painter.text(L"THE LANTERN OF DAWN", 480, HudHeight + 203, 18, Gold, true);
    }
    for (const auto& s : game.shots) {
        painter.ellipse(int(s.position.x) - 9, int(s.position.y) + HudHeight - 9, 18, 18,
                        RGB(110, 59, 90));
        painter.rect(int(s.position.x) - 4, int(s.position.y) + HudHeight - 4, 8, 8, Gold);
    }
    if (game.invulnerabilityTimer <= 0 || int(game.elapsedTime * 14) % 2 == 0) {
        drawHero(painter, int(game.player.x), HudHeight + int(game.player.y), game.facing,
                 game.elapsedTime);
    }
    if (game.swordSwingTimer > 0) {
        Rect b = game.swordHitbox();
        int x = int(b.x), y = int(b.y) + HudHeight;
        if (game.facing == Direction::Right || game.facing == Direction::Left) {
            painter.rect(x + 4, y + 20, int(b.w) - 8, 8, Cream);
            painter.rect(x + 9, y + 18, int(b.w) - 18, 3, Mint);
        } else {
            painter.rect(x + 20, y + 4, 8, int(b.h) - 8, Cream);
            painter.rect(x + 18, y + 9, 3, int(b.h) - 18, Mint);
        }
    }
    for (const auto& s : game.sparks) {
        painter.rect(int(s.position.x), HudHeight + int(s.position.y), 4, 4, s.color);
    }
    if (!hud) {
        return;
    }
    painter.rect(0, 0, WindowWidth, HudHeight, Ink);
    painter.rect(0, HudHeight - 2, WindowWidth, 2, RGB(69, 99, 100));
    painter.text(L"LANTERN OF THE VALE", 22, 12, 18, Gold);
    for (int i = 0; i < game.maxHealth; ++i) {
        int x = 24 + i * 24;
        COLORREF c = i < game.health ? Red : RGB(65, 65, 79);
        painter.rect(x, 44, 7, 7, c);
        painter.rect(x + 10, 44, 7, 7, c);
        painter.rect(x + 2, 48, 13, 9, c);
        painter.rect(x + 5, 57, 7, 4, c);
    }
    painter.text(L"AMBER " + std::to_wstring(game.amber), 282, 15, 16, Mint);
    painter.text(L"SUNSEALS " + std::to_wstring(game.sealCount()) + L" / 3", 282, 43, 16, Gold);
    painter.text(WorldRooms[game.roomIndex].name, 590, 17, 18, Cream, true);
    painter.text(L"M MAP   SPACE SWORD   E USE", 590, 46, 14, Muted, true);
    for (int i = 0; i < 9; ++i) {
        painter.rect(822 + i % 3 * 32, 12 + i / 3 * 19, 27, 14,
                     game.visited.count(i) ? RGB(72, 117, 105) : RGB(36, 48, 62));
        if (i == game.roomIndex) {
            painter.rect(831 + i % 3 * 32, 16 + i / 3 * 19, 7, 6, Gold);
        }
    }
    if (game.roomBannerTimer > 0 && !game.bossActive) {
        painter.frame(235, 95, 490, 58);
        painter.text(WorldRooms[game.roomIndex].name, 480, 105, 24, Cream, true);
        painter.text(L"- THE VALE -", 480, 134, 12, Mint, true);
    }
    if (game.bossActive) {
        painter.frame(282, 94, 396, 44);
        painter.text(L"THE STONEWING WARDEN", 480, 99, 14, Gold, true);
        painter.rect(294, 122, 372, 6, RGB(61, 47, 62));
        painter.rect(294, 122, 372 * game.bossHealth / 28, 6, Red);
    }
    if (game.toastTimer > 0) {
        painter.frame(20, WindowHeight - 43, WindowWidth - 40, 32);
        painter.text(game.toast, 480, WindowHeight - 36, game.toast.size() > 90 ? 12 : 14, Gold,
                     true);
    }
    if (game.respawnTimer > 0) {
        painter.frame(220, 250, 520, 100);
        painter.text(L"The lantern calls you home...", 480, 281, 24, Cream, true);
    }
}

// Screen selection is kept separate from simulation: title/map/pause overlays
// draw the same world underneath without advancing enemy or player movement.
void render(Painter& painter, Game& game) {
    drawWorld(painter, game);
    if (game.title) {
        painter.frame(90, 130, 780, 386);
        painter.text(L"A SMALL, ORIGINAL TILE ADVENTURE", 480, 159, 16, Mint, true);
        painter.text(L"ZELDOH", 375, 197, 64, Gold, true);
        painter.text(L"LANTERN OF THE VALE", 375, 272, 24, Cream, true);
        painter.text(L"Three lost seals. One sleeping guardian.", 375, 316, 16, Muted, true);
        drawHero(painter, 737, 276, 2, game.elapsedTime, true);
        bool exists = fs::exists(game.saveFile);
        painter.text(exists
                         ? (game.menu % 2 == 0 ? L"> CONTINUE JOURNEY <" : L"  CONTINUE JOURNEY  ")
                         : L"> BEGIN JOURNEY <",
                     480, 374, 22, game.menu % 2 == 0 ? Mint : Cream, true);
        if (exists) {
            painter.text(game.menu % 2 ? L"> NEW JOURNEY <" : L"  NEW JOURNEY  ", 480, 412, 22,
                         game.menu % 2 ? Mint : Cream, true);
        }
        painter.text(L"ARROWS SELECT  /  ENTER BEGIN  /  ESC QUIT", 480, 470, 14, Muted, true);
    } else if (game.mapOpen) {
        painter.frame(75, 103, 810, 477);
        painter.text(L"ATLAS OF THE VALE", 480, 121, 30, Gold, true);
        painter.text(L"Find the three corner Sunseals. Open the southeast vault.", 480, 165, 16,
                     Muted, true);
        for (int i = 0; i < 9; ++i) {
            int x = 108 + i % 3 * 252, y = 205 + i / 3 * 98;
            painter.frame(x, y, 238, 84, i == game.roomIndex ? Gold : RGB(66, 106, 105));
            painter.text(game.visited.count(i) ? WorldRooms[i].name : L"Uncharted", x + 119, y + 12,
                         16, Cream, true);
            if (game.visited.count(i)) {
                std::wstring label =
                    i == 4 ? L"LANTERN / SAVE"
                    : (i == 0 || i == 2 || i == 6)
                        ? (game.opened.count(i) ? L"SUNSEAL FOUND" : L"SUNSEAL CHEST")
                    : i == 8 ? (game.won ? L"DAWN RESTORED" : L"BOSS / THREE SEALS")
                             : L"PATH / ENEMIES";
                painter.text(label, x + 119, y + 38, 14, Mint, true);
                if (i == game.roomIndex) {
                    painter.text(L"YOU ARE HERE", x + 119, y + 60, 12, Gold, true);
                }
            }
        }
        painter.text(L"M / TAB / ESC RETURN     |     " + std::to_wstring(game.visited.size()) +
                         L" / 9 ROOMS EXPLORED",
                     480, 534, 14, Muted, true);
    } else if (game.paused) {
        painter.frame(185, 158, 590, 330);
        painter.text(L"A MOMENT BY THE LANTERN", 480, 185, 28, Gold, true);
        painter.text(L"WASD / ARROWS       MOVE", 480, 244, 18, Cream, true);
        painter.text(L"SPACE / J / Z      SWORD", 480, 277, 18, Cream, true);
        painter.text(L"E              INTERACT", 480, 310, 18, Cream, true);
        painter.text(L"M / TAB             MAP", 480, 343, 18, Cream, true);
        painter.text(L"ESC RESUME   R RETURN TO VILLAGE   Q SAVE & QUIT", 480, 433, 14, Mint, true);
    }
}

// Deterministic integration checks exercise real game methods using an isolated
// progress file. These checks never require keyboard input or a visible window.
bool runGameplayTests(Game& game) {
    std::ofstream log(game.root / L"self-test-results.txt");
    auto check = [&](bool ok, const char* name) {
        log << (ok ? "PASS " : "FAIL ") << name << '\n';
        log.flush();
        if (!ok) {
            throw std::runtime_error(name);
        }
    };
    try {
        check(game.roomIndex == RoomId::Village && game.visited.count(4), "new journey");
        Vec2 start = game.player;
        game.update(.1, {1, 0});
        check(game.player.x > start.x, "movement");
        game.player = {54, 70};
        game.movePlayer({-20, 0});
        check(game.player.x == 54, "wall collision");
        game.enterRoom(5, {480, 530});
        check(!game.tryRoomTransition(0, 1), "three-seal vault lock");
        for (int roomIndex : {0, 2, 6}) {
            game.enterRoom(roomIndex, {480, 180});
            game.interact();
            check(!game.opened.count(roomIndex), "guarded chest stays locked");
            game.enemies.clear();
            game.interact();
            check(game.opened.count(roomIndex) != 0, "sunseal pickup");
        }
        check(game.sealCount() == 3, "three seals collected");
        game.enterRoom(5, {480, 530});
        check(game.tryRoomTransition(0, 1) && game.roomIndex == RoomId::WardenVault,
              "vault unlocked");
        check(!game.tryRoomTransition(-1, 0), "boss arena sealed");
        game.bossActionTimer = 0;
        game.updateBoss(.01);
        check(game.bossAttackState == BossState::Warning, "boss warning");
        game.bossActionTimer = 0;
        game.updateBoss(.01);
        check(game.bossAttackState == BossState::Charging, "boss charge");
        game.bossAttackCount = 1;
        game.bossAttackState = BossState::Resting;
        game.bossActionTimer = 0;
        game.updateBoss(.01);
        game.bossActionTimer = 0;
        game.updateBoss(.01);
        check(game.shots.size() == 8, "boss projectile ring");
        game.player = {game.boss.x - 64, game.boss.y};
        game.facing = Direction::Right;
        game.bossHealth = 1;
        game.swingSword();
        game.updateBoss(.01);
        check(game.won && !game.bossActive, "boss victory");
        game.enterRoom(1, {480, 180});
        game.enemies.clear();
        game.interact();
        check(game.maxHealth == 7, "heart vessel");
        game.enterRoom(3, {230, 220});
        game.facing = Direction::Right;
        for (int i = 0; i < 3; ++i) {
            game.swordCooldownTimer = 0;
            game.swingSword();
            game.update(.001, {});
        }
        game.swordSwingTimer = 0;
        game.update(.001, {});
        check(game.enemies.size() == 1, "sword defeats enemy");
        game.invulnerabilityTimer = 0;
        int oldHp = game.health;
        game.damagePlayer({game.player.x + 30, game.player.y});
        game.damagePlayer({game.player.x + 30, game.player.y});
        check(game.health == oldHp - 1, "damage invulnerability");
        game.invulnerabilityTimer = 0;
        game.damagePlayer({0, 0}, 20);
        game.update(1.5, {});
        check(game.roomIndex == RoomId::Village && game.health == game.maxHealth,
              "death returns to lantern");
        check(game.save(), "atomic save");
        Game restored(game.root);
        restored.saveFile = game.saveFile;
        check(restored.load() && restored.won && restored.sealCount() == 3 &&
                  restored.maxHealth == 7,
              "save reload");
        for (int roomIndex = 0; roomIndex < 9; ++roomIndex) {
            game.enterRoom(roomIndex, {480, 280});
            check(!game.isBlocked(game.playerHitbox()), "room clearing");
            std::set<int> reached;
            std::vector<int> queue{7 * RoomColumns + 12};
            reached.insert(queue[0]);
            for (size_t j = 0; j < queue.size(); ++j) {
                int x = queue[j] % RoomColumns, y = queue[j] / RoomColumns;
                for (Vec2 d : {Vec2{-1, 0}, Vec2{1, 0}, Vec2{0, -1}, Vec2{0, 1}}) {
                    int nx = x + int(d.x), ny = y + int(d.y), next = ny * RoomColumns + nx;
                    if (nx >= 0 && nx < RoomColumns && ny >= 0 && ny < RoomRows &&
                        !game.tiles[next] && reached.insert(next).second) {
                        queue.push_back(next);
                    }
                }
            }
            bool accessible = reached.count(3 * RoomColumns + 12) != 0;
            for (const auto& e : game.enemies) {
                accessible =
                    accessible && reached.count(int(e.position.y / TileSize) * RoomColumns +
                                                int(e.position.x / TileSize));
            }
            check(accessible, "chest and enemy spawn routes reachable");
        }
        log << "All gameplay checks passed.\n";
        return true;
    } catch (const std::exception& e) {
        log << "FAILED: " << e.what() << '\n';
        return false;
    }
}

// Compose the entire client area (including letterboxing) off-screen. The
// window receives only one finished BitBlt, never a clear followed by a draw.
struct Presenter {
    HDC scene{}, buffer{};
    HBITMAP bitmap{};
    HGDIOBJ original{};
    int width{}, height{};
    explicit Presenter(HDC source) : scene(source), buffer(CreateCompatibleDC(source)) {
    }
    Presenter(const Presenter&) = delete;
    Presenter& operator=(const Presenter&) = delete;
    ~Presenter() {
        if (bitmap) {
            SelectObject(buffer, original);
            DeleteObject(bitmap);
        }
        if (buffer) {
            DeleteDC(buffer);
        }
    }
    bool paint(HDC target, int w, int h) {
        if (w <= 0 || h <= 0) {
            return true; // Minimized windows have no surface.
        }
        if (!buffer) {
            return false;
        }
        if (w != width || h != height) {
            HBITMAP replacement = CreateCompatibleBitmap(target, w, h);
            if (!replacement) {
                return false;
            }
            HGDIOBJ previous = SelectObject(buffer, replacement);
            if (!bitmap) {
                original = previous;
            } else {
                DeleteObject(bitmap);
            }
            bitmap = replacement;
            width = w;
            height = h;
        }
        RECT client{0, 0, w, h};
        HBRUSH background = CreateSolidBrush(Ink);
        FillRect(buffer, &client, background);
        DeleteObject(background);
        double scale = std::min(double(w) / WindowWidth, double(h) / WindowHeight);
        int drawW = std::max(1, int(WindowWidth * scale)),
            drawH = std::max(1, int(WindowHeight * scale));
        SetStretchBltMode(buffer, COLORONCOLOR);
        if (!StretchBlt(buffer, (w - drawW) / 2, (h - drawH) / 2, drawW, drawH, scene, 0, 0,
                        WindowWidth, WindowHeight, SRCCOPY)) {
            return false;
        }
        return BitBlt(target, 0, 0, w, h, buffer, 0, 0, SRCCOPY) != FALSE;
    }
};

// Paint into a test bitmap to verify resizing, letterbox coverage and stable
// GDI resource use. A bright sentinel detects pixels left over from old frames.
bool testPresentation(HDC reference, const fs::path& root) {
    HDC source = CreateCompatibleDC(reference), target = CreateCompatibleDC(reference);
    HBITMAP sourceBitmap = CreateCompatibleBitmap(reference, WindowWidth, WindowHeight);
    HBITMAP targetBitmap = CreateCompatibleBitmap(reference, 1280, 960);
    auto oldSource = SelectObject(source, sourceBitmap),
         oldTarget = SelectObject(target, targetBitmap);
    const COLORREF sceneColor = RGB(81, 144, 123);
    bool passed = source && target && sourceBitmap && targetBitmap;
    {
        Painter p(source);
        p.rect(0, 0, WindowWidth, WindowHeight, sceneColor);
        Presenter output(source);
        passed = passed && output.paint(target, WindowWidth, WindowHeight);
        DWORD baseline = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        const std::array<Vec2, 4> sizes{{{960, 640}, {1280, 640}, {960, 960}, {480, 320}}};
        for (int iteration = 0; iteration < 100 && passed; ++iteration) {
            Vec2 size = sizes[size_t(iteration) % sizes.size()];
            int w = int(size.x), h = int(size.y);
            // A contrasting destination must be completely replaced by one
            // composited frame, including any letterbox bars after resizing.
            RECT client{0, 0, w, h};
            HBRUSH sentinel = CreateSolidBrush(RGB(255, 0, 255));
            FillRect(target, &client, sentinel);
            DeleteObject(sentinel);
            passed = output.paint(target, w, h);
            passed = passed && GetPixel(target, w / 2, h / 2) == sceneColor;
            bool letterboxed = w * WindowHeight != h * WindowWidth;
            passed = passed && GetPixel(target, 0, 0) == (letterboxed ? Ink : sceneColor);
            passed = passed && GetPixel(target, w - 1, h - 1) == (letterboxed ? Ink : sceneColor);
        }
        passed = passed && output.paint(target, 0, 0);
        passed = passed && GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= baseline + 1;
    }
    SelectObject(source, oldSource);
    SelectObject(target, oldTarget);
    DeleteObject(sourceBitmap);
    DeleteObject(targetBitmap);
    DeleteDC(source);
    DeleteDC(target);
    std::ofstream log(root / L"self-test-results.txt", std::ios::app);
    log << (passed ? "PASS" : "FAIL")
        << " frame presentation: 100 resizes, letterbox coverage, minimized surface, stable GDI "
           "resources\n";
    return passed;
}

Game* activeGame{};
Presenter* activePresenter{};
// Windows events handle discrete key presses. Continuous movement is polled
// separately in the frame loop. WM_PAINT is the only path that presents a frame.
LRESULT CALLBACK handleWindowMessage(HWND window, UINT message, WPARAM key, LPARAM data) {
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    if (!activeGame) {
        return DefWindowProcW(window, message, key, data);
    }
    if (message == WM_CLOSE) {
        if (!activeGame->title && !activeGame->save()) {
            MessageBoxW(window, activeGame->saveError.c_str(), L"Save problem",
                        MB_OK | MB_ICONWARNING);
            return 0;
        }
        DestroyWindow(window);
        return 0;
    }

    // Suppress Windows' automatic erase; Presenter covers the whole client area.
    // A separate on-screen clear would reintroduce the old flicker.
    if (message == WM_ERASEBKGND) {
        return 1;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC target = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        if (activePresenter) {
            activePresenter->paint(target, client.right, client.bottom);
        }
        EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_SIZE) {
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    if (message == WM_KILLFOCUS && !activeGame->title) {
        activeGame->paused = true;
    }

    // Bit 30 is set on repeated key-down messages from a held key. Ignore those
    // here so menu choices and sword presses happen once per physical press.
    if (message == WM_KEYDOWN && !(data & (1LL << 30))) {
        if (activeGame->title) {
            if (key == VK_UP || key == VK_DOWN || key == 'W' || key == 'S') {
                ++activeGame->menu;
            }
            if (key == VK_RETURN || key == VK_SPACE) {
                if (fs::exists(activeGame->saveFile) && activeGame->menu % 2 == 0) {
                    if (!activeGame->load()) {
                        MessageBoxW(
                            window,
                            L"The save could not be read. Select New Journey to begin again.",
                            L"Save problem", MB_OK | MB_ICONWARNING);
                    }
                } else {
                    activeGame->reset();
                    activeGame->title = false;
                    activeGame->showMessage(L"Find three Sunseals. Defeat each shrine's guards, "
                                            L"then press E at its chest.",
                                            7);
                }
            }
            if (key == VK_ESCAPE) {
                DestroyWindow(window);
            }
        } else if (activeGame->mapOpen) {
            if (key == 'M' || key == VK_TAB || key == VK_ESCAPE) {
                activeGame->mapOpen = false;
            }
        } else if (activeGame->paused) {
            if (key == VK_ESCAPE) {
                activeGame->paused = false;
            }
            if (key == 'R') {
                activeGame->enterRoom(RoomId::Village, {468, 280});
                activeGame->health = activeGame->maxHealth;
                activeGame->paused = false;
            }
            if (key == 'Q') {
                SendMessageW(window, WM_CLOSE, 0, 0);
            }
        } else {
            if (key == VK_ESCAPE) {
                activeGame->paused = true;
            }
            if (key == 'M' || key == VK_TAB) {
                activeGame->mapOpen = true;
            }
            if (key == VK_SPACE || key == 'J' || key == 'Z') {
                activeGame->swingSword();
            }
            if (key == 'E' && activeGame->respawnTimer <= 0) {
                activeGame->interact();
            }
        }
        return 0;
    }
    return DefWindowProcW(window, message, key, data);
}

// GDI+ is used only to encode diagnostic PNGs, not for the live renderer.
void saveScreenshot(HBITMAP bitmap, const fs::path& path) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    ULONG_PTR token{};
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&token, &input, nullptr) == Gdiplus::Ok) {
        {
            Gdiplus::Bitmap image(bitmap, nullptr);
            CLSID png{0x557cf406, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
            image.Save(path.c_str(), &png, nullptr);
        }
        Gdiplus::GdiplusShutdown(token);
    }
}

// APPLICATION: locate files, optionally run checks, create the window, then
// pump Windows messages and advance/render the game until the window closes.
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command, int show) {
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, 32768);
    fs::path root = fs::path(executable).parent_path().parent_path();
    Game game(root);
    activeGame = &game;
    bool test = std::wstring(command).find(L"--self-test") != std::wstring::npos;
    if (test) {
        // Test saves are isolated from actual player progress.
        game.saveFile = root / L"build" / L"test-save" / L"progress.txt";
        bool okay = runGameplayTests(game);
        HDC desktop = GetDC(nullptr), dc = CreateCompatibleDC(desktop);
        HBITMAP bitmap = CreateCompatibleBitmap(desktop, WindowWidth, WindowHeight);
        auto old = SelectObject(dc, bitmap);
        okay = testPresentation(dc, root) && okay;
        {
            Painter p(dc);
            game.reset();
            game.title = false;
            game.enterRoom(0, {410, 280});
            game.roomBannerTimer = game.toastTimer = 0;
            render(p, game);
            saveScreenshot(bitmap, root / L"screenshots" / L"gameplay.png");
            game.visited = {0, 1, 2, 3, 4, 5, 6, 7, 8};
            game.mapOpen = true;
            render(p, game);
            saveScreenshot(bitmap, root / L"screenshots" / L"map.png");
            game.mapOpen = false;
            game.enterRoom(8, {430, 280});
            game.bossAttackState = BossState::Warning;
            game.roomBannerTimer = 0;
            render(p, game);
            saveScreenshot(bitmap, root / L"screenshots" / L"boss.png");
            game.title = true;
            render(p, game);
            saveScreenshot(bitmap, root / L"screenshots" / L"title.png");
        }
        SelectObject(dc, old);
        DeleteObject(bitmap);
        DeleteDC(dc);
        ReleaseDC(nullptr, desktop);
        return okay ? 0 : 1;
    }
    SetProcessDPIAware();
    WNDCLASSW wc{};
    wc.lpfnWndProc = handleWindowMessage;
    wc.hInstance = instance;
    wc.lpszClassName = L"ZeldohWindow";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    if (!RegisterClassW(&wc)) {
        return 1;
    }
    RECT desired{0, 0, WindowWidth, WindowHeight};
    AdjustWindowRect(&desired, WS_OVERLAPPEDWINDOW, FALSE);
    HWND window =
        CreateWindowW(wc.lpszClassName, L"Zeldoh - Lantern of the Vale", WS_OVERLAPPEDWINDOW,
                      CW_USEDEFAULT, CW_USEDEFAULT, desired.right - desired.left,
                      desired.bottom - desired.top, nullptr, nullptr, instance, nullptr);
    if (!window) {
        return 1;
    }
    HDC dc = CreateCompatibleDC(nullptr);
    HDC screen = GetDC(window);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, WindowWidth, WindowHeight);
    ReleaseDC(window, screen);
    auto old = SelectObject(dc, bitmap);
    {
        Painter painter(dc);
        Presenter presentation(dc);
        activePresenter = &presentation;
        MSG message{};
        // A first paint can arrive during ShowWindow; initialize the scene first.
        render(painter, game);
        ShowWindow(window, show);
        auto previous = std::chrono::steady_clock::now();
        bool running = true;
        while (running) {
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (message.message == WM_QUIT) {
                    running = false;
                }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            if (!running) {
                break;
            }
            // Clamp catch-up time after stalls, then split it into <= 1/120 s
            // simulation steps so fast movement cannot tunnel through a tile.
            auto now = std::chrono::steady_clock::now();
            double deltaTime =
                std::min(.033, std::chrono::duration<double>(now - previous).count());
            previous = now;
            if (!game.title && !game.paused && !game.mapOpen) {
                auto held = [](int key) { return (GetAsyncKeyState(key) & 0x8000) != 0; };
                Vec2 movement{
                    double(held('D') || held(VK_RIGHT)) - double(held('A') || held(VK_LEFT)),
                    double(held('S') || held(VK_DOWN)) - double(held('W') || held(VK_UP))};
                int steps = std::max(1, int(std::ceil(deltaTime * 120)));
                for (int i = 0; i < steps; ++i) {
                    game.update(deltaTime / steps, movement);
                }
            } else {
                game.elapsedTime += deltaTime;
            }
            render(painter, game);
            // Ask WM_PAINT to present the completed frame without erasing it.
            InvalidateRect(window, nullptr, FALSE);
            UpdateWindow(window);
            Sleep(8);
        }
        activePresenter = nullptr;
    }
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    activeGame = nullptr;
    return 0;
}
