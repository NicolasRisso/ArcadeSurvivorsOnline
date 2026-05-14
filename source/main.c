#include "main.h"
#include "connection/connection.h"

// --- Global Variables ---
GlobalVariables globalVariables = { 0 };
InputState currentInputState = { 0 };
ConnectionState currentConnectionState = { 0 };

f32 playerXP = 0.0f;
f32 xpToNextLevel = 100.0f;
u16 playerLevel = 1;

void DrawXPBar(void);

// --- Level Up / Weapon System State ---
bool isChoosingUpgrade = false;
LevelUpOption upgradeOptions[3];
int pendingLevels = 0;

void Weapon_Initialize(Weapon* w, WeaponType type);
void Weapon_Upgrade(Weapon* w);
void GenerateUpgradeOptions(LevelUpOption options[3]);
void ApplyUpgrade(int optionIndex);
void DrawUpgradeCards(void);

// --- Main Entry Point ---
int main(void) {
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Arcade Survivors Online");

    if (!Network_InitConnection(&currentConnectionState)) {
        CloseWindow();
        return 1;
    }

    // Initialize player weapons - Start with 1 random weapon
    for (int i = 0; i < 5; i++) {
        globalVariables.playerWeapons[i].type = WEAPON_UNDEFINED;
    }
    
    // Use current time as seed for randomness
    srand(time(NULL));
    WeaponType startingType = (WeaponType)((rand() % 5) + 1);
    Weapon_Initialize(&globalVariables.playerWeapons[0], startingType);

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
                
                entity->character.position = Vector2Lerp(entity->character.position, entity->character.targetPosition, 0.25f);
            }
        }

        Enemy_UpdateMovement(deltaTime);
        Player_UpdateMovement(deltaTime);
        Weapons_Update(deltaTime);
        Projectile_UpdateMovement(deltaTime);
        Network_SendDeathReport(&currentConnectionState);
        Network_SendDamageBatch(&currentConnectionState);

        // Update XP Crystals (Magnetization and Collection)
        for (i32 entityIndex = MAX_PLAYERS + MAX_ENEMIES; entityIndex < MAX_REMOTE_ENTITIES; entityIndex++) {
            Entity* entity = &currentConnectionState.remoteEntities[entityIndex];
            if (entity->entityType == ENTITY_XP_CRYSTAL) {
                float dist = Vector2Distance(entity->xpCrystal.position, currentConnectionState.localPosition);
                
                if (entity->xpCrystal.isMagnetized || dist < MAGNET_RADIUS) {
                    if (!entity->xpCrystal.isMagnetized) {
                        entity->xpCrystal.isMagnetized = true;
                        entity->xpCrystal.magnetizedTimer = 0.0f;
                    }

                    entity->xpCrystal.magnetizedTimer += deltaTime;
                    float alpha = entity->xpCrystal.magnetizedTimer / 1.0f;
                    if (alpha > 1.0f) alpha = 1.0f;
                    float easedAlpha = alpha * alpha; // Ease-in

                    // Ease-in movement towards player
                    Vector2 dir = Vector2Normalize(Vector2Subtract(currentConnectionState.localPosition, entity->xpCrystal.position));
                    float speed = easedAlpha * (PLAYER_SPEED * 1.5f);
                    entity->xpCrystal.position.x += dir.x * speed * deltaTime;
                    entity->xpCrystal.position.y += dir.y * speed * deltaTime;
                }
                
                if (dist < COLLECT_RADIUS) {
                    // Collect!
                    playerXP += entity->xpCrystal.xpValue;
                    if (playerXP >= xpToNextLevel) {
                        playerLevel++;
                        playerXP -= xpToNextLevel;
                        xpToNextLevel *= 1.2f; // Increase difficulty
                        pendingLevels++;
                    }
                    Network_SendXPCollect(&currentConnectionState, entityIndex);
                    entity->entityType = ENTITY_UNDEFINED; // Remove locally immediately
                }
            }
        }

        // Trigger Upgrade Menu if pending levels
        if (pendingLevels > 0 && !isChoosingUpgrade) {
            GenerateUpgradeOptions(upgradeOptions);
            isChoosingUpgrade = true;
            pendingLevels--;
        }

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
            DrawXPBar();
            if (isChoosingUpgrade) DrawUpgradeCards();
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
    if (!currentConnectionState.isConnected || isChoosingUpgrade) return;

    for (i32 i = 0; i < 5; i++) {
        Weapon* weapon = &globalVariables.playerWeapons[i];
        if (weapon->type == WEAPON_UNDEFINED) continue;
        
        weapon->cooldownTimer -= deltaTime;
        if (weapon->cooldownTimer <= 0) {
            f32 dmg = weapon->stats.damage;
            f32 rad = weapon->stats.size;
            i32 extra = 0;
            
            if (weapon->type == WEAPON_CRYSTAL_STAFF) extra = weapon->stats.spec.crystalStaff.projectileAmount;
            else if (weapon->type == WEAPON_FIREBALL_RING) rad = weapon->stats.spec.fireball.explosionSize;
            else if (weapon->type == WEAPON_NATURE_SPIKES) extra = weapon->stats.spec.natureSpikes.spikeAmount;
            
            Network_SendWeaponFire(&currentConnectionState, (u8)weapon->type, dmg, rad, extra);
            
            if (weapon->type == WEAPON_DEATH_AURA) {
                // Death Aura Logic: Check enemies in radius and deal damage
                int hitCount = 0;
                for (i32 enemyIndex = 0; enemyIndex < MAX_REMOTE_ENTITIES; enemyIndex++) {
                    Entity* enemy = &currentConnectionState.remoteEntities[enemyIndex];
                    if (enemy->entityType == ENTITY_CHARACTER && enemy->character.characterType == CHARACTER_ENEMY) {
                        if (CheckCollisionCircles(currentConnectionState.localPosition, weapon->stats.size, enemy->character.position, PLAYER_RADIUS)) {
                            Network_SendDamage(&currentConnectionState, enemyIndex, weapon->stats.damage);
                            enemy->character.health -= weapon->stats.damage; // Local Prediction
                            hitCount++;
                            if (hitCount >= 100) break;
                        }
                    }
                }
            }
            
            weapon->cooldownTimer = weapon->stats.attackSpeed;
        }
    }
}

