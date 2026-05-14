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

    // Initialize player weapons
    for (int i = 0; i < 5; i++) {
        globalVariables.playerWeapons[i].type = (WeaponType)(i + 1);
        globalVariables.playerWeapons[i].cooldownTimer = 0.0f;
        globalVariables.playerWeapons[i].level = 1;
        
        // Update local mask
        if (globalVariables.playerWeapons[i].type != WEAPON_UNDEFINED) {
            // Wait, we need a way to set the local mask. 
            // We'll just assume local player has all for now in Render.
        }
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
        for (i32 entityIndex = 0; entityIndex < MAX_REMOTE_ENTITIES; entityIndex++) {
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
        Weapons_Update(deltaTime);
        Projectile_UpdateMovement(deltaTime);
        Network_SendDeathReport(&currentConnectionState);

        camera.target = currentConnectionState.localPosition;

        BeginDrawing();
            ClearBackground(DARKGRAY);
            BeginMode2D(camera);
                Render_Map();

                // Update and Render Local Visual Effects
                f32 frameDelta = GetFrameTime();
                for (int i = 0; i < 128; i++) {
                    if (currentConnectionState.localVisualEffects[i].active) {
                        currentConnectionState.localVisualEffects[i].lifetime -= frameDelta;
                        if (currentConnectionState.localVisualEffects[i].lifetime <= 0) {
                            currentConnectionState.localVisualEffects[i].active = false;
                        } else {
                            f32 scale = currentConnectionState.localVisualEffects[i].lifetime / 0.5f;
                            DrawCircleV(currentConnectionState.localVisualEffects[i].position, currentConnectionState.localVisualEffects[i].radius * (1.0f - scale), Fade(ORANGE, scale));
                            DrawCircleV(currentConnectionState.localVisualEffects[i].position, (currentConnectionState.localVisualEffects[i].radius * 0.6f) * (1.0f - scale), Fade(YELLOW, scale));
                        }
                    }
                }

                for (i32 entityIndex = 0; entityIndex < MAX_REMOTE_ENTITIES; entityIndex++) {
                    const Entity* entity = &currentConnectionState.remoteEntities[entityIndex];
                    if (entity->entityType == ENTITY_CHARACTER) {
                        // Draw Death Aura for remote players
                        if (entity->character.weaponsMask & (1 << (WEAPON_DEATH_AURA - 1))) {
                            DrawCircleLinesV(entity->character.position, AURA_RADIUS, Fade(BLACK, 0.3f));
                            DrawCircleV(entity->character.position, AURA_RADIUS, Fade(BLACK, 0.1f));
                        }
                    }
                    Render_Entity(entity);
                }

                if (currentConnectionState.isConnected) {
                    DrawCircleV(currentConnectionState.localPosition, PLAYER_RADIUS, BLUE);
                    DrawText(TextFormat("ME (ID: %u)", currentConnectionState.localPlayerIdentification), currentConnectionState.localPosition.x - 30, currentConnectionState.localPosition.y - 40, 12, BLUE);
                    
                    // Draw local player's death aura
                    // (We can use a local mask or just check weapons)
                    for (int i = 0; i < 5; i++) {
                        if (globalVariables.playerWeapons[i].type == WEAPON_DEATH_AURA) {
                            DrawCircleLinesV(currentConnectionState.localPosition, AURA_RADIUS, Fade(BLACK, 0.3f));
                            DrawCircleV(currentConnectionState.localPosition, AURA_RADIUS, Fade(BLACK, 0.1f));
                            break;
                        }
                    }
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
    for (i32 entityIndex = 0; entityIndex < MAX_REMOTE_ENTITIES; entityIndex++) {
        Entity* entity = &currentConnectionState.remoteEntities[entityIndex];
        if (entity->entityType == ENTITY_CHARACTER && entity->character.characterType == CHARACTER_ENEMY) {
            
            // 1. Calculate direction to target player
            Vector2 targetPosition = entity->character.position; // Default to staying put
            bool targetFound = false;

            if (entity->character.targetPlayerID != 0) {
                if (currentConnectionState.localPlayerIdentification == entity->character.targetPlayerID) {
                    targetPosition = currentConnectionState.localPosition;
                    targetFound = true;
                } else {
                    // Look for remote player
                    u32 targetIndex = entity->character.targetPlayerID % MAX_REMOTE_ENTITIES;
                    Entity* targetPlayer = &currentConnectionState.remoteEntities[targetIndex];
                    if (targetPlayer->entityType == ENTITY_CHARACTER && targetPlayer->character.characterType == CHARACTER_PLAYER) {
                        targetPosition = targetPlayer->character.position;
                        targetFound = true;
                    } else {
                        // Diagnostic log (throttled)
                        static f32 lastLogTime = 0;
                        if (GetTime() - lastLogTime > 2.0) {
                            printf("Enemy %d searching for Player %u at index %u... Found EntityType %d\n", 
                                   entityIndex, entity->character.targetPlayerID, targetIndex, targetPlayer->entityType);
                            lastLogTime = GetTime();
                        }
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
            for (i32 otherIndex = 0; otherIndex < MAX_REMOTE_ENTITIES; otherIndex++) {
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

            // Diagnostic: Heartbeat every 3 seconds for active enemies
            static f32 lastHeartbeatTime = 0;
            if (GetTime() - lastHeartbeatTime > 3.0) {
                printf("Enemy %d active! Pos: (%.1f, %.1f), Vel: (%.1f, %.1f), TargetID: %u\n", 
                       entityIndex, entity->character.position.x, entity->character.position.y,
                       entity->character.velocity.x, entity->character.velocity.y,
                       entity->character.targetPlayerID);
                lastHeartbeatTime = GetTime();
            }
        }
    }
}

// --- Weapon System Implementation ---
void Weapons_Update(f32 deltaTime) {
    if (!currentConnectionState.isConnected) return;

    for (i32 i = 0; i < 5; i++) {
        Weapon* weapon = &globalVariables.playerWeapons[i];
        if (weapon->type == WEAPON_UNDEFINED) continue;
        
        weapon->cooldownTimer -= deltaTime;
        if (weapon->cooldownTimer <= 0) {
            Network_SendWeaponFire(&currentConnectionState, weapon->type);
            
            if (weapon->type == WEAPON_DEATH_AURA) {
                // Death Aura Logic: Check enemies in radius and deal damage
                int hitCount = 0;
                for (i32 enemyIndex = 0; enemyIndex < MAX_REMOTE_ENTITIES; enemyIndex++) {
                    Entity* enemy = &currentConnectionState.remoteEntities[enemyIndex];
                    if (enemy->entityType == ENTITY_CHARACTER && enemy->character.characterType == CHARACTER_ENEMY) {
                        if (CheckCollisionCircles(currentConnectionState.localPosition, AURA_RADIUS, enemy->character.position, PLAYER_RADIUS)) {
                            Network_SendDamage(&currentConnectionState, enemyIndex, 15.0f); // DAMAGE_AURA
                            hitCount++;
                            if (hitCount >= 100) break;
                        }
                    }
                }
            }
            
            switch (weapon->type) {
                case WEAPON_FIREBALL_RING: weapon->cooldownTimer = FIREBALL_COOLDOWN; break;
                case WEAPON_CRYSTAL_STAFF: weapon->cooldownTimer = CRYSTAL_COOLDOWN; break;
                case WEAPON_DEATH_AURA: weapon->cooldownTimer = 0.2f; break; // Damage interval
                case WEAPON_BOMB_SHOES: weapon->cooldownTimer = BOMB_COOLDOWN; break;
                case WEAPON_NATURE_SPIKES: weapon->cooldownTimer = SPIKE_COOLDOWN; break;
                default: weapon->cooldownTimer = 1.0f; break;
            }
        }
    }
}

void Projectile_UpdateMovement(f32 deltaTime) {
    for (i32 i = 0; i < MAX_REMOTE_ENTITIES; i++) {
        Entity* entity = &currentConnectionState.remoteEntities[i];
        if (entity->entityType == ENTITY_PROJECTILE) {
            Projectile* proj = &entity->projectile;
            
            // Move
            proj->position.x += proj->velocity.x * deltaTime;
            proj->position.y += proj->velocity.y * deltaTime;
            
            // Lifetime (Predicted locally)
            proj->lifetime -= deltaTime;
            // Collision check with enemies (Client prediction)
            if (proj->type == PROJECTILE_FIREBALL) {
                for (i32 enemyIndex = 0; enemyIndex < MAX_REMOTE_ENTITIES; enemyIndex++) {
                    Entity* remoteEntity = &currentConnectionState.remoteEntities[enemyIndex];
                    if (remoteEntity->entityType == ENTITY_CHARACTER && remoteEntity->character.characterType == CHARACTER_ENEMY) {
                        if (CheckCollisionCircles(proj->position, 10, remoteEntity->character.position, PLAYER_RADIUS)) {
                            // Prediction: Hide fireball and show local explosion instantly
                            for (int v = 0; v < 128; v++) {
                                if (!currentConnectionState.localVisualEffects[v].active) {
                                    currentConnectionState.localVisualEffects[v].active = true;
                                    currentConnectionState.localVisualEffects[v].position = proj->position;
                                    currentConnectionState.localVisualEffects[v].radius = FIREBALL_RADIUS;
                                    currentConnectionState.localVisualEffects[v].lifetime = 0.5f;
                                    break;
                                }
                            }
                            
                            // Explosion damage report
                            for (i32 otherIndex = 0; otherIndex < MAX_REMOTE_ENTITIES; otherIndex++) {
                                Entity* other = &currentConnectionState.remoteEntities[otherIndex];
                                if (other->entityType == ENTITY_CHARACTER && other->character.characterType == CHARACTER_ENEMY) {
                                    if (CheckCollisionCircles(proj->position, FIREBALL_RADIUS, other->character.position, PLAYER_RADIUS)) {
                                        Network_SendDamage(&currentConnectionState, otherIndex, 50.0f); // DAMAGE_FIREBALL
                                    }
                                }
                            }
                            
                            Network_SendProjectileExplode(&currentConnectionState, i);
                            entity->entityType = ENTITY_UNDEFINED;
                            break;
                        }
                    }
                }
            } else if (proj->type == PROJECTILE_CRYSTAL) {
                for (i32 enemyIndex = 0; enemyIndex < MAX_REMOTE_ENTITIES; enemyIndex++) {
                    Entity* remoteEntity = &currentConnectionState.remoteEntities[enemyIndex];
                    if (remoteEntity->entityType == ENTITY_CHARACTER && remoteEntity->character.characterType == CHARACTER_ENEMY) {
                        if (CheckCollisionCircles(proj->position, 10, remoteEntity->character.position, PLAYER_RADIUS)) {
                            // Penetration check
                            bool alreadyHit = false;
                            for (int h = 0; h < proj->hitCount; h++) {
                                if (proj->hitEnemies[h] == (u32)enemyIndex) { alreadyHit = true; break; }
                            }
                            if (!alreadyHit) {
                                Network_SendDamage(&currentConnectionState, enemyIndex, 100.0f); // DAMAGE_CRYSTAL
                                if (proj->hitCount < 8) proj->hitEnemies[proj->hitCount++] = enemyIndex;
                            }
                        }
                    }
                }
            } else if (proj->type == PROJECTILE_BOMB) {
                if (proj->lifetime <= 0.0f) { // Explosion trigger (BOMB_DELAY=2.0)
                    // Prediction: Hide bomb and show local explosion instantly
                    for (int v = 0; v < 128; v++) {
                        if (!currentConnectionState.localVisualEffects[v].active) {
                            currentConnectionState.localVisualEffects[v].active = true;
                            currentConnectionState.localVisualEffects[v].position = proj->position;
                            currentConnectionState.localVisualEffects[v].radius = BOMB_RADIUS;
                            currentConnectionState.localVisualEffects[v].lifetime = 0.5f;
                            break;
                        }
                    }
                
                    for (i32 otherIndex = 0; otherIndex < MAX_REMOTE_ENTITIES; otherIndex++) {
                        Entity* other = &currentConnectionState.remoteEntities[otherIndex];
                        if (other->entityType == ENTITY_CHARACTER && other->character.characterType == CHARACTER_ENEMY) {
                            if (CheckCollisionCircles(proj->position, BOMB_RADIUS, other->character.position, PLAYER_RADIUS)) {
                                Network_SendDamage(&currentConnectionState, otherIndex, 500.0f); // DAMAGE_BOMB
                            }
                        }
                    }
                    entity->entityType = ENTITY_UNDEFINED;
                }
            } else if (proj->type == PROJECTILE_SPIKE) {
                // Spikes deal damage every 0.15s
                proj->tickTimer += deltaTime;
                if (proj->tickTimer >= 0.15f) {
                    for (i32 otherIndex = 0; otherIndex < MAX_REMOTE_ENTITIES; otherIndex++) {
                        Entity* other = &currentConnectionState.remoteEntities[otherIndex];
                        if (other->entityType == ENTITY_CHARACTER && other->character.characterType == CHARACTER_ENEMY) {
                            if (CheckCollisionCircles(proj->position, SPIKE_RADIUS, other->character.position, PLAYER_RADIUS)) {
                                Network_SendDamage(&currentConnectionState, otherIndex, 20.0f); // DAMAGE_SPIKE
                                proj->damageAccumulated += 20.0f;
                            }
                        }
                    }
                    if (proj->damageAccumulated >= 150.0f) entity->entityType = ENTITY_UNDEFINED;
                    proj->tickTimer = 0;
                }
            }

            // Lifetime expiry (Predicted locally)
            if (proj->lifetime <= 0 && entity->entityType != ENTITY_UNDEFINED) {
                entity->entityType = ENTITY_UNDEFINED;
            }
        }
    }
}

void Weapon_FireFireballRing(Vector2 position, u32 ownerID) {
    // Deprecated: Now handled by server via Network_SendWeaponFire
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
    
    // Hide dead enemies (waiting for server despawn)
    if (entity->entityType == ENTITY_CHARACTER && entity->character.characterType == CHARACTER_ENEMY && entity->character.health <= 0) return;

    switch (entity->entityType) {
        case ENTITY_CHARACTER:
            if (entity->character.characterType == CHARACTER_PLAYER) {
                DrawCircleV(entity->character.position, PLAYER_RADIUS, RED);
                DrawText("PLAYER", entity->character.position.x - 20, entity->character.position.y - 40, 10, MAROON);
            } else if (entity->character.characterType == CHARACTER_ENEMY) {
                DrawCircleV(entity->character.position, PLAYER_RADIUS, PURPLE);
                // Draw HP Bar
                f32 hpPercent = entity->character.health / entity->character.maxHealth;
                DrawRectangle(entity->character.position.x - 20, entity->character.position.y - 30, 40, 5, DARKGRAY);
                DrawRectangle(entity->character.position.x - 20, entity->character.position.y - 30, (int)(40 * hpPercent), 5, GREEN);
                DrawText("ENEMY", entity->character.position.x - 20, entity->character.position.y - 45, 10, PURPLE);
            }
            break;
        case ENTITY_PROJECTILE:
            if (entity->projectile.type == PROJECTILE_FIREBALL) {
                DrawCircleV(entity->projectile.position, 10, ORANGE);
                DrawCircleV(entity->projectile.position, 6, YELLOW);
            } else if (entity->projectile.type == PROJECTILE_CRYSTAL) {
                DrawCircleV(entity->projectile.position, 8, SKYBLUE);
                DrawCircleV(entity->projectile.position, 4, WHITE);
            } else if (entity->projectile.type == PROJECTILE_BOMB) {
                DrawCircleV(entity->projectile.position, 12, BLACK);
                DrawCircleV(entity->projectile.position, 6, RED);
                // Draw explosion radius preview if about to explode
                if (entity->projectile.lifetime < 1.0f) {
                    DrawCircleLinesV(entity->projectile.position, BOMB_RADIUS, Fade(RED, 0.5f));
                }
            } else if (entity->projectile.type == PROJECTILE_SPIKE) {
                DrawTriangle((Vector2){entity->projectile.position.x, entity->projectile.position.y - 20},
                             (Vector2){entity->projectile.position.x - 15, entity->projectile.position.y + 10},
                             (Vector2){entity->projectile.position.x + 15, entity->projectile.position.y + 10}, BROWN);
                DrawTriangle((Vector2){entity->projectile.position.x, entity->projectile.position.y - 25},
                             (Vector2){entity->projectile.position.x - 10, entity->projectile.position.y + 5},
                             (Vector2){entity->projectile.position.x + 10, entity->projectile.position.y + 5}, GRAY);
            } else if (entity->projectile.type == PROJECTILE_EXPLOSION) {
                f32 scale = entity->projectile.lifetime / 0.5f; // lifetime goes from 0.5 to 0
                f32 radius = entity->projectile.radius;
                if (radius <= 0) radius = BOMB_RADIUS; // Fallback
                
                DrawCircleV(entity->projectile.position, radius * (1.0f - scale), Fade(ORANGE, scale));
                DrawCircleV(entity->projectile.position, (radius * 0.6f) * (1.0f - scale), Fade(YELLOW, scale));
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
