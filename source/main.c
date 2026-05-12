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

        // Predict and Interpolate movement for characters
        for (i32 entityIndex = 0; entityIndex < MAX_REMOTE_PLAYERS; entityIndex++) {
            Entity* entity = &currentConnectionState.remoteEntities[entityIndex];
            if (entity->entityType == ENTITY_CHARACTER) {
                entity->character.position.x += entity->character.velocity.x * deltaTime;
                entity->character.position.y += entity->character.velocity.y * deltaTime;
                
                entity->character.targetPosition.x += entity->character.velocity.x * deltaTime;
                entity->character.targetPosition.y += entity->character.velocity.y * deltaTime;
                
                entity->character.position = Vector2Lerp(entity->character.position, entity->character.targetPosition, 0.1f);
            }
        }

        Enemy_UpdateMovement(deltaTime);
        Player_UpdateMovement(deltaTime);

        camera.target = currentConnectionState.localPosition;

        BeginDrawing();
            ClearBackground(DARKGRAY);
            BeginMode2D(camera);
                Render_Map();

                for (i32 entityIndex = 0; entityIndex < MAX_REMOTE_PLAYERS; entityIndex++) {
                    Render_Entity(&currentConnectionState.remoteEntities[entityIndex]);
                }

                if (currentConnectionState.isConnected) {
                    DrawCircleV(currentConnectionState.localPosition, PLAYER_RADIUS, BLUE);
                    DrawText(TextFormat("ME (ID: %u)", currentConnectionState.localPlayerIdentification), currentConnectionState.localPosition.x - 30, currentConnectionState.localPosition.y - 40, 12, BLUE);
                } else {
                    DrawText("Searching for server...", currentConnectionState.localPosition.x - 60, currentConnectionState.localPosition.y, 20, WHITE);
                }
            EndMode2D();
            
            // UI Overlay
            DrawFPS(10, 10);
            if (!currentConnectionState.isConnected) {
                DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(BLACK, 0.6f));
                DrawText("CONNECTING TO SERVER...", SCREEN_WIDTH/2 - 150, SCREEN_HEIGHT/2, 20, WHITE);
                DrawText(TextFormat("Target IP: %s:%d", SERVER_IP, SERVER_PORT), SCREEN_WIDTH/2 - 100, SCREEN_HEIGHT/2 + 40, 10, GRAY);
            } else {
                DrawText("CONNECTED", 10, 30, 20, GREEN);
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
            
            // 1. Check for spawn delay
            if (GetTime() - entity->character.spawnTime < 3.0) {
                entity->character.velocity = (Vector2){ 0, 0 };
                continue;
            }

            // 2. Calculate direction to target player
            Vector2 targetPosition = entity->character.position; // Default to staying put
            bool targetFound = false;

            if (entity->character.targetPlayerID != 0) {
                if (currentConnectionState.localPlayerIdentification == entity->character.targetPlayerID) {
                    targetPosition = currentConnectionState.localPosition;
                    targetFound = true;
                } else {
                    // Look for remote player
                    u32 targetIndex = entity->character.targetPlayerID % MAX_REMOTE_PLAYERS;
                    Entity* targetPlayer = &currentConnectionState.remoteEntities[targetIndex];
                    if (targetPlayer->entityType == ENTITY_CHARACTER && targetPlayer->character.characterType == CHARACTER_PLAYER) {
                        targetPosition = targetPlayer->character.position;
                        targetFound = true;
                    }
                }
            }

            Vector2 steerDirection = { 0, 0 };
            if (targetFound) {
                steerDirection = Vector2Subtract(targetPosition, entity->character.position);
                f32 distToPlayer = Vector2Length(steerDirection);
                
                if (distToPlayer > 1.0f) {
                    steerDirection = Vector2Normalize(steerDirection);
                } else {
                    steerDirection = (Vector2){ 0, 0 };
                }
            }

            // 2. Avoidance pass (Repulsion from other enemies)
            Vector2 avoidanceForce = { 0, 0 };
            for (i32 otherIndex = 0; otherIndex < MAX_REMOTE_PLAYERS; otherIndex++) {
                if (entityIndex == otherIndex) continue;
                
                Entity* other = &currentConnectionState.remoteEntities[otherIndex];
                if (other->entityType == ENTITY_CHARACTER && other->character.characterType == CHARACTER_ENEMY) {
                    Vector2 diff = Vector2Subtract(entity->character.position, other->character.position);
                    f32 distance = Vector2Length(diff);
                    
                    if (distance > 0 && distance < ENEMY_AVOIDANCE_RADIUS) {
                        // Repulsion is stronger the closer they are
                        f32 forceMagnitude = (1.0f - (distance / ENEMY_AVOIDANCE_RADIUS)) * ENEMY_AVOIDANCE_FORCE;
                        avoidanceForce = Vector2Add(avoidanceForce, Vector2Scale(Vector2Normalize(diff), forceMagnitude));
                    }
                }
            }
            
            // Combine pursuit and avoidance
            Vector2 finalDirection = Vector2Add(steerDirection, avoidanceForce);
            if (Vector2Length(finalDirection) > 0.1f) {
                finalDirection = Vector2Normalize(finalDirection);
            }

            entity->character.velocity.x = finalDirection.x * PLAYER_SPEED * 0.5f;
            entity->character.velocity.y = finalDirection.y * PLAYER_SPEED * 0.5f;
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
                DrawText("PLAYER", entity->character.position.x - 20, entity->character.position.y - 40, 10, MAROON);
            } else if (entity->character.characterType == CHARACTER_ENEMY) {
                DrawCircleV(entity->character.position, PLAYER_RADIUS, PURPLE);
                DrawText("ENEMY", entity->character.position.x - 20, entity->character.position.y - 40, 10, PURPLE);
            }
            break;
        default:
            break;
    }
}

void Render_Map(void) {
    DrawRectangle(-MAP_SIZE/2, -MAP_SIZE/2, MAP_SIZE, MAP_SIZE, BEIGE);
    for (i32 gridIndex = -MAP_SIZE/2; gridIndex <= MAP_SIZE/2; gridIndex += 250) {
        DrawLine(gridIndex, -MAP_SIZE/2, gridIndex, MAP_SIZE/2, LIGHTGRAY);
        DrawLine(-MAP_SIZE/2, gridIndex, MAP_SIZE/2, gridIndex, LIGHTGRAY);
    }
}
