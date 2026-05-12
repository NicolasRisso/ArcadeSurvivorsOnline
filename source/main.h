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
    ENTITY_CHARACTER = 1
} EntityType;

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
    Vector2 targetPosition; // The "ground truth" from the server
    f64 spawnTime;
    u32 targetPlayerID;
} Character;

typedef struct Entity{
    EntityType entityType;
    union { Character character; };
} Entity;

//~ Global Definitions
typedef struct GlobalVariables{
    Entity entities[MAX_ENTITY_AMOUNT];
} GlobalVariables;

extern GlobalVariables globalVariables;

void Enemy_UpdateMovement(f32 deltaTime);

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