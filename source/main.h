#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include "modern_types.h"
#include "raylib.h"
#include "raymath.h"

// Global Values
#define MAX_REMOTE_ENTITIES 5100
#define MAX_ENEMIES 3000
#define MAX_XP_CRYSTALS 2000
#define MAX_PLAYERS 4
#define MAX_MENU_PARTICLES 80

#define MAGNET_RADIUS 200.0f
#define COLLECT_RADIUS 30.0f
#define XP_PER_CRYSTAL 20.0f
#define MAP_SIZE 10000.0f
#define PLAYER_SPEED 300.0f
#define PLAYER_RADIUS 20.0f
#define ENEMY_AVOIDANCE_RADIUS 45.0f
#define ENEMY_AVOIDANCE_FORCE 0.5f
#define PROJECTILE_SPEED 500.0f
#define PROJECTILE_LIFETIME 3.0f
#define FIREBALL_COOLDOWN 2.0f
#define CRYSTAL_COOLDOWN 1.5f
#define BOMB_COOLDOWN 2.5f
#define SPIKE_COOLDOWN 3.5f

#define FIREBALL_RADIUS 50.0f
#define BOMB_RADIUS 150.0f
#define SPIKE_RADIUS 40.0f
#define AURA_RADIUS 120.0f

#define ENEMY_DEFAULT_HP 100.0f
#define INITIAL_WINDOW_WIDTH 1280
#define INITIAL_WINDOW_HEIGHT 720
#define SCREEN_WIDTH GetScreenWidth()
#define SCREEN_HEIGHT GetScreenHeight()
#define GetUIScale() fminf((float)GetScreenWidth() / 1280.0f, (float)GetScreenHeight() / 720.0f)
#define TARGET_FPS 60

#define MAX_ENTITY_AMOUNT 20000

#define DEFAULT_MAX_HEALTH 100.0f
#define DEFAULT_DAMAGE 1.0f
#define DEFAULT_ATTACK_SPEED 1.0f
#define DEFAULT_MOVEMENT_SPEED 1.0f
#define DEFAULT_SIZE 1.0f
#define DEFAULT_XP_GAINED 1.0f
#define DEFAULT_LIFESTEAL 0.0f

#define RELIC_LEVELUP_HEALTH 0.12f
#define RELIC_LEVELUP_DAMAGE 0.08f
#define RELIC_LEVELUP_ATTACKSPEED 0.06f
#define RELIC_LEVELUP_SIZE 0.15f
#define RELIC_LEVELUP_MOVEMENTSPEED 0.09f
#define RELIC_LEVELUP_XPGAIN 0.08f
#define RELIC_LEVELUP_LIFESTEAL 0.01f

//~ Begin of Enums
typedef enum GameState {
    STATE_MAIN_MENU = 0,
    STATE_JOIN_IP = 1,
    STATE_LOBBY = 2,
    STATE_IN_GAME = 3
} GameState;

typedef enum InGameState {
    IN_GAME_PLAYING = 0,
    IN_GAME_SPECTATING = 1
} InGameState;

typedef enum EntityType : u8 {
    ENTITY_UNDEFINED = 0,
    ENTITY_CHARACTER = 1,
    ENTITY_PROJECTILE = 2,
    ENTITY_XP_CRYSTAL = 3,
    ENTITY_DAMAGE_POPUP = 4
} EntityType;

typedef enum ProjectileType : u8 {
    PROJECTILE_UNDEFINED = 0,
    PROJECTILE_FIREBALL = 1,
    PROJECTILE_CRYSTAL = 2,
    PROJECTILE_BOMB = 3,
    PROJECTILE_SPIKE = 4,
    PROJECTILE_EXPLOSION = 5
} ProjectileType;

typedef enum WeaponType : u8 {
    WEAPON_UNDEFINED = 0,
    WEAPON_FIREBALL_RING = 1,
    WEAPON_CRYSTAL_STAFF = 2,
    WEAPON_DEATH_AURA = 3,
    WEAPON_BOMB_SHOES = 4,
    WEAPON_NATURE_SPIKES = 5
} WeaponType;

typedef enum CharacterType : u8 {
    CHARACTER_UNDEFINED = 0,
    CHARACTER_PLAYER = 1,
    CHARACTER_ENEMY = 2
} CharacterType;

typedef enum EnemyClass : u8 {
    ENEMY_CLASS_NORMAL = 0,
    ENEMY_CLASS_FAST = 1,
    ENEMY_CLASS_TANK = 2,
    ENEMY_CLASS_BOSS = 3
} EnemyClass;

typedef enum RelicType : u8 {
    RELIC_UNDEFINED = 0,
    RELIC_HEALTH = 1,
    RELIC_DAMAGE = 2,
    RELIC_ATTACK_SPEED = 3,
    RELIC_SIZE = 4,
    RELIC_MOVEMENT_SPEED = 5,
    RELIC_XP_GAIN = 6,
    RELIC_LIFE_STEAL = 7
} RelicType;

