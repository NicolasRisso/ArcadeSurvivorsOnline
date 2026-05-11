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

#define SCREEN_WIDTH 1920
#define SCREEN_HEIGHT 1080
#define TARGET_FPS 165

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

//~ Begin of Structs
typedef struct Entity{
    u32 identification;
    EntityType entityType;
    Vector2 position;
    Vector2 velocity;
} Entity;

//~ Global Definitions
typedef struct GlobalVariables{
    Entity entities[MAX_ENTITY_AMOUNT];
} GlobalVariables;

extern GlobalVariables globalVariables;

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