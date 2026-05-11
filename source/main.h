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
typedef struct f32Range { f32 min; f32 max; } f32Range;
typedef struct u16Range { u16 min; u16 max; } u16Range;
//~ End of Utility Structs

//~ Begin of Enums
typedef enum EntityType : u8 {
    ENTITY_UNDEFINED = 0,
    ENTITY_CHARACTER = 1
} EntityType;

//~ Begin of Structs
typedef struct Entity{
    EntityType entityType;
} Entity;

//~ Global Definitions
typedef struct GlobalVariables{
    Entity entities[MAX_ENTITY_AMOUNT];
} GlobalVariables;

GlobalVariables globalVariables;

//~ Begin of Player
void Player_UpdateMovement(f32 deltaTime);
//~ End of Player