void Projectile_UpdateMovement(f32 deltaTime) {
    for (i32 i = 0; i < MAX_REMOTE_ENTITIES; i++) {
        Entity* entity = &currentConnectionState.remoteEntities[i];
        if (entity->entityType == ENTITY_PROJECTILE) {
            Projectile* proj = &entity->proj;
            
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
                                        other->character.health -= 50.0f; // Local Prediction
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
                                remoteEntity->character.health -= 100.0f; // Local Prediction
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
                                other->character.health -= 500.0f; // Local Prediction
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
                                other->character.health -= 20.0f; // Local Prediction
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
    
    // Handle Weapon Selection if active
    if (isChoosingUpgrade) {
        if (IsKeyPressed(KEY_ONE)) { ApplyUpgrade(0); isChoosingUpgrade = false; }
        if (IsKeyPressed(KEY_TWO)) { ApplyUpgrade(1); isChoosingUpgrade = false; }
        if (IsKeyPressed(KEY_THREE)) { ApplyUpgrade(2); isChoosingUpgrade = false; }
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
            if (entity->proj.type == PROJECTILE_FIREBALL) {
                DrawCircleV(entity->proj.position, 10, ORANGE);
                DrawCircleV(entity->proj.position, 6, YELLOW);
            } else if (entity->proj.type == PROJECTILE_CRYSTAL) {
                DrawCircleV(entity->proj.position, 8, SKYBLUE);
                DrawCircleV(entity->proj.position, 4, WHITE);
            } else if (entity->proj.type == PROJECTILE_BOMB) {
                DrawCircleV(entity->proj.position, 12, BLACK);
                DrawCircleV(entity->proj.position, 6, RED);
                // Draw explosion radius preview if about to explode
                if (entity->proj.lifetime < 1.0f) {
                    DrawCircleLinesV(entity->proj.position, BOMB_RADIUS, Fade(RED, 0.5f));
                }
            } else if (entity->proj.type == PROJECTILE_SPIKE) {
                DrawTriangle((Vector2){entity->proj.position.x, entity->proj.position.y - 20},
                             (Vector2){entity->proj.position.x - 15, entity->proj.position.y + 10},
                             (Vector2){entity->proj.position.x + 15, entity->proj.position.y + 10}, BROWN);
                DrawTriangle((Vector2){entity->proj.position.x, entity->proj.position.y - 25},
                             (Vector2){entity->proj.position.x - 10, entity->proj.position.y + 5},
                             (Vector2){entity->proj.position.x + 10, entity->proj.position.y + 5}, GRAY);
            } else if (entity->proj.type == PROJECTILE_EXPLOSION) {
                f32 scale = entity->proj.lifetime / 0.5f; // lifetime goes from 0.5 to 0
                f32 radius = entity->proj.radius;
                if (radius <= 0) radius = BOMB_RADIUS; // Fallback
                
                DrawCircleV(entity->proj.position, radius * (1.0f - scale), Fade(ORANGE, scale));
                DrawCircleV(entity->proj.position, (radius * 0.6f) * (1.0f - scale), Fade(YELLOW, scale));
            }
            break;
        case ENTITY_XP_CRYSTAL:
            DrawPoly(entity->xpCrystal.position, 4, 12, 45, SKYBLUE);
            DrawPoly(entity->xpCrystal.position, 4, 8, 45, BLUE);
            DrawPoly(entity->xpCrystal.position, 4, 4, 45, WHITE);
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

void DrawXPBar(void) {
    float barWidth = SCREEN_WIDTH * 0.8f;
    float barHeight = 20.0f;
    float x = (SCREEN_WIDTH - barWidth) / 2.0f;
    float y = 20.0f;
    
    float progress = (float)playerXP / xpToNextLevel;
    
    DrawRectangle(x, y, barWidth, barHeight, Fade(DARKBLUE, 0.5f));
    DrawRectangle(x, y, barWidth * progress, barHeight, SKYBLUE);
    DrawRectangleLines(x, y, barWidth, barHeight, WHITE);
    
    DrawText(TextFormat("LV %d", playerLevel), (int)x, (int)(y + barHeight + 5), 20, WHITE);
}

// --- Weapon System Implementation ---
void Weapon_Initialize(Weapon* w, WeaponType type) {
    w->type = type;
    w->level = 1;
    w->cooldownTimer = 0.0f;
    
    switch (type) {
        case WEAPON_CRYSTAL_STAFF:
            w->stats.damage = 100.0f;
            w->stats.attackSpeed = 1.5f;
            w->stats.spec.crystalStaff.pierce = 1;
            w->stats.spec.crystalStaff.projectileAmount = 1;
            break;
        case WEAPON_DEATH_AURA:
            w->stats.damage = 15.0f;
            w->stats.attackSpeed = 0.2f;
            w->stats.size = AURA_RADIUS;
            break;
        case WEAPON_FIREBALL_RING:
            w->stats.damage = 50.0f;
            w->stats.attackSpeed = 2.0f;
            w->stats.spec.fireball.explosionSize = FIREBALL_RADIUS;
            break;
        case WEAPON_NATURE_SPIKES:
            w->stats.damage = 20.0f;
            w->stats.attackSpeed = 2.5f;
            w->stats.spec.natureSpikes.damageCap = 150.0f;
            w->stats.spec.natureSpikes.spikeAmount = 3;
            w->stats.size = 1.0f;
            break;
        case WEAPON_BOMB_SHOES:
            w->stats.damage = 500.0f;
            w->stats.attackSpeed = 3.0f;
            w->stats.size = BOMB_RADIUS;
            break;
        case WEAPON_UNDEFINED: break;
    }
}

void Weapon_Upgrade(Weapon* w) {
    if (w->level >= 15) return;
    w->level++;
    
    // Scale stats
    w->stats.damage *= 1.2f;
    
    switch (w->type) {
        case WEAPON_CRYSTAL_STAFF:
            if (w->level % 3 == 0) w->stats.spec.crystalStaff.projectileAmount++;
            if (w->level % 4 == 0) w->stats.spec.crystalStaff.pierce++;
            w->stats.attackSpeed *= 0.95f;
            break;
        case WEAPON_DEATH_AURA:
            w->stats.size += 15.0f;
            w->stats.attackSpeed -= 0.007f; // Toward 0.1s
            if (w->stats.attackSpeed < 0.1f) w->stats.attackSpeed = 0.1f;
            break;
        case WEAPON_FIREBALL_RING:
            w->stats.spec.fireball.explosionSize += 10.0f;
            w->stats.attackSpeed *= 0.95f;
            break;
        case WEAPON_NATURE_SPIKES:
            w->stats.spec.natureSpikes.spikeAmount++;
            w->stats.spec.natureSpikes.damageCap += 50.0f;
            w->stats.size += 0.1f;
            break;
        case WEAPON_BOMB_SHOES:
            w->stats.size += 20.0f;
            w->stats.attackSpeed *= 0.95f;
            break;
        case WEAPON_UNDEFINED: break;
    }
}

void GenerateUpgradeOptions(LevelUpOption options[3]) {
    WeaponType types[5] = { WEAPON_FIREBALL_RING, WEAPON_CRYSTAL_STAFF, WEAPON_DEATH_AURA, WEAPON_BOMB_SHOES, WEAPON_NATURE_SPIKES };
    const char* names[6] = { "", "Fireball", "Crystal Staff", "Death Aura", "Bomb Shoes", "Nature Spikes" };
    const char* descs[6] = { "", "Fiery explosions", "Piercing crystals", "Continuous damage aura", "Delayed explosions", "Spikes from the earth" };
    Color colors[6] = { WHITE, ORANGE, SKYBLUE, BLACK, RED, GREEN };

    // Randomize 3 different options
    for (int i = 0; i < 3; i++) {
        int idx = rand() % 5;
        options[i].type = types[idx];
        options[i].name = names[options[i].type];
        options[i].description = descs[options[i].type];
        options[i].color = colors[options[i].type];
    }
}

void ApplyUpgrade(int optionIndex) {
    WeaponType type = upgradeOptions[optionIndex].type;
    
    // Check if we already have it
    for (int i = 0; i < 5; i++) {
        if (globalVariables.playerWeapons[i].type == type) {
            Weapon_Upgrade(&globalVariables.playerWeapons[i]);
            return;
        }
    }
    
    // Find empty slot
    for (int i = 0; i < 5; i++) {
        if (globalVariables.playerWeapons[i].type == WEAPON_UNDEFINED) {
            Weapon_Initialize(&globalVariables.playerWeapons[i], type);
            return;
        }
    }
}

void DrawUpgradeCards(void) {
    float cardWidth = 250.0f;
    float cardHeight = 350.0f;
    float spacing = 40.0f;
    float startX = (SCREEN_WIDTH - (cardWidth * 3 + spacing * 2)) / 2.0f;
    float startY = SCREEN_HEIGHT - cardHeight - 50.0f;
    
    for (int i = 0; i < 3; i++) {
        Rectangle card = { startX + i * (cardWidth + spacing), startY, cardWidth, cardHeight };
        
        // Draw card background
        DrawRectangleRec(card, Fade(DARKGRAY, 0.9f));
        DrawRectangleLinesEx(card, 2, WHITE);
        
        // Draw Header
        DrawText(TextFormat("[%d]", i + 1), card.x + 10, card.y + 10, 20, YELLOW);
        DrawText(upgradeOptions[i].name, card.x + 50, card.y + 10, 20, WHITE);
        
        // Find current level if owned
        int currentLv = 0;
        for (int j = 0; j < 5; j++) {
            if (globalVariables.playerWeapons[j].type == upgradeOptions[i].type) {
                currentLv = globalVariables.playerWeapons[j].level;
                break;
            }
        }
        
        if (currentLv > 0) {
            DrawText(TextFormat("Level %d -> %d", currentLv, currentLv + 1), card.x + 10, card.y + 40, 16, GREEN);
        } else {
            DrawText("NEW WEAPON", card.x + 10, card.y + 40, 16, SKYBLUE);
        }
        
        // Draw Image Placeholder
        DrawRectangle(card.x + 50, card.y + 80, cardWidth - 100, 150, upgradeOptions[i].color);
        DrawRectangleLines(card.x + 50, card.y + 80, cardWidth - 100, 150, WHITE);
        
        // Draw Description
        DrawText(upgradeOptions[i].description, card.x + 10, card.y + 250, 14, GRAY);
    }
}