typedef enum LogoType : u8 {
    LOGO_UNDEFINED = 0,
    LOGO_CRYSTAL_STAFF = 1,
    LOGO_BOMB_SHOES = 2,
    LOGO_DEATH_AURA = 3,
    LOGO_NATURE_SPIKES = 4,
    LOGO_FIRE_RING = 5,
    LOGO_HEALTH_RELIC = 6,
    LOGO_DAMAGE_RELIC = 7,
    LOGO_SIZE_RELIC = 8,
    LOGO_ATTACK_SPEED_RELIC = 9,
    LOGO_MOVEMENT_SPEED_RELIC = 10,
    LOGO_XP_RELIC = 11,
    LOGO_LIFESTEAL_RELIC = 12,
    LOGO_COUNT = 13
} LogoType;

typedef enum SpriteType : u8 {
    SPRITE_UNDEFINED       = 0,  // Error sentinel — maps to blank cell (3,3)
    SPRITE_PLAYER_IDLE     = 1,  // Cell (0,0)
    SPRITE_PLAYER_WALK_1   = 2,  // Cell (1,0)
    SPRITE_PLAYER_WALK_2   = 3,  // Cell (2,0)
    SPRITE_PLAYER_WALK_3   = 4,  // Cell (3,0)
    SPRITE_ENEMY_NORMAL    = 5,  // Cell (0,1)
    SPRITE_ENEMY_FAST      = 6,  // Cell (1,1)
    SPRITE_ENEMY_TANK      = 7,  // Cell (2,1)
    SPRITE_ENEMY_BOSS      = 8,  // Cell (3,1)
    SPRITE_XP_CRYSTAL      = 9,  // Cell (0,2)
    SPRITE_FIREBALL        = 10, // Cell (1,2)
    SPRITE_CRYSTAL_SHARD   = 11, // Cell (2,2)
    SPRITE_NATURE_SPIKES   = 12, // Cell (3,2)
    SPRITE_BOMB            = 13, // Cell (0,3)
    SPRITE_TYPE_COUNT      = 14
} SpriteType;

typedef enum RendererType : u8 {
    RENDERER_UNDEFINED       = 0,
    RENDERER_NO_SPRITE       = 1,  // Keep primitive rendering (explosions, damage popups)
    RENDERER_STATIC_SPRITE   = 2,  // Single frame from atlas
    RENDERER_ANIMATED_SPRITE = 3   // Multiple frames cycling from atlas
} RendererType;

typedef struct StaticSprite {
    SpriteType spriteIndex; // Index into spriteRects[]
} StaticSprite;

typedef struct AnimatedSprite {
    SpriteType frames[4];   // Up to 4 frame indices into spriteRects[]
    u8 frameCount;          // How many frames in the animation cycle
    f32 frameDuration;      // Seconds per frame
} AnimatedSprite;

typedef struct SpriteRenderer {
    RendererType type;
    f32 drawSize;           // World-space width/height for the destination rect
    union {
        StaticSprite   staticSprite;
        AnimatedSprite animatedSprite;
    };
} SpriteRenderer;

#define MAX_RELIC_LEVEL 5

typedef struct Relic {
    RelicType type;
    u8 level;
} Relic;

//~ Begin of Structs
typedef struct Character {
    CharacterType characterType;
    Vector2 position;
    Vector2 velocity;
    Vector2 targetPosition; 
    f64 spawnTime;
    u32 targetPlayerID;
    f32 health;
    f32 maxHealth;
    u8 weaponsMask;
    u8 weaponLevels[5];
    u8 relicLevels[7];
    f32 damageFlashTimer;
    EnemyClass enemyClass;
} Character;

typedef struct Projectile {
    ProjectileType type;
    Vector2 position;
    Vector2 velocity;
    f32 lifetime;
    u32 ownerID;
    f32 damageAccumulated; // For spikes
    f32 tickTimer;
    f32 radius;
    f32 damage;
    i32 pierce;
    u32 hitEnemies[8];
    u8 hitCount;
} Projectile;

typedef struct XPCrystal {
    Vector2 position;
    f32 xpValue;
    bool isMagnetized;
    f32 magnetizedTimer;
    u32 targetPlayerID;
} XPCrystal;

typedef struct VisualEffect {
    Vector2 position;
    f32 radius;
    f32 lifetime;
    bool active;
} VisualEffect;

typedef struct DamagePopup {
    Vector2 position;
    f32 damageValue;
    f32 lifetime;
    Color color;
} DamagePopup;

typedef struct WeaponStats {
    f32 damage;
    f32 attackSpeed;
    f32 size;
    union {
        struct { i32 pierce; i32 projectileAmount; } crystalStaff;
        struct { f32 damageCap; i32 spikeAmount; } natureSpikes;
        struct { f32 explosionSize; } fireball;
    } spec;
} WeaponStats;

typedef struct Weapon {
    WeaponType type;
    u8 level;
    f32 cooldownTimer;
    WeaponStats stats;
} Weapon;

typedef struct PlayerAttributes {
    f32 maxHealth;
    f32 damage;
    f32 attackSpeed;
    f32 movementSpeed;
    f32 size;
    f32 xpGained;
    f32 lifeSteal;
} PlayerAttributes;

