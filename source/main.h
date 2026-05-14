#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include "modern_types.h"
#include "raylib.h"
#include "raymath.h"

// Global Values
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

//~ Begin of Utility Structs
typedef struct f32Range { f32 minimum; f32 maximum; } f32Range;
typedef struct u16Range { u16 minimum; u16 maximum; } u16Range;
//~ End of Utility Structs

//~ Begin of Enums
typedef enum EntityType : u8 {
    ENTITY_UNDEFINED = 0,
    ENTITY_CHARACTER = 1,
    ENTITY_PROJECTILE = 2
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
} Character;

typedef struct Projectile {
    ProjectileType type;
    Vector2 position;
    Vector2 velocity;
    f32 lifetime;
    u32 ownerID;
    f32 damageAccumulated; // For spikes
    f32 tickTimer;         // For DOT effects
    f32 radius;            // For explosions
    u32 hitEnemies[8];     // For penetration (Crystal)
    u8 hitCount;
} Projectile;

typedef struct VisualEffect {
    Vector2 position;
    f32 radius;
    f32 lifetime;
    bool active;
} VisualEffect;

typedef struct Weapon {
    WeaponType type;
    f32 cooldownTimer;
    i32 level;
} Weapon;

typedef struct Entity{
    EntityType entityType;
    union { 
        Character character; 
        Projectile projectile;
    };
} Entity;

//~ Global Definitions
typedef struct GlobalVariables{
    Entity entities[MAX_ENTITY_AMOUNT];
    Weapon playerWeapons[5];
} GlobalVariables;

extern GlobalVariables globalVariables;

void Enemy_UpdateMovement(f32 deltaTime);

//~ Begin of Weapons
void Weapons_Update(f32 deltaTime);
void Projectile_UpdateMovement(f32 deltaTime);
void Weapon_FireFireballRing(Vector2 position, u32 ownerID);
//~ End of Weapons

//~ Begin of Player
void Player_UpdateMovement(f32 deltaTime);
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