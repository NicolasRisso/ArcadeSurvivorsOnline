#include "main.h"
#include "connection/connection.h"

// --- Global Variables ---
GlobalVariables globalVariables = { 0 };
InputState currentInputState = { 0 };
ConnectionState currentConnectionState = { 0 };

// --- Main Entry Point ---
int main(void) {
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Arcade Survivors Online");

    if (!Network_InitConnection(&currentConnectionState)) {
        CloseWindow();
        return 1;
    }

    Camera2D camera = { 0 };
    camera.target = currentConnectionState.localPosition;
    camera.offset = (Vector2){ SCREEN_WIDTH / 2.0f, SCREEN_HEIGHT / 2.0f };
    camera.rotation = 0.0f;
    camera.zoom = 1.0f;

    SetTargetFPS(TARGET_FPS);

    while (!currentInputState.quitApplication) {
        Network_UpdateConnection(&currentConnectionState);
        Input_Update(&currentInputState);

        f32 deltaTime = GetFrameTime();

        // Predict movement for characters
        for (i32 entityIndex = 0; entityIndex < MAX_REMOTE_PLAYERS; entityIndex++) {
            Entity* entity = &currentConnectionState.remoteEntities[entityIndex];
            if (entity->entityType == ENTITY_CHARACTER) {
                entity->character.position.x += entity->character.velocity.x * deltaTime;
                entity->character.position.y += entity->character.velocity.y * deltaTime;
            }
        }

        Enemy_UpdateMovement(deltaTime);
        Player_UpdateMovement(deltaTime);

        camera.target = currentConnectionState.localPosition;

        BeginDrawing();
            ClearBackground(RAYWHITE);
            BeginMode2D(camera);
                Render_Map();

                for (i32 entityIndex = 0; entityIndex < MAX_REMOTE_PLAYERS; entityIndex++) {
                    Render_Entity(&currentConnectionState.remoteEntities[entityIndex]);
                }

                if (currentConnectionState.isConnected) {
                    DrawCircleV(currentConnectionState.localPosition, PLAYER_RADIUS, BLUE);
                    DrawText(TextFormat("ME (ID: %u)", currentConnectionState.localPlayerIdentification), currentConnectionState.localPosition.x - 30, currentConnectionState.localPosition.y - 40, 12, DARKBLUE);
                } else {
                    DrawText("Connecting...", currentConnectionState.localPosition.x - 40, currentConnectionState.localPosition.y, 20, DARKGRAY);
                }
            EndMode2D();
            DrawFPS(10, 10);
            if (!currentConnectionState.isConnected) {
                DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(BLACK, 0.3f));
                DrawText("CONNECTING TO SERVER...", SCREEN_WIDTH/2 - 150, SCREEN_HEIGHT/2, 20, WHITE);
            }
        EndDrawing();
    }

    Network_CloseConnection();
    CloseWindow();
    return 0;
}

// --- Enemy Implementation ---
void Enemy_UpdateMovement(f32 deltaTime) {
    for (i32 entityIndex = 0; entityIndex < MAX_REMOTE_PLAYERS; entityIndex++) {
        Entity* entity = &currentConnectionState.remoteEntities[entityIndex];
        if (entity->entityType == ENTITY_CHARACTER && entity->character.characterType == CHARACTER_ENEMY) {
            // Move towards local player (simple deterministic AI)
            Vector2 direction = Vector2Subtract(currentConnectionState.localPosition, entity->character.position);
            if (Vector2Length(direction) > 1.0f) {
                direction = Vector2Normalize(direction);
                // Enemies move at 50% player speed
                entity->character.velocity.x = direction.x * PLAYER_SPEED * 0.5f;
                entity->character.velocity.y = direction.y * PLAYER_SPEED * 0.5f;
            } else {
                entity->character.velocity = (Vector2){ 0, 0 };
            }
        }
    }
}

// --- Player Implementation ---
void Player_UpdateMovement(f32 deltaTime) {
    if (!Network_IsConnected(&currentConnectionState)) return;

    Vector2 movementVelocity = (Vector2){ 0, 0 };
    movementVelocity.x = currentInputState.movementDirection.x * PLAYER_SPEED;
    movementVelocity.y = currentInputState.movementDirection.y * PLAYER_SPEED;

    currentConnectionState.localPosition.x += movementVelocity.x * deltaTime;
    currentConnectionState.localPosition.y += movementVelocity.y * deltaTime;
    
    Network_SendVelocity(&currentConnectionState, movementVelocity);
}

// --- Input System Implementation ---
void Input_Update(InputState* state) {
    state->movementDirection = (Vector2){ 0, 0 };
    
    if (IsKeyDown(KEY_W)) state->movementDirection.y -= 1;
    if (IsKeyDown(KEY_S)) state->movementDirection.y += 1;
    if (IsKeyDown(KEY_A)) state->movementDirection.x -= 1;
    if (IsKeyDown(KEY_D)) state->movementDirection.x += 1;
    
    if (state->movementDirection.x != 0 || state->movementDirection.y != 0) {
        state->movementDirection = Vector2Normalize(state->movementDirection);
    }
    
    state->quitApplication = WindowShouldClose();
}

// --- Renderer System Implementation ---
void Render_Entity(const Entity* entity) {
    if (entity->entityType == ENTITY_UNDEFINED) return;
    
    switch (entity->entityType) {
        case ENTITY_CHARACTER:
            if (entity->character.characterType == CHARACTER_PLAYER) {
                DrawCircleV(entity->character.position, PLAYER_RADIUS, RED);
                DrawText("PLAYER", entity->character.position.x - 20, entity->character.position.y - 40, 10, DARKGRAY);
            } else if (entity->character.characterType == CHARACTER_ENEMY) {
                DrawCircleV(entity->character.position, PLAYER_RADIUS, MAROON);
                DrawText("ENEMY", entity->character.position.x - 20, entity->character.position.y - 40, 10, DARKGRAY);
            }
            break;
        default:
            break;
    }
}

void Render_Map(void) {
    DrawRectangle(-MAP_SIZE/2, -MAP_SIZE/2, MAP_SIZE, MAP_SIZE, LIGHTGRAY);
    for (i32 gridIndex = -MAP_SIZE/2; gridIndex <= MAP_SIZE/2; gridIndex += 500) {
        DrawLine(gridIndex, -MAP_SIZE/2, gridIndex, MAP_SIZE/2, GRAY);
        DrawLine(-MAP_SIZE/2, gridIndex, MAP_SIZE/2, gridIndex, GRAY);
    }
}