typedef struct LevelUpOption {
    bool isRelic;
    u8 type;
    const char* name;
    const char* description;
    Color color;
} LevelUpOption;

typedef struct Entity {
    EntityType entityType;
    union { 
        Character character; 
        Projectile proj;
        XPCrystal xpCrystal;
        DamagePopup damagePopup;
    };
} Entity;

typedef struct MenuParticle {
    Vector2 position;
    Vector2 velocity;
    f32 size;
    f32 alpha;
    Color color;
} MenuParticle;

typedef struct InputState {
    Vector2 movementDirection;
    bool quitApplication;
} InputState;

typedef struct UpgradeCandidate {
    bool isRelic;
    u8 type;
} UpgradeCandidate;

typedef struct Assets {
    // Logo atlas (weapon/relic icons for UI)
    Texture2D logoAtlas;
    Rectangle logoRects[LOGO_COUNT];
    
    // Sprite atlas (gameplay entities)
    Texture2D spriteAtlas;
    Rectangle spriteRects[SPRITE_TYPE_COUNT]; // Source rects for each sprite cell
    SpriteRenderer entityRenderers[SPRITE_TYPE_COUNT]; // Enum-indexed renderer definitions
    
    bool loaded;
} Assets;

#include "connection/connection.h"

//~ Centralized State Definitions
typedef struct GlobalVariables {
    Entity entities[MAX_ENTITY_AMOUNT];
    Weapon playerWeapons[4];
    Relic playerRelics[4];
    PlayerAttributes playerAttributes[MAX_PLAYERS];

    InputState currentInputState;
    ConnectionState currentConnectionState;
    GameState currentGameState;
    InGameState currentInGameState;

    char playerNames[MAX_PLAYERS][32];
    char myNameInput[32];
    char joinIpAddress[64];

    f32 playerXP;
    f32 xpToNextLevel;
    u16 playerLevel;
    f32 gameTime;

    u32 spectatedPlayerID;

    bool isChoosingUpgrade;
    LevelUpOption upgradeOptions[3];
    i32 pendingLevels;

    MenuParticle menuParticles[MAX_MENU_PARTICLES];
    bool particlesInitialized;
    Assets assets;
    f32 localFacingX; // 1.0f = right, -1.0f = left (default 1.0f)
} GlobalVariables;

extern GlobalVariables globalVariables;

//~ Begin of Assets
void Assets_Load(void);
void Assets_Unload(void);
//~ End of Assets

//~ Begin of Enemy
u32 Enemy_GetAlternativeTargetPlayerID(u32 deadPlayerID, u32 enemyIndex);
void Enemy_UpdateMovement(f32 deltaTime);
//~ End of Enemy

//~ Begin of Input
void Input_Update(InputState* state);
//~ End of Input

//~ Begin of Player
void Player_ApplyLifesteal(ConnectionState* state, u32 enemyIndex, f32 damage, bool isAoE, f32 weaponMult);
u32 Player_FindNextAlivePlayer(u32 currentSpectatedID, bool forward);
bool Player_IsConnected(i32 playerID);
void Player_RecalculateAttributes(void);
void Player_UpdateAttributes(ConnectionState* state, PlayerAttributes attr);
void Player_UpdateMovement(f32 deltaTime);
//~ End of Player

//~ Begin of Renderer
bool Render_DrawCustomButton(Rectangle rect, const char* text, Color baseColor, Color hoverColor, Vector2 mousePos, f32 deltaTime, f32* animProgress);
void Render_DrawCustomTextBox(Rectangle rect, char* textBuffer, i32 maxLen, bool* active, const char* label, Vector2 mousePos);
void Render_DrawGameTimer(void);
void Render_DrawHeart(Vector2 center, f32 size, Color color);
void Render_DrawJoinInputScreen(Vector2 mousePos, f32 deltaTime);
void Render_DrawLobby(Vector2 mousePos, f32 deltaTime);
void Render_DrawMainMenu(Vector2 mousePos, f32 deltaTime);
void Render_DrawStatsOverlay(void);
void Render_DrawTombstone(Vector2 pos, const char* name, Color nameColor);
void Render_DrawUpgradeCards(void);
void Render_DrawXPBar(void);
void Render_Entity(const Entity* entity);
void Render_Map(void);
void Render_Sprite(SpriteType spriteType, Vector2 position, f32 size, bool flipX, f32 animTime);
void Render_SpawnDamagePopup(Vector2 position, f32 damage, Color color);
void Render_UpdateAndDrawMenuParticles(f32 deltaTime);
//~ End of Renderer

//~ Begin of Weapons
void Weapon_ApplyUpgrade(i32 optionIndex);
void Weapon_FireFireballRing(Vector2 position, u32 ownerID);
void Weapon_GenerateUpgradeOptions(LevelUpOption options[3]);
void Weapon_Initialize(Weapon* w, WeaponType type);
void Weapon_ProjectileUpdateMovement(f32 deltaTime);
void Weapon_Upgrade(Weapon* w);
void Weapons_Update(f32 deltaTime);
//~ End of Weapons