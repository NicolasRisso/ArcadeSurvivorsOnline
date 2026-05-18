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

#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 720
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

//~ Begin of Utility Structs
typedef struct f32Range { f32 minimum; f32 maximum; } f32Range;
typedef struct u16Range { u16 minimum; u16 maximum; } u16Range;
//~ End of Utility Structs

//~ Begin of Enums
typedef enum EntityType : u8 {
    ENTITY_UNDEFINED = 0,
    ENTITY_CHARACTER = 1,
    ENTITY_PROJECTILE = 2,
    ENTITY_XP_CRYSTAL = 3
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

#define MAX_RELIC_LEVEL 5

typedef struct Relic {
    RelicType type;
    u8 level;
} Relic;


//~ Begin of Structs
typedef struct Character{
    CharacterType characterType;
    Vector2 position;
    Vector2 velocity;
    Vector2 targetPosition; 
    f64 spawnTime;
    u32 targetPlayerID;
    f32 health;
    f32 maxHealth;
    u8 weaponsMask;
    f32 damageFlashTimer;
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

typedef struct Entity{
    EntityType entityType;
    union { 
        Character character; 
        Projectile proj;
        XPCrystal xpCrystal;
    };
} Entity;

//~ Global Definitions
typedef struct GlobalVariables{
    Entity entities[MAX_ENTITY_AMOUNT];
    Weapon playerWeapons[4];
    Relic playerRelics[4];
    PlayerAttributes playerAttributes[MAX_PLAYERS];
} GlobalVariables;

extern GlobalVariables globalVariables;

void Enemy_UpdateMovement(f32 deltaTime);

//~ Begin of Weapons
void Weapons_Update(f32 deltaTime);
void Projectile_UpdateMovement(f32 deltaTime);
void Weapon_FireFireballRing(Vector2 position, u32 ownerID);
//~ End of Weapons

//~ Begin of Player
struct ConnectionState;
typedef struct ConnectionState ConnectionState;
void Player_UpdateMovement(f32 deltaTime);
void Player_UpdateAttributes(ConnectionState* state, PlayerAttributes attr);
void ApplyLifesteal(ConnectionState* state, u32 enemyIndex, f32 damage, bool isAoE);
void Player_RecalculateAttributes(void);
void DrawStatsOverlay(void);
//~ End of Player

//~ Begin of Input
typedef struct InputState {
    Vector2 movementDirection;
    bool quitApplication;
} InputState;

void Input_Update(InputState* state);
//~ End of Input

//~ Begin of Renderer
void Render_Entity(const Entity* entity);
void Render_Map(void);
//~ End of Renderer