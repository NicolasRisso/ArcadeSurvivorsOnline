#include "main.h"
#include "connection/connection.h"

// --- Global Variables ---
GlobalVariables globalVariables = {
    .currentGameState = STATE_MAIN_MENU,
    .currentInGameState = IN_GAME_PLAYING,
    .playerNames = {
        "Player 1",
        "Player 2",
        "Player 3",
        "Player 4"
    },
    .myNameInput = "Survivor",
    .joinIpAddress = "127.0.0.1",
    .playerXP = 0.0f,
    .xpToNextLevel = 100.0f,
    .playerLevel = 1,
    .gameTime = 0.0f,
    .spectatedPlayerID = 0,
    .isChoosingUpgrade = false,
    .pendingLevels = 0,
    .particlesInitialized = false
};

// --- Main Entry Point ---
int main(void) {
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Arcade Survivors Online");

    // Initialize player weapons and relics - Start with 1 random weapon
    for (u8 i = 0; i < 4; i++) {
        globalVariables.playerWeapons[i].type = WEAPON_UNDEFINED;
        globalVariables.playerRelics[i].type = RELIC_UNDEFINED;
        globalVariables.playerRelics[i].level = 0;
    }

    // Use current time as seed for randomness
    srand(time(NULL));
    WeaponType startingType = (WeaponType)((rand() % 5) + 1);
    Weapon_Initialize(&globalVariables.playerWeapons[0], startingType);

    Camera2D camera = { 0 };
    camera.target = globalVariables.currentConnectionState.localPosition;
    camera.offset = (Vector2){ SCREEN_WIDTH / 2.0f, SCREEN_HEIGHT / 2.0f };
    camera.rotation = 0.0f;
    camera.zoom = 1.0f;

    SetTargetFPS(TARGET_FPS);

    while (!globalVariables.currentInputState.quitApplication) {
        if (globalVariables.currentGameState == STATE_LOBBY || globalVariables.currentGameState == STATE_IN_GAME) {
            Network_UpdateConnection(&globalVariables.currentConnectionState);
        }
        Input_Update(&globalVariables.currentInputState);

        f32 deltaTime = GetFrameTime();
        Vector2 mousePos = GetMousePosition();
        
        if (globalVariables.currentGameState == STATE_IN_GAME) {
            if (globalVariables.currentConnectionState.isConnected) {
                globalVariables.gameTime += deltaTime;
            } else {
                globalVariables.gameTime = 0.0f;
            }

            // Update active notification
            if (globalVariables.currentConnectionState.notificationCount > 0) {
                ClientNotification* activeNotif = &globalVariables.currentConnectionState.notificationQueue[0];
                activeNotif->timeElapsed += deltaTime;
                if (activeNotif->timeElapsed >= activeNotif->duration) {
                    // Shift notifications queue forward
                    for (u8 i = 0; i < globalVariables.currentConnectionState.notificationCount - 1; i++) {
                        globalVariables.currentConnectionState.notificationQueue[i] = globalVariables.currentConnectionState.notificationQueue[i + 1];
                    }
                    globalVariables.currentConnectionState.notificationCount--;
                    if (globalVariables.currentConnectionState.notificationCount > 0) {
                        globalVariables.currentConnectionState.notificationQueue[0].timeElapsed = 0.0f;
                    }
                }
            }

            // Predict and Interpolate movement and count down visual timers for characters
            for (u16 entityIndex = 0; entityIndex < MAX_REMOTE_ENTITIES; entityIndex++) {
                Entity* entity = &globalVariables.currentConnectionState.remoteEntities[entityIndex];
                if (entity->entityType == ENTITY_CHARACTER) {
                    entity->character.position.x += entity->character.velocity.x * deltaTime;
                    entity->character.position.y += entity->character.velocity.y * deltaTime;
                    
                    entity->character.targetPosition.x += entity->character.velocity.x * deltaTime;
                    entity->character.targetPosition.y += entity->character.velocity.y * deltaTime;
                    
                    entity->character.position = Vector2Lerp(entity->character.position, entity->character.targetPosition, 0.25f);
                    
                    if (entity->character.damageFlashTimer > 0) {
                        entity->character.damageFlashTimer -= deltaTime;
                        if (entity->character.damageFlashTimer < 0) entity->character.damageFlashTimer = 0;
                    }
                }
            }
            
            // Count down local player visual & invulnerability timers
            if (globalVariables.currentConnectionState.damageFlashTimer > 0) {
                globalVariables.currentConnectionState.damageFlashTimer -= deltaTime;
                if (globalVariables.currentConnectionState.damageFlashTimer < 0) globalVariables.currentConnectionState.damageFlashTimer = 0;
            }
            if (globalVariables.currentConnectionState.iframeTimer > 0) {
                globalVariables.currentConnectionState.iframeTimer -= deltaTime;
                if (globalVariables.currentConnectionState.iframeTimer < 0) globalVariables.currentConnectionState.iframeTimer = 0;
            }
            if (globalVariables.currentConnectionState.isConnected) {
                globalVariables.currentConnectionState.gameTime += deltaTime;
            }

            Enemy_UpdateMovement(deltaTime);
            Player_UpdateMovement(deltaTime);
            Weapons_Update(deltaTime);
            Weapon_ProjectileUpdateMovement(deltaTime);
            Network_SendDeathReport(&globalVariables.currentConnectionState);
            Network_SendDamageBatch(&globalVariables.currentConnectionState);

            // Update XP Crystals (Magnetization and Collection)
            if (globalVariables.currentConnectionState.health > 0.0f && globalVariables.currentInGameState != IN_GAME_SPECTATING) {
                for (u16 entityIndex = MAX_PLAYERS + MAX_ENEMIES; entityIndex < MAX_REMOTE_ENTITIES; entityIndex++) {
                    Entity* entity = &globalVariables.currentConnectionState.remoteEntities[entityIndex];
                    if (entity->entityType == ENTITY_XP_CRYSTAL) {
                        f32 dist = Vector2Distance(entity->xpCrystal.position, globalVariables.currentConnectionState.localPosition);
                        
                        if (entity->xpCrystal.isMagnetized || dist < MAGNET_RADIUS) {
                            if (!entity->xpCrystal.isMagnetized) {
                                entity->xpCrystal.isMagnetized = true;
                                entity->xpCrystal.magnetizedTimer = 0.0f;
                            }

                            entity->xpCrystal.magnetizedTimer += deltaTime;
                            f32 alpha = entity->xpCrystal.magnetizedTimer / 1.0f;
                            if (alpha > 1.0f) alpha = 1.0f;
                            f32 easedAlpha = alpha * alpha; // Ease-in

                            // Ease-in movement towards player
                            Vector2 dir = Vector2Normalize(Vector2Subtract(globalVariables.currentConnectionState.localPosition, entity->xpCrystal.position));
                            f32 speed = easedAlpha * (PLAYER_SPEED * 1.5f);
                            entity->xpCrystal.position.x += dir.x * speed * deltaTime;
                            entity->xpCrystal.position.y += dir.y * speed * deltaTime;
                        }
                        
                        if (dist < COLLECT_RADIUS) {
                            // Collect!
                            u32 localIndex = (globalVariables.currentConnectionState.localPlayerIdentification - 1) % MAX_REMOTE_PLAYERS;
                            PlayerAttributes* attr = &globalVariables.currentConnectionState.playerAttributes[localIndex];
                            
                            globalVariables.playerXP += entity->xpCrystal.xpValue * attr->xpGained;
                            if (globalVariables.playerXP >= globalVariables.xpToNextLevel) {
                                globalVariables.playerLevel++;
                                globalVariables.playerXP -= globalVariables.xpToNextLevel;
                                globalVariables.xpToNextLevel *= 1.2f; // Increase difficulty
                                globalVariables.pendingLevels++;
                            }
                            Network_SendXPCollect(&globalVariables.currentConnectionState, entityIndex);
                            entity->entityType = ENTITY_UNDEFINED; // Remove locally immediately
                        }
                    }
                }
            }

            // Trigger Upgrade Menu if pending levels
            if (globalVariables.pendingLevels > 0 && !globalVariables.isChoosingUpgrade && globalVariables.currentInGameState != IN_GAME_SPECTATING) {
                Weapon_GenerateUpgradeOptions(globalVariables.upgradeOptions);
                globalVariables.isChoosingUpgrade = true;
                globalVariables.pendingLevels--;
            }

            bool isSpectating = (globalVariables.currentConnectionState.health <= 0.0f && globalVariables.currentConnectionState.teamLives <= 0);
            if (isSpectating) {
                globalVariables.currentInGameState = IN_GAME_SPECTATING;
                globalVariables.isChoosingUpgrade = false;
                globalVariables.pendingLevels = 0;
            } else {
                globalVariables.currentInGameState = IN_GAME_PLAYING;
            }

            if (globalVariables.currentInGameState == IN_GAME_SPECTATING) {
                bool currentSpectatedIsAlive = false;
                if (globalVariables.spectatedPlayerID == globalVariables.currentConnectionState.localPlayerIdentification) {
                    if (globalVariables.currentConnectionState.health > 0.0f) currentSpectatedIsAlive = true;
                } else {
                    Entity* ent = &globalVariables.currentConnectionState.remoteEntities[globalVariables.spectatedPlayerID];
                    if (ent->entityType == ENTITY_CHARACTER &&
                        ent->character.characterType == CHARACTER_PLAYER &&
                        ent->character.health > 0.0f) {
                        currentSpectatedIsAlive = true;
                    }
                }

                if (!currentSpectatedIsAlive || globalVariables.spectatedPlayerID == 0) {
                    globalVariables.spectatedPlayerID = Player_FindNextAlivePlayer(globalVariables.currentConnectionState.localPlayerIdentification, true);
                }

                Vector2 targetPos = globalVariables.currentConnectionState.localPosition;
                if (globalVariables.spectatedPlayerID == globalVariables.currentConnectionState.localPlayerIdentification) {
                    targetPos = globalVariables.currentConnectionState.localPosition;
                } else {
                    Entity* ent = &globalVariables.currentConnectionState.remoteEntities[globalVariables.spectatedPlayerID];
                    if (ent->entityType == ENTITY_CHARACTER && ent->character.characterType == CHARACTER_PLAYER) {
                        targetPos = ent->character.position;
                    }
                }
                camera.target = targetPos;
            } else {
                camera.target = globalVariables.currentConnectionState.localPosition;
                globalVariables.spectatedPlayerID = globalVariables.currentConnectionState.localPlayerIdentification;
            }
        }

        BeginDrawing();
            if (globalVariables.currentGameState == STATE_IN_GAME) {
                ClearBackground(DARKGRAY);
                BeginMode2D(camera);
                    Render_Map();

                    // Update and Render Local Visual Effects
                    f32 frameDelta = GetFrameTime();
                    for (u8 i = 0; i < 128; i++) {
                        if (globalVariables.currentConnectionState.localVisualEffects[i].active) {
                            globalVariables.currentConnectionState.localVisualEffects[i].lifetime -= frameDelta;
                            if (globalVariables.currentConnectionState.localVisualEffects[i].lifetime <= 0) {
                                globalVariables.currentConnectionState.localVisualEffects[i].active = false;
                            } else {
                                f32 scale = globalVariables.currentConnectionState.localVisualEffects[i].lifetime / 0.5f;
                                DrawCircleV(globalVariables.currentConnectionState.localVisualEffects[i].position, globalVariables.currentConnectionState.localVisualEffects[i].radius * (1.0f - scale), Fade(ORANGE, scale));
                                DrawCircleV(globalVariables.currentConnectionState.localVisualEffects[i].position, (globalVariables.currentConnectionState.localVisualEffects[i].radius * 0.6f) * (1.0f - scale), Fade(YELLOW, scale));
                            }
                        }
                    }

                    // Update and Render Local Damage Popups
                    for (u16 i = 0; i < 256; i++) {
                        Entity* popupEnt = &globalVariables.currentConnectionState.localDamagePopups[i];
                        if (popupEnt->entityType == ENTITY_DAMAGE_POPUP) {
                            DamagePopup* popup = &popupEnt->damagePopup;
                            popup->lifetime += frameDelta;
                            if (popup->lifetime >= 0.7f) {
                                popupEnt->entityType = ENTITY_UNDEFINED;
                            } else {
                                popup->position.y -= frameDelta * 40.0f;
                                
                                f32 t = popup->lifetime;
                                f32 scale = 1.0f;
                                f32 alpha = 1.0f;
                                if (t <= 0.2f) {
                                    scale = 1.0f + (t / 0.2f) * 0.2f;
                                    alpha = 1.0f;
                                } else {
                                    scale = 1.2f * (1.0f - (t - 0.2f) / 0.5f);
                                    alpha = 1.0f - (t - 0.2f) / 0.5f;
                                }
                                
                                if (scale < 0.0f) scale = 0.0f;
                                if (alpha < 0.0f) alpha = 0.0f;
                                if (alpha > 1.0f) alpha = 1.0f;
                                
                                int baseFontSize = 18;
                                int currentFontSize = (int)(baseFontSize * scale);
                                if (currentFontSize > 0) {
                                    const char* dmgText = TextFormat("%.0f", popup->damageValue);
                                    int textWidth = MeasureText(dmgText, currentFontSize);
                                    DrawText(dmgText, (int)(popup->position.x - textWidth / 2), (int)(popup->position.y - currentFontSize / 2), currentFontSize, Fade(popup->color, alpha));
                                }
                            }
                        }
                    }

                    for (u16 entityIndex = 0; entityIndex < MAX_REMOTE_ENTITIES; entityIndex++) {
                        const Entity* entity = &globalVariables.currentConnectionState.remoteEntities[entityIndex];
                        if (entity->entityType == ENTITY_CHARACTER && entity->character.characterType == CHARACTER_PLAYER) {
                            // Draw Death Aura for remote players if alive
                            if (entity->character.health > 0.0f && (entity->character.weaponsMask & (1 << (WEAPON_DEATH_AURA - 1)))) {
                                u32 remoteID = entityIndex;
                                u32 remoteIndex = (remoteID - 1) % MAX_REMOTE_PLAYERS;
                                f32 remoteSizeMult = globalVariables.currentConnectionState.playerAttributes[remoteIndex].size;
                                u8 auraLevel = entity->character.weaponLevels[WEAPON_DEATH_AURA - 1];
                                f32 baseAuraRadius = AURA_RADIUS;
                                if (auraLevel > 1) {
                                    baseAuraRadius += 15.0f * (auraLevel - 1);
                                }
                                f32 currentAuraRadius = baseAuraRadius * remoteSizeMult;
                                DrawCircleLinesV(entity->character.position, currentAuraRadius, Fade(BLACK, 0.3f));
                                DrawCircleV(entity->character.position, currentAuraRadius, Fade(BLACK, 0.1f));
                            }
                        }
                        Render_Entity(entity);
                    }

                    if (globalVariables.currentConnectionState.isConnected) {
                        u32 localID = globalVariables.currentConnectionState.localPlayerIdentification;
                        u32 idx = (localID - 1) % MAX_PLAYERS;
                        if (globalVariables.currentConnectionState.health <= 0.0f) {
                            Render_DrawTombstone(globalVariables.currentConnectionState.localPosition, globalVariables.playerNames[idx], BLUE);
                        } else {
                            DrawCircleV(globalVariables.currentConnectionState.localPosition, PLAYER_RADIUS, BLUE);
                            
                            // Draw local player high-frequency pulse damage flash if timer is active
                            if (globalVariables.currentConnectionState.damageFlashTimer > 0) {
                                f32 flashAlpha = (sinf(globalVariables.currentConnectionState.damageFlashTimer * 75.0f) > 0.0f) ? 0.7f : 0.0f;
                                if (flashAlpha > 0.0f) {
                                    DrawCircleV(globalVariables.currentConnectionState.localPosition, PLAYER_RADIUS, Fade(WHITE, flashAlpha));
                                }
                            }
                            
                            int textWidth = MeasureText(globalVariables.playerNames[idx], 12);
                            DrawText(globalVariables.playerNames[idx], globalVariables.currentConnectionState.localPosition.x - textWidth / 2, globalVariables.currentConnectionState.localPosition.y - 40, 12, BLUE);
                            
                            // Draw local player's death aura
                            u32 localIndex = (globalVariables.currentConnectionState.localPlayerIdentification - 1) % MAX_REMOTE_PLAYERS;
                            PlayerAttributes* attr = &globalVariables.currentConnectionState.playerAttributes[localIndex];
                            for (u8 i = 0; i < 4; i++) {
                                if (globalVariables.playerWeapons[i].type == WEAPON_DEATH_AURA) {
                                    f32 currentAuraRadius = globalVariables.playerWeapons[i].stats.size * attr->size;
                                    DrawCircleLinesV(globalVariables.currentConnectionState.localPosition, currentAuraRadius, Fade(BLACK, 0.3f));
                                    DrawCircleV(globalVariables.currentConnectionState.localPosition, currentAuraRadius, Fade(BLACK, 0.1f));
                                    break;
                                }
                            }
                        }
                    } else {
                        DrawText("Searching for server...", globalVariables.currentConnectionState.localPosition.x - 60, globalVariables.currentConnectionState.localPosition.y, 20, WHITE);
                    }
                EndMode2D();
                
                // UI Overlay
                Render_DrawXPBar();
                Render_DrawGameTimer();
                if (globalVariables.isChoosingUpgrade) Render_DrawUpgradeCards();
                if (IsKeyDown(KEY_TAB)) Render_DrawStatsOverlay();

                // Draw glassmorphic spectator panel at bottom center if spectating
                if (globalVariables.currentInGameState == IN_GAME_SPECTATING) {
                    float panelWidth = 400.0f;
                    float panelHeight = 80.0f;
                    float panelX = (SCREEN_WIDTH - panelWidth) / 2.0f;
                    float panelY = SCREEN_HEIGHT - panelHeight - 40.0f;
                    
                    // Glassmorphic background
                    DrawRectangleRounded((Rectangle){ panelX, panelY, panelWidth, panelHeight }, 0.15f, 4, Fade(BLACK, 0.6f));
                    DrawRectangleRoundedLines((Rectangle){ panelX, panelY, panelWidth, panelHeight }, 0.15f, 4, Fade(WHITE, 0.2f));
                    
                    // Draw a red "SPECTATING" pulsing tag
                    float pulse = 0.5f + 0.5f * sinf(GetTime() * 4.0f);
                    DrawText("SPECTATING", panelX + (panelWidth - MeasureText("SPECTATING", 14)) / 2.0f, panelY + 12, 14, Fade(RED, 0.7f + 0.3f * pulse));
                    
                    // Draw the current player spectated name
                    u32 sIdx = (globalVariables.spectatedPlayerID - 1) % MAX_PLAYERS;
                    const char* targetName = globalVariables.playerNames[sIdx];
                    if (globalVariables.spectatedPlayerID == globalVariables.currentConnectionState.localPlayerIdentification) {
                        targetName = TextFormat("%s (YOU)", globalVariables.playerNames[sIdx]);
                    }
                    int nameSize = 22;
                    DrawText(targetName, panelX + (panelWidth - MeasureText(targetName, nameSize)) / 2.0f, panelY + 32, nameSize, WHITE);
                    
                    // Draw left and right cycling arrows and keys
                    DrawText("< A", panelX + 30, panelY + 35, 16, GRAY);
                    DrawText("D >", panelX + panelWidth - 30 - MeasureText("D >", 16), panelY + 35, 16, GRAY);
                    
                    DrawText("Press [TAB] to view stats", panelX + (panelWidth - MeasureText("Press [TAB] to view stats", 10)) / 2.0f, panelY + 60, 10, LIGHTGRAY);
                }

                DrawFPS(10, 10);

                // Draw active notification
                if (globalVariables.currentConnectionState.notificationCount > 0) {
                    ClientNotification* activeNotif = &globalVariables.currentConnectionState.notificationQueue[0];
                    f32 flashSpeed = activeNotif->flashDuration;
                    f32 alpha = 1.0f;
                    if (flashSpeed > 0.001f) {
                        f32 progress = activeNotif->timeElapsed / flashSpeed;
                        f32 angle = progress * 2.0f * 3.14159265f - (3.14159265f / 2.0f);
                        alpha = 0.5f + 0.5f * sinf(angle);
                    }
                    
                    int fontSize = 36;
                    int textWidth = MeasureText(activeNotif->message, fontSize);
                    int posX = (SCREEN_WIDTH - textWidth) / 2;
                    int posY = 150;
                    
                    DrawRectangle(0, posY - 10, SCREEN_WIDTH, fontSize + 20, Fade(BLACK, alpha * 0.4f));
                    DrawText(activeNotif->message, posX + 2, posY + 2, fontSize, Fade(BLACK, alpha * 0.6f));
                    DrawText(activeNotif->message, posX, posY, fontSize, Fade(activeNotif->color, alpha));
                }
                
                if (!globalVariables.currentConnectionState.isConnected) {
                    DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(BLACK, 0.6f));
                    DrawText("CONNECTING TO SERVER...", SCREEN_WIDTH/2 - 150, SCREEN_HEIGHT/2, 20, WHITE);
                    DrawText(TextFormat("Target IP: %s:%d", globalVariables.joinIpAddress, SERVER_PORT), SCREEN_WIDTH/2 - 100, SCREEN_HEIGHT/2 + 40, 10, GRAY);
                } else {
                    DrawText("CONNECTED", 10, 30, 20, GREEN);
                    
                    // Draw local health bar
                    f32 healthPercent = globalVariables.currentConnectionState.health / globalVariables.currentConnectionState.maxHealth;
                    if (healthPercent < 0.0f) healthPercent = 0.0f;
                    DrawRectangle(10, 60, 200, 20, DARKGRAY);
                    DrawRectangle(10, 60, (int)(200 * healthPercent), 20, RED);
                    DrawRectangleLines(10, 60, 200, 20, BLACK);
                    DrawText(TextFormat("%.0f / %.0f", globalVariables.currentConnectionState.health, globalVariables.currentConnectionState.maxHealth), 70, 65, 10, WHITE);
                    
                    // Draw lives counter next to the health bar
                    Render_DrawHeart((Vector2){ 235, 70 }, 10, RED);
                    DrawText(TextFormat("x %d", globalVariables.currentConnectionState.teamLives), 255, 62, 16, WHITE);
                }
            } else {
                ClearBackground((Color){ 10, 12, 18, 255 });
                Render_UpdateAndDrawMenuParticles(deltaTime);
                
                if (globalVariables.currentGameState == STATE_MAIN_MENU) {
                    Render_DrawMainMenu(mousePos, deltaTime);
                } else if (globalVariables.currentGameState == STATE_JOIN_IP) {
                    Render_DrawJoinInputScreen(mousePos, deltaTime);
                } else if (globalVariables.currentGameState == STATE_LOBBY) {
                    Render_DrawLobby(mousePos, deltaTime);
                }
                
                DrawFPS(10, 10);
            }
        EndDrawing();
    }

    Network_CloseConnection();
    CloseWindow();
    return 0;
}

//~ Begin of Enemy

u32 Enemy_GetAlternativeTargetPlayerID(u32 deadPlayerID, u32 enemyIndex) {
    u32 alivePlayers[4];
    u32 aliveCount = 0;
    
    // Check if local player is alive
    if (globalVariables.currentConnectionState.isConnected && globalVariables.currentConnectionState.health > 0.0f) {
        alivePlayers[aliveCount++] = globalVariables.currentConnectionState.localPlayerIdentification;
    }
    
    // Check if remote players are alive
    for (u8 i = 1; i <= 4; i++) {
        if (i == globalVariables.currentConnectionState.localPlayerIdentification) continue;
        
        Entity* remotePlayer = &globalVariables.currentConnectionState.remoteEntities[i];
        if (remotePlayer->entityType == ENTITY_CHARACTER && 
            remotePlayer->character.characterType == CHARACTER_PLAYER && 
            remotePlayer->character.health > 0.0f) {
            alivePlayers[aliveCount++] = i;
        }
    }
    
    // If no players are alive, return the original target
    if (aliveCount == 0) return deadPlayerID;
    
    // Select one of the alive players deterministically based on enemyIndex % aliveCount
    return alivePlayers[enemyIndex % aliveCount];
}

void Enemy_UpdateMovement(f32 deltaTime) {
    // Enum-indexed array lookup for base speed based on EnemyClass
    static const f32 ENEMY_BASE_SPEEDS[] = {
        [ENEMY_CLASS_NORMAL] = 150.0f,
        [ENEMY_CLASS_FAST]   = 225.0f,
        [ENEMY_CLASS_TANK]   = 45.0f,
        [ENEMY_CLASS_BOSS]   = 150.0f
    };

    for (u16 entityIndex = 0; entityIndex < MAX_REMOTE_ENTITIES; entityIndex++) {
        Entity* entity = &globalVariables.currentConnectionState.remoteEntities[entityIndex];
        if (entity->entityType == ENTITY_CHARACTER && entity->character.characterType == CHARACTER_ENEMY) {
            
            // 1. Calculate direction to target player
            Vector2 targetPosition = entity->character.position; // Default to staying put
            bool targetFound = false;

            if (entity->character.targetPlayerID != 0) {
                // If target player is dead, select alternative alive player deterministically
                u32 currentTargetID = entity->character.targetPlayerID;
                bool targetIsDead = false;
                
                if (currentTargetID == globalVariables.currentConnectionState.localPlayerIdentification) {
                    if (globalVariables.currentConnectionState.health <= 0.0f) {
                        targetIsDead = true;
                    }
                } else {
                    u32 targetIndex = currentTargetID % MAX_REMOTE_ENTITIES;
                    Entity* targetPlayer = &globalVariables.currentConnectionState.remoteEntities[targetIndex];
                    if (targetPlayer->entityType == ENTITY_CHARACTER && 
                        targetPlayer->character.characterType == CHARACTER_PLAYER && 
                        targetPlayer->character.health <= 0.0f) {
                        targetIsDead = true;
                    }
                }
                
                if (targetIsDead) {
                    entity->character.targetPlayerID = Enemy_GetAlternativeTargetPlayerID(currentTargetID, entityIndex);
                }

                if (globalVariables.currentConnectionState.localPlayerIdentification == entity->character.targetPlayerID) {
                    targetPosition = globalVariables.currentConnectionState.localPosition;
                    targetFound = true;
                } else {
                    // Look for remote player
                    u32 targetIndex = entity->character.targetPlayerID % MAX_REMOTE_ENTITIES;
                    Entity* targetPlayer = &globalVariables.currentConnectionState.remoteEntities[targetIndex];
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
            for (u16 otherIndex = 0; otherIndex < MAX_REMOTE_ENTITIES; otherIndex++) {
                if (entityIndex == otherIndex) continue;
                
                Entity* other = &globalVariables.currentConnectionState.remoteEntities[otherIndex];
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

            f32 baseSpeed = ENEMY_BASE_SPEEDS[entity->character.enemyClass];
            f32 speedMultiplier = 1.0f + (globalVariables.currentConnectionState.difficulty / 20.0f) * 1.05f;
            f32 actualSpeed = baseSpeed * speedMultiplier;

            entity->character.velocity.x = finalDirection.x * actualSpeed;
            entity->character.velocity.y = finalDirection.y * actualSpeed;

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

//~ End of Enemy

//~ Begin of Input

void Input_Update(InputState* state) {
    state->movementDirection = (Vector2){ 0, 0 };
    
    if (globalVariables.currentGameState == STATE_IN_GAME) {
        if (globalVariables.currentInGameState == IN_GAME_SPECTATING) {
            if (IsKeyPressed(KEY_A) || IsKeyPressed(KEY_LEFT)) {
                globalVariables.spectatedPlayerID = Player_FindNextAlivePlayer(globalVariables.spectatedPlayerID, false);
            }
            if (IsKeyPressed(KEY_D) || IsKeyPressed(KEY_RIGHT)) {
                globalVariables.spectatedPlayerID = Player_FindNextAlivePlayer(globalVariables.spectatedPlayerID, true);
            }
        } else {
            if (IsKeyDown(KEY_W)) state->movementDirection.y -= 1;
            if (IsKeyDown(KEY_S)) state->movementDirection.y += 1;
            if (IsKeyDown(KEY_A)) state->movementDirection.x -= 1;
            if (IsKeyDown(KEY_D)) state->movementDirection.x += 1;
            
            if (state->movementDirection.x != 0 || state->movementDirection.y != 0) {
                state->movementDirection = Vector2Normalize(state->movementDirection);
            }
        }
    }
    
    // Handle Selection if active
    if (globalVariables.isChoosingUpgrade) {
        if (IsKeyPressed(KEY_ONE) && globalVariables.upgradeOptions[0].type != 0) { Weapon_ApplyUpgrade(0); globalVariables.isChoosingUpgrade = false; }
        if (IsKeyPressed(KEY_TWO) && globalVariables.upgradeOptions[1].type != 0) { Weapon_ApplyUpgrade(1); globalVariables.isChoosingUpgrade = false; }
        if (IsKeyPressed(KEY_THREE) && globalVariables.upgradeOptions[2].type != 0) { Weapon_ApplyUpgrade(2); globalVariables.isChoosingUpgrade = false; }
    }

    state->quitApplication = WindowShouldClose();
}

//~ End of Input

//~ Begin of Player

void Player_ApplyLifesteal(ConnectionState* state, u32 enemyIndex, f32 damage, bool isAoE, f32 weaponMult) {
    if (state->iframeTimer > 0) return; // Cannot heal from lifesteal while iframed
    u32 localIndex = (state->localPlayerIdentification - 1) % MAX_REMOTE_PLAYERS;
    PlayerAttributes* attr = &state->playerAttributes[localIndex];
    if (attr->lifeSteal <= 0) return;
    
    Entity* enemy = &state->remoteEntities[enemyIndex % MAX_REMOTE_ENTITIES];
    if (enemy->entityType != ENTITY_CHARACTER) return;

    // Cap damage by enemy health
    f32 actualDamage = damage;
    if (actualDamage > enemy->character.health) actualDamage = enemy->character.health;
    if (actualDamage <= 0) return;
    
    f32 healing = actualDamage * attr->lifeSteal * weaponMult;
    if (isAoE) healing *= 0.40f;
    
    state->health += healing;
    if (state->health > state->maxHealth) state->health = state->maxHealth;
}

u32 Player_FindNextAlivePlayer(u32 currentSpectatedID, bool forward) {
    for (u8 step = 1; step <= 4; step++) {
        int nextID = currentSpectatedID + (forward ? step : -step);
        while (nextID < 1) nextID += 4;
        while (nextID > 4) nextID -= 4;

        if (nextID == globalVariables.currentConnectionState.localPlayerIdentification) {
            if (globalVariables.currentConnectionState.health > 0.0f) {
                return nextID;
            }
        } else {
            Entity* ent = &globalVariables.currentConnectionState.remoteEntities[nextID];
            if (ent->entityType == ENTITY_CHARACTER &&
                ent->character.characterType == CHARACTER_PLAYER &&
                ent->character.health > 0.0f) {
                return nextID;
            }
        }
    }
    return currentSpectatedID;
}

bool Player_IsConnected(i32 playerID) {
    if (!globalVariables.currentConnectionState.isConnected) return false;
    if ((i32)globalVariables.currentConnectionState.localPlayerIdentification == playerID) return true;
    if (playerID > 0 && playerID <= MAX_PLAYERS) {
        Entity* ent = &globalVariables.currentConnectionState.remoteEntities[playerID];
        if (ent->entityType == ENTITY_CHARACTER && ent->character.characterType == CHARACTER_PLAYER) {
            return true;
        }
    }
    return false;
}

void Player_RecalculateAttributes(void) {
    PlayerAttributes attr = {
        .maxHealth = DEFAULT_MAX_HEALTH,
        .damage = DEFAULT_DAMAGE,
        .attackSpeed = DEFAULT_ATTACK_SPEED,
        .movementSpeed = DEFAULT_MOVEMENT_SPEED,
        .size = DEFAULT_SIZE,
        .xpGained = DEFAULT_XP_GAINED,
        .lifeSteal = DEFAULT_LIFESTEAL
    };

    for (u8 i = 0; i < 4; i++) {
        Relic* relic = &globalVariables.playerRelics[i];
        if (relic->type == RELIC_UNDEFINED) continue;

        switch (relic->type) {
            case RELIC_HEALTH:
                attr.maxHealth += DEFAULT_MAX_HEALTH * RELIC_LEVELUP_HEALTH * relic->level;
                break;
            case RELIC_DAMAGE:
                attr.damage += RELIC_LEVELUP_DAMAGE * relic->level;
                break;
            case RELIC_ATTACK_SPEED:
                attr.attackSpeed += RELIC_LEVELUP_ATTACKSPEED * relic->level;
                break;
            case RELIC_SIZE:
                attr.size += RELIC_LEVELUP_SIZE * relic->level;
                break;
            case RELIC_MOVEMENT_SPEED:
                attr.movementSpeed += RELIC_LEVELUP_MOVEMENTSPEED * relic->level;
                break;
            case RELIC_XP_GAIN:
                attr.xpGained += RELIC_LEVELUP_XPGAIN * relic->level;
                break;
            case RELIC_LIFE_STEAL:
                attr.lifeSteal += RELIC_LEVELUP_LIFESTEAL * relic->level;
                break;
            default:
                break;
        }
    }

    // Proportional health adjustment when max health changes
    f32 healthDiff = attr.maxHealth - globalVariables.currentConnectionState.maxHealth;
    if (healthDiff != 0.0f) {
        globalVariables.currentConnectionState.health += healthDiff;
    }

    Player_UpdateAttributes(&globalVariables.currentConnectionState, attr);
}

void Player_UpdateAttributes(ConnectionState* state, PlayerAttributes attr) {
    u32 localIndex = 0;
    if (state->isConnected) {
        localIndex = (state->localPlayerIdentification - 1) % MAX_REMOTE_PLAYERS;
    }
    state->playerAttributes[localIndex] = attr;
    
    // Update local health values based on attribute change
    state->maxHealth = attr.maxHealth;
    if (state->health > state->maxHealth) state->health = state->maxHealth;
}

void Player_UpdateMovement(f32 deltaTime) {
    if (!Network_IsConnected(&globalVariables.currentConnectionState)) return;

    if (globalVariables.currentInGameState == IN_GAME_SPECTATING) {
        Network_SendVelocity(&globalVariables.currentConnectionState, (Vector2){ 0, 0 });
        return;
    }

    // Check for predicted local respawn when health <= 0
    if (globalVariables.currentConnectionState.health <= 0.0f) {
        if (globalVariables.currentConnectionState.teamLives > 0) {
            globalVariables.currentConnectionState.health = globalVariables.currentConnectionState.maxHealth;
            globalVariables.currentConnectionState.localPosition = (Vector2){ 0, 0 };
            globalVariables.currentConnectionState.iframeTimer = 2.0f; // 2 seconds spawn protection
            globalVariables.currentConnectionState.damageFlashTimer = 2.0f; // 2 seconds spawn protection visual damage flash
            
            // Predict life decrement
            globalVariables.currentConnectionState.teamLives--;

            // Notify server of respawn position
            Network_SendVelocity(&globalVariables.currentConnectionState, (Vector2){ 0, 0 });
            printf("[DEATH] Player died! Predicted local respawn at spawn (0, 0)...\n");
        } else {
            // Out of lives! Send zero velocity/position to server
            Network_SendVelocity(&globalVariables.currentConnectionState, (Vector2){ 0, 0 });
            return;
        }
    }

    u32 localIndex = (globalVariables.currentConnectionState.localPlayerIdentification - 1) % MAX_REMOTE_PLAYERS;
    PlayerAttributes* attr = &globalVariables.currentConnectionState.playerAttributes[localIndex];

    Vector2 movementVelocity = (Vector2){ 0, 0 };
    movementVelocity.x = globalVariables.currentInputState.movementDirection.x * PLAYER_SPEED * attr->movementSpeed;
    movementVelocity.y = globalVariables.currentInputState.movementDirection.y * PLAYER_SPEED * attr->movementSpeed;

    globalVariables.currentConnectionState.localPosition.x += movementVelocity.x * deltaTime;
    globalVariables.currentConnectionState.localPosition.y += movementVelocity.y * deltaTime;
    
    // Cap player position within map boundaries (including player radius)
    float mapLimit = MAP_SIZE / 2.0f - PLAYER_RADIUS;
    if (globalVariables.currentConnectionState.localPosition.x < -mapLimit) globalVariables.currentConnectionState.localPosition.x = -mapLimit;
    if (globalVariables.currentConnectionState.localPosition.x > mapLimit) globalVariables.currentConnectionState.localPosition.x = mapLimit;
    if (globalVariables.currentConnectionState.localPosition.y < -mapLimit) globalVariables.currentConnectionState.localPosition.y = -mapLimit;
    if (globalVariables.currentConnectionState.localPosition.y > mapLimit) globalVariables.currentConnectionState.localPosition.y = mapLimit;
    
    // Local player-enemy collision damage prediction
    if (globalVariables.currentConnectionState.iframeTimer <= 0) {
        for (u16 enemyIndex = 0; enemyIndex < MAX_REMOTE_ENTITIES; enemyIndex++) {
            Entity* enemy = &globalVariables.currentConnectionState.remoteEntities[enemyIndex];
            if (enemy->entityType == ENTITY_CHARACTER && 
                enemy->character.characterType == CHARACTER_ENEMY && 
                enemy->character.health > 0) {
                
                if (CheckCollisionCircles(globalVariables.currentConnectionState.localPosition, PLAYER_RADIUS, enemy->character.position, PLAYER_RADIUS)) {
                    f32 stat_mult = 1.0f + (globalVariables.currentConnectionState.difficulty / 20.0f) * 1.25f;
                    
                    f32 baseDamage = 10.0f;
                    if (enemy->character.enemyClass == ENEMY_CLASS_BOSS) {
                        baseDamage = 40.0f;
                    }
                    f32 predictedDamage = baseDamage * stat_mult;
                    
                    globalVariables.currentConnectionState.health -= predictedDamage;
                    globalVariables.currentConnectionState.iframeTimer = 0.5f;
                    globalVariables.currentConnectionState.damageFlashTimer = 0.5f;
                    
                    Network_SendDamage(&globalVariables.currentConnectionState, globalVariables.currentConnectionState.localPlayerIdentification, predictedDamage, WEAPON_UNDEFINED);
                    break;
                }
            }
        }
    }
    
    Network_SendVelocity(&globalVariables.currentConnectionState, movementVelocity);
}

//~ End of Player

//~ Begin of Renderer

bool Render_DrawCustomButton(Rectangle rect, const char* text, Color baseColor, Color hoverColor, Vector2 mousePos, f32 deltaTime, f32* animProgress) {
    bool hovered = CheckCollisionPointRec(mousePos, rect);
    
    float target = hovered ? 1.0f : 0.0f;
    *animProgress = Lerp(*animProgress, target, deltaTime * 12.0f);
    
    float scale = 1.0f + (*animProgress * 0.04f);
    float width = rect.width * scale;
    float height = rect.height * scale;
    float x = rect.x - (width - rect.width) / 2.0f;
    float y = rect.y - (height - rect.height) / 2.0f;
    
    unsigned char r = (unsigned char)Lerp(baseColor.r, hoverColor.r, *animProgress);
    unsigned char g = (unsigned char)Lerp(baseColor.g, hoverColor.g, *animProgress);
    unsigned char b = (unsigned char)Lerp(baseColor.b, hoverColor.b, *animProgress);
    unsigned char a = (unsigned char)Lerp(baseColor.a, hoverColor.a, *animProgress);
    Color currentColor = (Color){ r, g, b, a };
    
    DrawRectangleRounded((Rectangle){ x + 4, y + 4, width, height }, 0.2f, 4, Fade(BLACK, 0.25f));
    DrawRectangleRounded((Rectangle){ x, y, width, height }, 0.2f, 4, currentColor);
    
    Color borderHighlight = Fade(WHITE, 0.15f + (*animProgress * 0.15f));
    DrawRectangleRoundedLines((Rectangle){ x, y, width, height }, 0.2f, 4, borderHighlight);
    
    int fontSize = 20;
    int textWidth = MeasureText(text, fontSize);
    Color textColor = hovered ? WHITE : (Color){ 230, 230, 240, 255 };
    DrawText(text, x + (width - textWidth) / 2.0f, y + (height - fontSize) / 2.0f, fontSize, textColor);
    
    return hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

void Render_DrawCustomTextBox(Rectangle rect, char* textBuffer, i32 maxLen, bool* active, const char* label, Vector2 mousePos) {
    if (CheckCollisionPointRec(mousePos, rect)) {
        SetMouseCursor(MOUSE_CURSOR_IBEAM);
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            *active = true;
        }
    } else {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            *active = false;
        }
    }
    
    if (*active) {
        int key = GetCharPressed();
        while (key > 0) {
            if ((key >= 32) && (key <= 125) && ((int)strlen(textBuffer) < maxLen - 1)) {
                int len = strlen(textBuffer);
                textBuffer[len] = (char)key;
                textBuffer[len + 1] = '\0';
            }
            key = GetCharPressed();
        }
        
        if (IsKeyPressed(KEY_BACKSPACE)) {
            int len = strlen(textBuffer);
            if (len > 0) {
                textBuffer[len - 1] = '\0';
            }
        }
    }
    
    DrawText(label, rect.x, rect.y - 20, 14, GRAY);
    
    Color boxBg = *active ? Fade(BLACK, 0.45f) : Fade(BLACK, 0.3f);
    DrawRectangleRounded(rect, 0.15f, 4, boxBg);
    
    Color borderCol = *active ? SKYBLUE : Fade(WHITE, 0.15f);
    DrawRectangleRoundedLines(rect, 0.15f, 4, borderCol);
    
    int fontSize = 18;
    DrawText(textBuffer, rect.x + 12, rect.y + (rect.height - fontSize) / 2.0f, fontSize, WHITE);
    
    if (*active) {
        if (((int)(GetTime() * 2.0f) % 2) == 0) {
            int textWidth = MeasureText(textBuffer, fontSize);
            DrawRectangle(rect.x + 12 + textWidth + 2, rect.y + (rect.height - 18) / 2.0f, 2, 18, SKYBLUE);
        }
    }
}

void Render_DrawGameTimer(void) {
    if (!globalVariables.currentConnectionState.isConnected) return;
    
    f32 elapsed = globalVariables.currentConnectionState.gameTime;
    Color timerColor = WHITE;
    f32 displayTime = 0.0f;
    
    if (elapsed < 600.0f) {
        displayTime = 600.0f - elapsed;
        timerColor = WHITE;
    } else {
        displayTime = elapsed - 600.0f;
        timerColor = RED;
    }
    
    int minutes = (int)displayTime / 60;
    int seconds = (int)displayTime % 60;
    
    const char* timeText = TextFormat("%02d:%02d", minutes, seconds);
    int fontSize = 24;
    int textWidth = MeasureText(timeText, fontSize);
    
    DrawText(timeText, SCREEN_WIDTH - textWidth - 50, 55, fontSize, timerColor);
}

void Render_DrawHeart(Vector2 center, f32 size, Color color) {
    // Draw black border first
    float border = size * 0.15f;
    float outerSize = size + border;
    
    Vector2 ov1 = { center.x - outerSize, center.y - outerSize * 0.1f };
    Vector2 ov2 = { center.x + outerSize, center.y - outerSize * 0.1f };
    Vector2 ov3 = { center.x, center.y + outerSize };
    DrawTriangle(ov1, ov3, ov2, BLACK);
    DrawCircleV((Vector2){ center.x - outerSize * 0.5f, center.y - outerSize * 0.1f }, outerSize * 0.5f, BLACK);
    DrawCircleV((Vector2){ center.x + outerSize * 0.5f, center.y - outerSize * 0.1f }, outerSize * 0.5f, BLACK);
    
    // Draw main heart
    Vector2 v1 = { center.x - size, center.y - size * 0.1f };
    Vector2 v2 = { center.x + size, center.y - size * 0.1f };
    Vector2 v3 = { center.x, center.y + size };
    DrawTriangle(v1, v3, v2, color);
    DrawCircleV((Vector2){ center.x - size * 0.5f, center.y - size * 0.1f }, size * 0.5f, color);
    DrawCircleV((Vector2){ center.x + size * 0.5f, center.y - size * 0.1f }, size * 0.5f, color);
}

void Render_DrawJoinInputScreen(Vector2 mousePos, f32 deltaTime) {
    int titleFontSize = 40;
    const char* titleText = "JOIN MULTIPLAYER GAME";
    int titleWidth = MeasureText(titleText, titleFontSize);
    DrawText(titleText, (SCREEN_WIDTH - titleWidth) / 2.0f, 90, titleFontSize, SKYBLUE);
    
    float cardWidth = 450.0f;
    float cardHeight = 320.0f;
    float cardX = (SCREEN_WIDTH - cardWidth) / 2.0f;
    float cardY = 180.0f;
    
    DrawRectangleRounded((Rectangle){ cardX, cardY, cardWidth, cardHeight }, 0.08f, 4, Fade(BLACK, 0.55f));
    DrawRectangleRoundedLines((Rectangle){ cardX, cardY, cardWidth, cardHeight }, 0.08f, 4, Fade(WHITE, 0.15f));
    
    static bool nameBoxActive = false;
    Render_DrawCustomTextBox((Rectangle){ cardX + 50, cardY + 40, 350, 40 }, globalVariables.myNameInput, 31, &nameBoxActive, "YOUR NAME", mousePos);
    
    static bool ipBoxActive = false;
    Render_DrawCustomTextBox((Rectangle){ cardX + 50, cardY + 115, 350, 40 }, globalVariables.joinIpAddress, 63, &ipBoxActive, "SERVER IP ADDRESS", mousePos);
    
    static float connectAnim = 0.0f;
    Rectangle connectRect = (Rectangle){ cardX + 50, cardY + 185, 350, 45 };
    if (Render_DrawCustomButton(connectRect, "CONNECT", Fade(SKYBLUE, 0.6f), Fade(BLUE, 0.8f), mousePos, deltaTime, &connectAnim)) {
        printf("JOIN: Connecting to %s...\n", globalVariables.joinIpAddress);
        if (Network_InitConnection(&globalVariables.currentConnectionState, globalVariables.joinIpAddress)) {
            Network_SendNameUpdate(&globalVariables.currentConnectionState, globalVariables.myNameInput);
            globalVariables.currentGameState = STATE_LOBBY;
        } else {
            printf("JOIN ERROR: Failed to connect to server at %s.\n", globalVariables.joinIpAddress);
        }
    }
    
    static float backAnim = 0.0f;
    Rectangle backRect = (Rectangle){ cardX + 50, cardY + 245, 350, 45 };
    if (Render_DrawCustomButton(backRect, "BACK", Fade(DARKGRAY, 0.6f), Fade(GRAY, 0.8f), mousePos, deltaTime, &backAnim)) {
        globalVariables.currentGameState = STATE_MAIN_MENU;
    }
}

void Render_DrawLobby(Vector2 mousePos, f32 deltaTime) {
    int titleFontSize = 40;
    const char* titleText = "MULTIPLAYER LOBBY ROOM";
    int titleWidth = MeasureText(titleText, titleFontSize);
    DrawText(titleText, (SCREEN_WIDTH - titleWidth) / 2.0f, 50, titleFontSize, GOLD);
    
    float cardWidth = 700.0f;
    float cardHeight = 490.0f;
    float cardX = (SCREEN_WIDTH - cardWidth) / 2.0f;
    float cardY = 120.0f;
    
    DrawRectangleRounded((Rectangle){ cardX, cardY, cardWidth, cardHeight }, 0.05f, 4, Fade(BLACK, 0.55f));
    DrawRectangleRoundedLines((Rectangle){ cardX, cardY, cardWidth, cardHeight }, 0.05f, 4, Fade(WHITE, 0.15f));
    
    for (u8 i = 1; i <= 4; i++) {
        float slotX = cardX + 40.0f;
        float slotY = cardY + 25.0f + (i - 1) * (65.0f + 10.0f);
        float slotWidth = cardWidth - 80.0f;
        float slotHeight = 65.0f;
        
        Rectangle slotRec = (Rectangle){ slotX, slotY, slotWidth, slotHeight };
        
        if (Player_IsConnected(i)) {
            DrawRectangleRounded(slotRec, 0.15f, 4, Fade(BLACK, 0.45f));
            DrawRectangleRoundedLines(slotRec, 0.15f, 4, Fade(WHITE, 0.2f));
            
            Color iconCol = RED;
            if (i == 1) iconCol = GOLD;
            else if (i == (int)globalVariables.currentConnectionState.localPlayerIdentification) iconCol = SKYBLUE;
            
            DrawCircle(slotX + 35, slotY + slotHeight/2.0f, 15, iconCol);
            
            const char* letter = (i == 1) ? "H" : TextFormat("%d", i);
            int letterWidth = MeasureText(letter, 12);
            DrawText(letter, slotX + 35 - letterWidth/2.0f, slotY + slotHeight/2.0f - 6, 12, BLACK);
            
            DrawText(globalVariables.playerNames[i - 1], slotX + 70, slotY + 22, 20, WHITE);
            
            if (i == 1) {
                DrawText("HOST", slotX + slotWidth - 80, slotY + 25, 14, GOLD);
            } else {
                DrawText("READY", slotX + slotWidth - 80, slotY + 25, 14, GREEN);
            }
        } else {
            float pulse = 0.4f + 0.2f * sinf(GetTime() * 3.0f + i);
            
            DrawRectangleRounded(slotRec, 0.15f, 4, Fade(BLACK, 0.2f));
            DrawRectangleRoundedLines(slotRec, 0.15f, 4, Fade(WHITE, 0.08f));
            
            DrawCircle(slotX + 35, slotY + slotHeight/2.0f, 15, Fade(GRAY, pulse));
            DrawText("Waiting for player...", slotX + 70, slotY + 24, 16, Fade(GRAY, pulse));
        }
    }
    
    static bool lobbyNameActive = false;
    char tempName[32];
    strcpy(tempName, globalVariables.myNameInput);
    
    Render_DrawCustomTextBox((Rectangle){ cardX + 40, cardY + cardHeight - 80, 300, 40 }, globalVariables.myNameInput, 31, &lobbyNameActive, "CHANGE YOUR NAME", mousePos);
    
    if (strcmp(tempName, globalVariables.myNameInput) != 0) {
        Network_SendNameUpdate(&globalVariables.currentConnectionState, globalVariables.myNameInput);
        u32 localID = globalVariables.currentConnectionState.localPlayerIdentification;
        if (localID > 0 && localID <= MAX_PLAYERS) {
            strncpy(globalVariables.playerNames[localID - 1], globalVariables.myNameInput, 31);
            globalVariables.playerNames[localID - 1][31] = '\0';
        }
    }
    
    if (globalVariables.currentConnectionState.localPlayerIdentification == 1) {
        static float startBtnAnim = 0.0f;
        Rectangle startBtnRect = (Rectangle){ cardX + cardWidth - 220, cardY + cardHeight - 80, 180, 40 };
        
        if (Render_DrawCustomButton(startBtnRect, "START GAME", (Color){ 245, 130, 48, 255 }, (Color){ 253, 191, 111, 255 }, mousePos, deltaTime, &startBtnAnim)) {
            Network_SendStartGame(&globalVariables.currentConnectionState);
        }
    } else {
        float pulseAlpha = 0.5f + 0.3f * sinf(GetTime() * 4.0f);
        DrawRectangleRounded((Rectangle){ cardX + cardWidth - 250, cardY + cardHeight - 80, 210, 40 }, 0.2f, 4, Fade(BLACK, 0.4f));
        DrawRectangleRoundedLines((Rectangle){ cardX + cardWidth - 250, cardY + cardHeight - 80, 210, 40 }, 0.2f, 4, Fade(WHITE, 0.15f));
        
        int textWidth = MeasureText("Waiting for Host...", 14);
        DrawText("Waiting for Host...", cardX + cardWidth - 250 + (210 - textWidth) / 2.0f, cardY + cardHeight - 80 + 13, 14, Fade(GOLD, pulseAlpha));
    }
}

void Render_DrawMainMenu(Vector2 mousePos, f32 deltaTime) {
    float titleGlow = 0.5f + 0.5f * sinf(GetTime() * 2.0f);
    int titleFontSize = 48;
    const char* titleText = "ARCADE SURVIVORS ONLINE";
    int titleWidth = MeasureText(titleText, titleFontSize);
    
    DrawText(titleText, (SCREEN_WIDTH - titleWidth) / 2.0f + 3, 90 + 3, titleFontSize, Fade(BLACK, 0.5f));
    DrawText(titleText, (SCREEN_WIDTH - titleWidth) / 2.0f, 90, titleFontSize, Fade(GOLD, 0.8f + titleGlow * 0.2f));
    
    float cardWidth = 400.0f;
    float cardHeight = 350.0f;
    float cardX = (SCREEN_WIDTH - cardWidth) / 2.0f;
    float cardY = 190.0f;
    
    DrawRectangleRounded((Rectangle){ cardX, cardY, cardWidth, cardHeight }, 0.08f, 4, Fade(BLACK, 0.55f));
    DrawRectangleRoundedLines((Rectangle){ cardX, cardY, cardWidth, cardHeight }, 0.08f, 4, Fade(WHITE, 0.15f));
    
    static bool nameBoxActive = false;
    Render_DrawCustomTextBox((Rectangle){ cardX + 50, cardY + 40, 300, 40 }, globalVariables.myNameInput, 31, &nameBoxActive, "YOUR NAME", mousePos);
    
    static float hostAnim = 0.0f;
    Rectangle hostRect = (Rectangle){ cardX + 50, cardY + 120, 300, 45 };
    if (Render_DrawCustomButton(hostRect, "HOST GAME", Fade(DARKGREEN, 0.6f), Fade(LIME, 0.8f), mousePos, deltaTime, &hostAnim)) {
        printf("HOST: Spawning server...\n");
        system("start python server/server.py");
        
        double startTime = GetTime();
        while (GetTime() - startTime < 1.2) {
            // Wait to ensure Python server binds
        }
        
        if (Network_InitConnection(&globalVariables.currentConnectionState, "127.0.0.1")) {
            Network_SendNameUpdate(&globalVariables.currentConnectionState, globalVariables.myNameInput);
            globalVariables.currentGameState = STATE_LOBBY;
        } else {
            printf("HOST ERROR: Failed to connect to server.\n");
        }
    }
    
    static float joinAnim = 0.0f;
    Rectangle joinRect = (Rectangle){ cardX + 50, cardY + 185, 300, 45 };
    if (Render_DrawCustomButton(joinRect, "JOIN GAME", Fade(BLUE, 0.6f), Fade(SKYBLUE, 0.8f), mousePos, deltaTime, &joinAnim)) {
        globalVariables.currentGameState = STATE_JOIN_IP;
    }
    
    static float exitAnim = 0.0f;
    Rectangle exitRect = (Rectangle){ cardX + 50, cardY + 250, 300, 45 };
    if (Render_DrawCustomButton(exitRect, "EXIT TO DESKTOP", Fade(DARKGRAY, 0.6f), Fade(RED, 0.8f), mousePos, deltaTime, &exitAnim)) {
        globalVariables.currentInputState.quitApplication = true;
    }
}

void Render_DrawStatsOverlay(void) {
    // 1. Dark fullscreen backdrop with 50% opacity
    DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(BLACK, 0.5f));
    
    // 2. Centered HUD Panel
    Rectangle hud = { 128, 72, 1024, 576 };
    DrawRectangleRec(hud, Fade(BLACK, 0.85f));
    DrawRectangleLinesEx(hud, 3, GOLD);
    
    // Determine which player we are spectating/viewing
    u32 targetPlayerID = globalVariables.spectatedPlayerID;
    if (targetPlayerID == 0) {
        targetPlayerID = globalVariables.currentConnectionState.localPlayerIdentification;
    }
    
    // 3. Header title
    const char* headerText = "";
    if (targetPlayerID == globalVariables.currentConnectionState.localPlayerIdentification) {
        headerText = TextFormat("PLAYER PROFILE & STATS (YOU - ID: %u)", targetPlayerID);
    } else {
        headerText = TextFormat("PLAYER PROFILE & STATS (PLAYER %u)", targetPlayerID);
    }
    DrawText(headerText, hud.x + (hud.width - MeasureText(headerText, 24)) / 2.0f, hud.y + 20, 24, GOLD);
    DrawLine(hud.x + 50, hud.y + 60, hud.x + 974, hud.y + 60, GRAY);
    
    // Get player attributes
    u32 spectatedIndex = (targetPlayerID - 1) % MAX_REMOTE_PLAYERS;
    PlayerAttributes* attr = &globalVariables.currentConnectionState.playerAttributes[spectatedIndex];
    
    // Reconstruct weapons and relics for display
    Weapon displayWeapons[4];
    Relic displayRelics[4];
    for (u8 i = 0; i < 4; i++) {
        displayWeapons[i].type = WEAPON_UNDEFINED;
        displayWeapons[i].level = 0;
        displayRelics[i].type = RELIC_UNDEFINED;
        displayRelics[i].level = 0;
    }
    
    if (targetPlayerID == globalVariables.currentConnectionState.localPlayerIdentification) {
        for (u8 i = 0; i < 4; i++) {
            displayWeapons[i] = globalVariables.playerWeapons[i];
            displayRelics[i] = globalVariables.playerRelics[i];
        }
    } else {
        u32 entityIndex = targetPlayerID % MAX_REMOTE_ENTITIES;
        Entity* entity = &globalVariables.currentConnectionState.remoteEntities[entityIndex];
        if (entity->entityType == ENTITY_CHARACTER && entity->character.characterType == CHARACTER_PLAYER) {
            int slotCount = 0;
            for (u8 w = 0; w < 5; w++) {
                u8 level = entity->character.weaponLevels[w];
                if (level > 0 && slotCount < 4) {
                    WeaponType wType = (WeaponType)(w + 1);
                    Weapon_Initialize(&displayWeapons[slotCount], wType);
                    displayWeapons[slotCount].level = level;
                    for (int l = 1; l < level; l++) {
                        Weapon_Upgrade(&displayWeapons[slotCount]);
                    }
                    slotCount++;
                }
            }
            
            int relicSlotCount = 0;
            for (u8 r = 0; r < 7; r++) {
                u8 level = entity->character.relicLevels[r];
                if (level > 0 && relicSlotCount < 4) {
                    displayRelics[relicSlotCount].type = (RelicType)(r + 1);
                    displayRelics[relicSlotCount].level = level;
                    relicSlotCount++;
                }
            }
        }
    }
    
    // 4. Left Column - Equipped Gear
    float leftX = hud.x + 50;
    DrawText("EQUIPPED GEAR", leftX, hud.y + 80, 20, SKYBLUE);
    
    // Weapons
    DrawText("WEAPONS", leftX, hud.y + 115, 16, YELLOW);
    const char* weaponNames[] = { "", "Fireball", "Crystal Staff", "Death Aura", "Bomb Shoes", "Nature Spikes" };
    Color weaponColors[] = { WHITE, ORANGE, SKYBLUE, BLACK, RED, GREEN };
    for (u8 i = 0; i < 4; i++) {
        float slotY = hud.y + 145 + i * 32;
        Weapon* w = &displayWeapons[i];
        if (w->type == WEAPON_UNDEFINED) {
            DrawText(TextFormat("[%d] [Empty Weapon Slot]", i + 1), leftX + 15, slotY, 14, DARKGRAY);
        } else {
            DrawRectangle(leftX + 15, slotY + 2, 10, 10, weaponColors[w->type]);
            DrawRectangleLines(leftX + 15, slotY + 2, 10, 10, WHITE);
            
            DrawText(TextFormat("%s  -  Lv.%d  (Dmg: %.1f, Spd: %.2fx, Sz: %.1fx)", 
                                weaponNames[w->type], w->level, 
                                w->stats.damage, w->stats.attackSpeed, w->stats.size), 
                     leftX + 35, slotY, 14, WHITE);
        }
    }
    
    // Relics
    DrawText("RELICS", leftX, hud.y + 295, 16, PINK);
    const char* relicNames[] = { "", "Relic of Health", "Relic of Damage", "Relic of Attack Speed", "Relic of Size", "Relic of Movement Speed", "Relic of XP Gain", "Relic of Lifesteal" };
    Color relicColors[] = { WHITE, RED, ORANGE, GOLD, PURPLE, LIME, PINK, VIOLET };
    for (u8 i = 0; i < 4; i++) {
        float slotY = hud.y + 325 + i * 32;
        Relic* r = &displayRelics[i];
        if (r->type == RELIC_UNDEFINED) {
            DrawText(TextFormat("[%d] [Empty Relic Slot]", i + 1), leftX + 15, slotY, 14, DARKGRAY);
        } else {
            DrawRectangle(leftX + 15, slotY + 2, 10, 10, relicColors[r->type]);
            DrawRectangleLines(leftX + 15, slotY + 2, 10, 10, WHITE);
            
            static const f32 RELIC_BONUS_MULTIPLIERS[] = {
                [RELIC_UNDEFINED] = 0.0f,
                [RELIC_HEALTH] = RELIC_LEVELUP_HEALTH,
                [RELIC_DAMAGE] = RELIC_LEVELUP_DAMAGE,
                [RELIC_ATTACK_SPEED] = RELIC_LEVELUP_ATTACKSPEED,
                [RELIC_SIZE] = RELIC_LEVELUP_SIZE,
                [RELIC_MOVEMENT_SPEED] = RELIC_LEVELUP_MOVEMENTSPEED,
                [RELIC_XP_GAIN] = RELIC_LEVELUP_XPGAIN,
                [RELIC_LIFE_STEAL] = RELIC_LEVELUP_LIFESTEAL
            };
            int pct = (int)(r->level * RELIC_BONUS_MULTIPLIERS[r->type] * 100.0f);
            
            DrawText(TextFormat("%s  -  Lv.%d  (+%d%% Bonus)", 
                                relicNames[r->type], r->level, pct), 
                     leftX + 35, slotY, 14, WHITE);
        }
    }
    
    // 5. Vertical Divider Line
    float midX = hud.x + 500;
    DrawLine(midX, hud.y + 80, midX, hud.y + 510, GRAY);
    
    // 6. Right Column - Player Attributes
    float rightX = hud.x + 540;
    DrawText("PLAYER ATTRIBUTES", rightX, hud.y + 80, 20, GOLD);
    
    DrawText(TextFormat("Max Health:               %.1f  (Base: %.1f)", attr->maxHealth, DEFAULT_MAX_HEALTH), rightX + 20, hud.y + 125, 15, WHITE);
    DrawText(TextFormat("Damage Multiplier:        %.2fx  (Base: %.2fx)", attr->damage, DEFAULT_DAMAGE), rightX + 20, hud.y + 175, 15, WHITE);
    DrawText(TextFormat("Attack Speed Multiplier:  %.2fx  (Base: %.2fx)", attr->attackSpeed, DEFAULT_ATTACK_SPEED), rightX + 20, hud.y + 225, 15, WHITE);
    DrawText(TextFormat("Movement Speed Multiplier:%.2fx  (Base: %.2fx)", attr->movementSpeed, DEFAULT_MOVEMENT_SPEED), rightX + 20, hud.y + 275, 15, WHITE);
    DrawText(TextFormat("Area Size Multiplier:     %.2fx  (Base: %.2fx)", attr->size, DEFAULT_SIZE), rightX + 20, hud.y + 325, 15, WHITE);
    DrawText(TextFormat("XP Gain Multiplier:       %.2fx  (Base: %.2fx)", attr->xpGained, DEFAULT_XP_GAINED), rightX + 20, hud.y + 375, 15, WHITE);
    DrawText(TextFormat("Lifesteal Factor:         %.0f%%  (Base: %.0f%%)", attr->lifeSteal * 100.0f, DEFAULT_LIFESTEAL * 100.0f), rightX + 20, hud.y + 425, 15, WHITE);
    
    // Decorative frame highlight around active stats
    DrawRectangleLinesEx((Rectangle){ rightX, hud.y + 110, 434, 345 }, 1, Fade(GRAY, 0.4f));
    
    // 7. Footer controls hint
    DrawText("HOLD [TAB] TO KEEP THIS OVERLAY OPEN", hud.x + 330, hud.y + 535, 14, GRAY);
}

void Render_DrawTombstone(Vector2 pos, const char* name, Color nameColor) {
    float width = 30.0f;
    float height = 40.0f;
    
    // Draw base slab
    DrawRectangle(pos.x - width/2.0f, pos.y - height/2.0f + 5.0f, width, height - 5.0f, GRAY);
    
    // Draw rounded top
    DrawCircleV((Vector2){ pos.x, pos.y - height/2.0f + 5.0f }, width/2.0f, GRAY);
    
    // Draw outlines
    DrawRectangleLines(pos.x - width/2.0f, pos.y - height/2.0f + 5.0f, width, height - 5.0f, DARKGRAY);
    DrawCircleLines((int)pos.x, (int)(pos.y - height/2.0f + 5.0f), width/2.0f, DARKGRAY);
    
    // Draw the "RIP" cross on tombstone
    DrawRectangle(pos.x - 2, pos.y - 12, 4, 16, BLACK);
    DrawRectangle(pos.x - 6, pos.y - 8, 12, 4, BLACK);
    
    // Draw labels
    DrawText("R.I.P.", pos.x - 12, pos.y + 10, 8, BLACK);
    
    // Draw player name above tombstone
    DrawText(name, pos.x - MeasureText(name, 10)/2, pos.y - height/2.0f - 18, 10, nameColor);
    DrawText("DEAD", pos.x - MeasureText("DEAD", 10)/2, pos.y - height/2.0f - 8, 10, RED);
}

void Render_DrawUpgradeCards(void) {
    float cardWidth = 220.0f;
    float cardHeight = 150.0f;
    float spacing = 20.0f;
    float totalWidth = (cardWidth * 3) + (spacing * 2);
    float startX = (SCREEN_WIDTH - totalWidth) / 2.0f;
    float startY = SCREEN_HEIGHT - cardHeight - 20.0f;
    
    for (u8 i = 0; i < 3; i++) {
        if (globalVariables.upgradeOptions[i].type == 0) continue;
        
        Rectangle card = { startX + i * (cardWidth + spacing), startY, cardWidth, cardHeight };
        
        // Draw card background
        DrawRectangleRec(card, Fade(BLACK, 0.8f));
        DrawRectangleLinesEx(card, 2, YELLOW);
        
        // Draw Header
        DrawText(TextFormat("[%d] SELECT", i + 1), card.x + 10, card.y + 10, 16, YELLOW);
        
        // Securely tokenize and wrap name within 140px next to the icon
        int titleFontSize = 16;
        int currentY = card.y + 35;
        
        char nameBuffer[128];
        strncpy(nameBuffer, globalVariables.upgradeOptions[i].name, sizeof(nameBuffer) - 1);
        nameBuffer[sizeof(nameBuffer) - 1] = '\0';
        
        char line[128] = "";
        char* word = strtok(nameBuffer, " ");
        while (word != NULL) {
            char testLine[256];
            if (strlen(line) == 0) {
                strcpy(testLine, word);
            } else {
                sprintf(testLine, "%s %s", line, word);
            }
            
            if (MeasureText(testLine, titleFontSize) > 140) {
                DrawText(line, card.x + 10, currentY, titleFontSize, WHITE);
                currentY += titleFontSize + 2;
                strcpy(line, word);
            } else {
                strcpy(line, testLine);
            }
            word = strtok(NULL, " ");
        }
        if (strlen(line) > 0) {
            DrawText(line, card.x + 10, currentY, titleFontSize, WHITE);
            currentY += titleFontSize + 2;
        }
        
        // Dynamically compute subsequent positions
        float levelY = currentY + 5.0f;
        float descY = levelY + 25.0f;
        
        int currentLv = 0;
        if (globalVariables.upgradeOptions[i].isRelic) {
            for (u8 j = 0; j < 4; j++) {
                if (globalVariables.playerRelics[j].type == globalVariables.upgradeOptions[i].type) {
                    currentLv = globalVariables.playerRelics[j].level;
                    break;
                }
            }
            if (currentLv > 0) {
                DrawText(TextFormat("Level %d -> %d", currentLv, currentLv + 1), card.x + 10, levelY, 16, GREEN);
            } else {
                DrawText("NEW RELIC", card.x + 10, levelY, 16, PINK);
            }
        } else {
            for (u8 j = 0; j < 4; j++) {
                if (globalVariables.playerWeapons[j].type == globalVariables.upgradeOptions[i].type) {
                    currentLv = globalVariables.playerWeapons[j].level;
                    break;
                }
            }
            if (currentLv > 0) {
                DrawText(TextFormat("Level %d -> %d", currentLv, currentLv + 1), card.x + 10, levelY, 16, GREEN);
            } else {
                DrawText("NEW WEAPON", card.x + 10, levelY, 16, SKYBLUE);
            }
        }
        
        // Small Color Box
        DrawRectangle(card.x + 160, card.y + 35, 40, 40, globalVariables.upgradeOptions[i].color);
        DrawRectangleLines(card.x + 160, card.y + 35, 40, 40, WHITE);
        
        // Draw Description
        DrawText(globalVariables.upgradeOptions[i].description, card.x + 10, descY, 14, GRAY);
    }
}

void Render_DrawXPBar(void) {
    float barWidth = SCREEN_WIDTH * 0.8f;
    float barHeight = 20.0f;
    float x = (SCREEN_WIDTH - barWidth) / 2.0f;
    float y = 20.0f;
    
    float progress = (float)globalVariables.playerXP / globalVariables.xpToNextLevel;
    
    DrawRectangle(x, y, barWidth, barHeight, Fade(DARKBLUE, 0.5f));
    DrawRectangle(x, y, barWidth * progress, barHeight, SKYBLUE);
    DrawRectangleLines(x, y, barWidth, barHeight, WHITE);
    
    DrawText(TextFormat("LV %d", globalVariables.playerLevel), (int)x, (int)(y + barHeight + 5), 20, WHITE);
}

void Render_Entity(const Entity* entity) {
    if (entity->entityType == ENTITY_UNDEFINED) return;
    
    // Hide dead enemies (waiting for server despawn)
    if (entity->entityType == ENTITY_CHARACTER && entity->character.characterType == CHARACTER_ENEMY && entity->character.health <= 0) return;

    // Struct to encapsulate enemy rendering properties
    typedef struct {
        f32 radiusMultiplier;
        Color baseColor;
        const char* labelText;
        i32 barWidth;
    } EnemyClassRenderProps;

    // Enum-indexed array lookup for properties of each enemy class
    static const EnemyClassRenderProps ENEMY_CLASS_RENDER_PROPS[] = {
        [ENEMY_CLASS_NORMAL] = { 1.0f,  PURPLE,     "ENEMY", 40 },
        [ENEMY_CLASS_FAST]   = { 0.75f, ORANGE,     "FAST",  30 },
        [ENEMY_CLASS_TANK]   = { 1.5f,  DARKPURPLE, "TANK",  60 },
        [ENEMY_CLASS_BOSS]   = { 2.5f,  MAROON,     "BOSS",  100 }
    };

    switch (entity->entityType) {
        case ENTITY_CHARACTER:
            if (entity->character.characterType == CHARACTER_PLAYER) {
                u32 playerID = (u32)(entity - globalVariables.currentConnectionState.remoteEntities);
                u32 idx = (playerID - 1) % MAX_PLAYERS;
                const char* displayName = globalVariables.playerNames[idx];
                if (entity->character.health <= 0.0f) {
                    Render_DrawTombstone(entity->character.position, displayName, MAROON);
                } else {
                    DrawCircleV(entity->character.position, PLAYER_RADIUS, RED);
                    int textWidth = MeasureText(displayName, 12);
                    DrawText(displayName, entity->character.position.x - textWidth / 2, entity->character.position.y - 40, 12, MAROON);
                    
                    // Draw remote player high-frequency pulse damage flash if timer is active
                    if (entity->character.damageFlashTimer > 0) {
                        f32 flashAlpha = (sinf(entity->character.damageFlashTimer * 75.0f) > 0.0f) ? 0.7f : 0.0f;
                        if (flashAlpha > 0.0f) {
                            DrawCircleV(entity->character.position, PLAYER_RADIUS, Fade(WHITE, flashAlpha));
                        }
                    }
                }
            } else if (entity->character.characterType == CHARACTER_ENEMY) {
                EnemyClassRenderProps props = ENEMY_CLASS_RENDER_PROPS[entity->character.enemyClass];
                f32 radius = PLAYER_RADIUS * props.radiusMultiplier;
                Color baseColor = props.baseColor;
                const char* labelText = props.labelText;
                i32 barWidth = props.barWidth;
                
                DrawCircleV(entity->character.position, radius, baseColor);
                // Draw HP Bar
                f32 hpPercent = entity->character.health / entity->character.maxHealth;
                if (hpPercent < 0.0f) hpPercent = 0.0f;
                if (hpPercent > 1.0f) hpPercent = 1.0f;
                DrawRectangle(entity->character.position.x - barWidth / 2, entity->character.position.y - radius - 10, barWidth, 5, DARKGRAY);
                DrawRectangle(entity->character.position.x - barWidth / 2, entity->character.position.y - radius - 10, (int)(barWidth * hpPercent), 5, GREEN);
                DrawText(labelText, entity->character.position.x - barWidth / 2, entity->character.position.y - radius - 25, 10, baseColor);
                
                // Draw enemy smooth ease-in/ease-out damage flash if timer is active
                if (entity->character.damageFlashTimer > 0) {
                    f32 t = entity->character.damageFlashTimer / 0.15f;
                    f32 flashAlpha = sinf(t * 3.14159265f);
                    if (flashAlpha > 0.0f) {
                        DrawCircleV(entity->character.position, radius, Fade(WHITE, flashAlpha));
                    }
                }
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
                    DrawCircleLinesV(entity->proj.position, entity->proj.radius, Fade(RED, 0.5f));
                }
            } else if (entity->proj.type == PROJECTILE_SPIKE) {
                f32 spikeSize = entity->proj.radius;
                DrawTriangle((Vector2){entity->proj.position.x, entity->proj.position.y - 20.0f * spikeSize},
                             (Vector2){entity->proj.position.x - 15.0f * spikeSize, entity->proj.position.y + 10.0f * spikeSize},
                             (Vector2){entity->proj.position.x + 15.0f * spikeSize, entity->proj.position.y + 10.0f * spikeSize}, BROWN);
                DrawTriangle((Vector2){entity->proj.position.x, entity->proj.position.y - 25.0f * spikeSize},
                             (Vector2){entity->proj.position.x - 10.0f * spikeSize, entity->proj.position.y + 5.0f * spikeSize},
                             (Vector2){entity->proj.position.x + 10.0f * spikeSize, entity->proj.position.y + 5.0f * spikeSize}, GRAY);
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

void Render_SpawnDamagePopup(Vector2 position, f32 damage, Color color) {
    for (u16 i = 0; i < 256; i++) {
        if (globalVariables.currentConnectionState.localDamagePopups[i].entityType == ENTITY_UNDEFINED) {
            f32 offsetX = (f32)(rand() % 31 - 15);
            f32 offsetY = (f32)(rand() % 31 - 15);
            
            globalVariables.currentConnectionState.localDamagePopups[i].entityType = ENTITY_DAMAGE_POPUP;
            globalVariables.currentConnectionState.localDamagePopups[i].damagePopup.position = (Vector2){ position.x + offsetX, position.y + offsetY };
            globalVariables.currentConnectionState.localDamagePopups[i].damagePopup.damageValue = damage;
            globalVariables.currentConnectionState.localDamagePopups[i].damagePopup.lifetime = 0.0f;
            globalVariables.currentConnectionState.localDamagePopups[i].damagePopup.color = color;
            break;
        }
    }
}

void Render_UpdateAndDrawMenuParticles(f32 deltaTime) {
    if (!globalVariables.particlesInitialized) {
        for (u8 i = 0; i < MAX_MENU_PARTICLES; i++) {
            globalVariables.menuParticles[i].position = (Vector2){ (float)(rand() % SCREEN_WIDTH), (float)(rand() % SCREEN_HEIGHT) };
            globalVariables.menuParticles[i].velocity = (Vector2){ (float)((rand() % 40) - 20) / 10.0f, (float)((rand() % 40) - 20) / 10.0f };
            globalVariables.menuParticles[i].size = (float)((rand() % 6) + 2);
            globalVariables.menuParticles[i].alpha = (float)(rand() % 100) / 100.0f * 0.4f + 0.1f;
            
            int colorIndex = rand() % 4;
            if (colorIndex == 0) globalVariables.menuParticles[i].color = (Color){ 120, 220, 255, 255 }; // Neo cyan
            else if (colorIndex == 1) globalVariables.menuParticles[i].color = (Color){ 255, 150, 200, 255 }; // Neo pink
            else if (colorIndex == 2) globalVariables.menuParticles[i].color = (Color){ 180, 150, 255, 255 }; // Neo violet
            else globalVariables.menuParticles[i].color = (Color){ 255, 230, 150, 255 }; // Neo gold
        }
        globalVariables.particlesInitialized = true;
    }
    
    for (u8 i = 0; i < MAX_MENU_PARTICLES; i++) {
        globalVariables.menuParticles[i].position.x += globalVariables.menuParticles[i].velocity.x * deltaTime * 10.0f;
        globalVariables.menuParticles[i].position.y += globalVariables.menuParticles[i].velocity.y * deltaTime * 10.0f;
        
        if (globalVariables.menuParticles[i].position.x < 0) globalVariables.menuParticles[i].position.x += SCREEN_WIDTH;
        if (globalVariables.menuParticles[i].position.x > SCREEN_WIDTH) globalVariables.menuParticles[i].position.x -= SCREEN_WIDTH;
        if (globalVariables.menuParticles[i].position.y < 0) globalVariables.menuParticles[i].position.y += SCREEN_HEIGHT;
        if (globalVariables.menuParticles[i].position.y > SCREEN_HEIGHT) globalVariables.menuParticles[i].position.y -= SCREEN_HEIGHT;
        
        float sizePulse = globalVariables.menuParticles[i].size + sinf(GetTime() + i) * 1.5f;
        if (sizePulse < 1.0f) sizePulse = 1.0f;
        
        DrawCircleV(globalVariables.menuParticles[i].position, sizePulse + 3.0f, Fade(globalVariables.menuParticles[i].color, globalVariables.menuParticles[i].alpha * 0.3f));
        DrawCircleV(globalVariables.menuParticles[i].position, sizePulse, Fade(globalVariables.menuParticles[i].color, globalVariables.menuParticles[i].alpha));
    }
}

//~ End of Renderer

//~ Begin of Weapons

void Weapon_ApplyUpgrade(i32 optionIndex) {
    LevelUpOption option = globalVariables.upgradeOptions[optionIndex];
    if (option.type == 0) return;

    u8 newLevel = 0;
    if (option.isRelic) {
        RelicType type = (RelicType)option.type;
        // Check if we already have it
        for (u8 i = 0; i < 4; i++) {
            if (globalVariables.playerRelics[i].type == type) {
                if (globalVariables.playerRelics[i].level < 15) {
                    globalVariables.playerRelics[i].level++;
                    newLevel = globalVariables.playerRelics[i].level;
                    Player_RecalculateAttributes();
                }
                break;
            }
        }
        
        if (newLevel == 0) {
            // Find empty slot
            for (u8 i = 0; i < 4; i++) {
                if (globalVariables.playerRelics[i].type == RELIC_UNDEFINED) {
                    globalVariables.playerRelics[i].type = type;
                    globalVariables.playerRelics[i].level = 1;
                    newLevel = 1;
                    Player_RecalculateAttributes();
                    break;
                }
            }
        }
    } else {
        WeaponType type = (WeaponType)option.type;
        // Check if we already have it
        for (u8 i = 0; i < 4; i++) {
            if (globalVariables.playerWeapons[i].type == type) {
                Weapon_Upgrade(&globalVariables.playerWeapons[i]);
                newLevel = globalVariables.playerWeapons[i].level;
                break;
            }
        }
        
        if (newLevel == 0) {
            // Find empty slot
            for (u8 i = 0; i < 4; i++) {
                if (globalVariables.playerWeapons[i].type == WEAPON_UNDEFINED) {
                    Weapon_Initialize(&globalVariables.playerWeapons[i], type);
                    newLevel = 1;
                    break;
                }
            }
        }
    }

    if (newLevel > 0) {
        Network_SendUpgradeUpdate(&globalVariables.currentConnectionState, option.isRelic, option.type, newLevel);
    }
}

void Weapon_FireFireballRing(Vector2 position, u32 ownerID) {
    // Deprecated: Now handled by server via Network_SendWeaponFire
}

void Weapon_GenerateUpgradeOptions(LevelUpOption options[3]) {
    UpgradeCandidate candidates[12];
    int candidateCount = 0;

    // Count owned weapons and relics
    int ownedWeaponsCount = 0;
    for (u8 i = 0; i < 4; i++) {
        if (globalVariables.playerWeapons[i].type != WEAPON_UNDEFINED) {
            ownedWeaponsCount++;
        }
    }

    int ownedRelicsCount = 0;
    for (u8 i = 0; i < 4; i++) {
        if (globalVariables.playerRelics[i].type != RELIC_UNDEFINED) {
            ownedRelicsCount++;
        }
    }

    // 1. Gather Weapon candidates
    for (u8 wType = 1; wType <= 5; wType++) {
        int ownedIndex = -1;
        for (u8 i = 0; i < 4; i++) {
            if (globalVariables.playerWeapons[i].type == wType) {
                ownedIndex = i;
                break;
            }
        }
        
        if (ownedIndex != -1) {
            // Owned: can upgrade if level < 15
            if (globalVariables.playerWeapons[ownedIndex].level < 15) {
                candidates[candidateCount++] = (UpgradeCandidate){ .isRelic = false, .type = wType };
            }
        } else {
            // Not owned: can acquire if inventory not full
            if (ownedWeaponsCount < 4) {
                candidates[candidateCount++] = (UpgradeCandidate){ .isRelic = false, .type = wType };
            }
        }
    }

    // 2. Gather Relic candidates
    for (u8 rType = 1; rType <= 7; rType++) {
        int ownedIndex = -1;
        for (u8 i = 0; i < 4; i++) {
            if (globalVariables.playerRelics[i].type == rType) {
                ownedIndex = i;
                break;
            }
        }
        
        if (ownedIndex != -1) {
            // Owned: can upgrade if level < 5
            if (globalVariables.playerRelics[ownedIndex].level < 5) {
                candidates[candidateCount++] = (UpgradeCandidate){ .isRelic = true, .type = rType };
            }
        } else {
            // Not owned: can acquire if inventory not full
            if (ownedRelicsCount < 4) {
                candidates[candidateCount++] = (UpgradeCandidate){ .isRelic = true, .type = rType };
            }
        }
    }

    // Shuffle candidate pool
    for (int i = 0; i < candidateCount; i++) {
        int r = i + rand() % (candidateCount - i);
        UpgradeCandidate temp = candidates[i];
        candidates[i] = candidates[r];
        candidates[r] = temp;
    }

    int countToGen = (candidateCount < 3) ? candidateCount : 3;
    for (u8 i = 0; i < 3; i++) {
        if (i < countToGen) {
            UpgradeCandidate selected = candidates[i];
            options[i].isRelic = selected.isRelic;
            options[i].type = selected.type;

            if (selected.isRelic) {
                const char* relicNames[] = { "", "Relic of Health", "Relic of Damage", "Relic of Attack Speed", "Relic of Size", "Relic of Movement Speed", "Relic of XP Gain", "Relic of Lifesteal" };
                const char* relicDescs[] = { "", "Increases Max Health (+12%)", "Increases Damage (+8%)", "Increases Attack Speed (+6%)", "Increases Size (+15%)", "Increases Speed (+9%)", "Increases XP Gained (+8%)", "Increases Lifesteal (+1%)" };
                Color relicColors[] = { WHITE, RED, ORANGE, GOLD, PURPLE, LIME, PINK, VIOLET };

                options[i].name = relicNames[selected.type];
                options[i].description = relicDescs[selected.type];
                options[i].color = relicColors[selected.type];
            } else {
                const char* weaponNames[] = { "", "Fireball", "Crystal Staff", "Death Aura", "Bomb Shoes", "Nature Spikes" };
                const char* weaponDescs[] = { "", "Fiery explosions", "Piercing crystals", "Continuous damage aura", "Delayed explosions", "Spikes from the earth" };
                Color weaponColors[] = { WHITE, ORANGE, SKYBLUE, BLACK, RED, GREEN };

                options[i].name = weaponNames[selected.type];
                options[i].description = weaponDescs[selected.type];
                options[i].color = weaponColors[selected.type];
            }
        } else {
            options[i].isRelic = false;
            options[i].type = 0; // Undefined
            options[i].name = "";
            options[i].description = "";
            options[i].color = BLANK;
        }
    }
}

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

void Weapon_ProjectileUpdateMovement(f32 deltaTime) {
    for (u16 i = 0; i < MAX_REMOTE_ENTITIES; i++) {
        Entity* entity = &globalVariables.currentConnectionState.remoteEntities[i];
        if (entity->entityType == ENTITY_PROJECTILE) {
            Projectile* proj = &entity->proj;
            
            // Move
            proj->position.x += proj->velocity.x * deltaTime;
            proj->position.y += proj->velocity.y * deltaTime;
            
            // Lifetime (Predicted locally)
            proj->lifetime -= deltaTime;

            u32 ownerIndex = (proj->ownerID - 1) % MAX_REMOTE_PLAYERS;
            PlayerAttributes* ownerAttr = &globalVariables.currentConnectionState.playerAttributes[ownerIndex];
            bool isLocalOwner = (proj->ownerID == globalVariables.currentConnectionState.localPlayerIdentification);

            // Collision check with enemies (Client prediction)
            if (proj->type == PROJECTILE_FIREBALL) {
                for (u16 enemyIndex = 0; enemyIndex < MAX_REMOTE_ENTITIES; enemyIndex++) {
                    Entity* remoteEntity = &globalVariables.currentConnectionState.remoteEntities[enemyIndex];
                    if (remoteEntity->entityType == ENTITY_CHARACTER && remoteEntity->character.characterType == CHARACTER_ENEMY) {
                        if (CheckCollisionCircles(proj->position, 10, remoteEntity->character.position, PLAYER_RADIUS)) {
                            // Prediction: Hide fireball and show local explosion instantly
                            f32 explosionRadius = proj->radius;
                            f32 explosionDamage = 50.0f * ownerAttr->damage;

                            for (u8 v = 0; v < 128; v++) {
                                if (!globalVariables.currentConnectionState.localVisualEffects[v].active) {
                                    globalVariables.currentConnectionState.localVisualEffects[v].active = true;
                                    globalVariables.currentConnectionState.localVisualEffects[v].position = proj->position;
                                    globalVariables.currentConnectionState.localVisualEffects[v].radius = explosionRadius;
                                    globalVariables.currentConnectionState.localVisualEffects[v].lifetime = 0.5f;
                                    break;
                                }
                            }
                            
                            // Explosion damage report
                            for (u16 otherIndex = 0; otherIndex < MAX_REMOTE_ENTITIES; otherIndex++) {
                                Entity* other = &globalVariables.currentConnectionState.remoteEntities[otherIndex];
                                if (other->entityType == ENTITY_CHARACTER && other->character.characterType == CHARACTER_ENEMY) {
                                    if (CheckCollisionCircles(proj->position, explosionRadius, other->character.position, PLAYER_RADIUS)) {
                                        if (isLocalOwner) {
                                            Player_ApplyLifesteal(&globalVariables.currentConnectionState, otherIndex, explosionDamage, true, 1.0f);
                                            Render_SpawnDamagePopup(other->character.position, explosionDamage, YELLOW);
                                        }
                                        Network_SendDamage(&globalVariables.currentConnectionState, otherIndex, explosionDamage, isLocalOwner ? WEAPON_FIREBALL_RING : WEAPON_UNDEFINED); // DAMAGE_FIREBALL
                                        other->character.health -= explosionDamage; // Local Prediction
                                    }
                                }
                            }
                            
                            Network_SendProjectileExplode(&globalVariables.currentConnectionState, i);
                            entity->entityType = ENTITY_UNDEFINED;
                            break;
                        }
                    }
                }
            } else if (proj->type == PROJECTILE_CRYSTAL) {
                for (u16 enemyIndex = 0; enemyIndex < MAX_REMOTE_ENTITIES; enemyIndex++) {
                    Entity* remoteEntity = &globalVariables.currentConnectionState.remoteEntities[enemyIndex];
                    if (remoteEntity->entityType == ENTITY_CHARACTER && remoteEntity->character.characterType == CHARACTER_ENEMY) {
                        if (CheckCollisionCircles(proj->position, 10, remoteEntity->character.position, PLAYER_RADIUS)) {
                            // Penetration check
                            bool alreadyHit = false;
                            for (u8 h = 0; h < proj->hitCount; h++) {
                                if (proj->hitEnemies[h] == (u32)enemyIndex) { alreadyHit = true; break; }
                            }
                            if (!alreadyHit) {
                                f32 crystalDamage = 100.0f * ownerAttr->damage;
                                if (isLocalOwner) {
                                    Player_ApplyLifesteal(&globalVariables.currentConnectionState, enemyIndex, crystalDamage, false, 1.0f);
                                    Render_SpawnDamagePopup(remoteEntity->character.position, crystalDamage, YELLOW);
                                }
                                Network_SendDamage(&globalVariables.currentConnectionState, enemyIndex, crystalDamage, isLocalOwner ? WEAPON_CRYSTAL_STAFF : WEAPON_UNDEFINED); // DAMAGE_CRYSTAL
                                remoteEntity->character.health -= crystalDamage; // Local Prediction
                                if (proj->hitCount < 8) proj->hitEnemies[proj->hitCount++] = enemyIndex;
                            }
                        }
                    }
                }
            } else if (proj->type == PROJECTILE_BOMB) {
                if (proj->lifetime <= 0.0f) { // Explosion trigger (BOMB_DELAY=2.0)
                    f32 bombRadius = proj->radius;
                    f32 bombDamage = 500.0f * ownerAttr->damage;

                    // Prediction: Hide bomb and show local explosion instantly
                    for (u8 v = 0; v < 128; v++) {
                        if (!globalVariables.currentConnectionState.localVisualEffects[v].active) {
                            globalVariables.currentConnectionState.localVisualEffects[v].active = true;
                            globalVariables.currentConnectionState.localVisualEffects[v].position = proj->position;
                            globalVariables.currentConnectionState.localVisualEffects[v].radius = bombRadius;
                            globalVariables.currentConnectionState.localVisualEffects[v].lifetime = 0.5f;
                            break;
                        }
                    }
                
                    for (u16 otherIndex = 0; otherIndex < MAX_REMOTE_ENTITIES; otherIndex++) {
                        Entity* other = &globalVariables.currentConnectionState.remoteEntities[otherIndex];
                        if (other->entityType == ENTITY_CHARACTER && other->character.characterType == CHARACTER_ENEMY) {
                            if (CheckCollisionCircles(proj->position, bombRadius, other->character.position, PLAYER_RADIUS)) {
                                if (isLocalOwner) {
                                    Player_ApplyLifesteal(&globalVariables.currentConnectionState, otherIndex, bombDamage, true, 0.5f);
                                    Render_SpawnDamagePopup(other->character.position, bombDamage, YELLOW);
                                }
                                Network_SendDamage(&globalVariables.currentConnectionState, otherIndex, bombDamage, isLocalOwner ? WEAPON_BOMB_SHOES : WEAPON_UNDEFINED); // DAMAGE_BOMB
                                other->character.health -= bombDamage; // Local Prediction
                            }
                        }
                    }
                    entity->entityType = ENTITY_UNDEFINED;
                }
            } else if (proj->type == PROJECTILE_SPIKE) {
                // Spikes deal damage every 0.15s
                proj->tickTimer += deltaTime;
                if (proj->tickTimer >= 0.15f) {
                    f32 spikeRadius = SPIKE_RADIUS * proj->radius;
                    f32 spikeDamage = 20.0f * ownerAttr->damage;

                    for (u16 otherIndex = 0; otherIndex < MAX_REMOTE_ENTITIES; otherIndex++) {
                        Entity* other = &globalVariables.currentConnectionState.remoteEntities[otherIndex];
                        if (other->entityType == ENTITY_CHARACTER && other->character.characterType == CHARACTER_ENEMY) {
                            if (CheckCollisionCircles(proj->position, spikeRadius, other->character.position, PLAYER_RADIUS)) {
                                if (isLocalOwner) {
                                    Player_ApplyLifesteal(&globalVariables.currentConnectionState, otherIndex, spikeDamage, true, 1.0f);
                                    Render_SpawnDamagePopup(other->character.position, spikeDamage, YELLOW);
                                }
                                Network_SendDamage(&globalVariables.currentConnectionState, otherIndex, spikeDamage, isLocalOwner ? WEAPON_NATURE_SPIKES : WEAPON_UNDEFINED); // DAMAGE_SPIKE
                                other->character.health -= spikeDamage; // Local Prediction
                                proj->damageAccumulated += spikeDamage;
                            }
                        }
                    }
                    if (proj->damageAccumulated >= 150.0f * ownerAttr->damage) entity->entityType = ENTITY_UNDEFINED;
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

void Weapons_Update(f32 deltaTime) {
    if (!globalVariables.currentConnectionState.isConnected) return;
    if (globalVariables.currentInGameState == IN_GAME_SPECTATING || globalVariables.currentConnectionState.health <= 0.0f) return;

    u32 localIndex = (globalVariables.currentConnectionState.localPlayerIdentification - 1) % MAX_REMOTE_PLAYERS;
    PlayerAttributes* attr = &globalVariables.currentConnectionState.playerAttributes[localIndex];

    for (u8 i = 0; i < 4; i++) {
        Weapon* weapon = &globalVariables.playerWeapons[i];
        if (weapon->type == WEAPON_UNDEFINED) continue;
        
        weapon->cooldownTimer -= deltaTime;
        if (weapon->cooldownTimer <= 0) {
            f32 dmg = weapon->stats.damage * attr->damage;
            f32 rad = weapon->stats.size * attr->size;
            i32 extra = 0;
            
            if (weapon->type == WEAPON_CRYSTAL_STAFF) extra = weapon->stats.spec.crystalStaff.projectileAmount;
            else if (weapon->type == WEAPON_FIREBALL_RING) rad = weapon->stats.spec.fireball.explosionSize * attr->size;
            else if (weapon->type == WEAPON_NATURE_SPIKES) extra = weapon->stats.spec.natureSpikes.spikeAmount;
            
            Network_SendWeaponFire(&globalVariables.currentConnectionState, (u8)weapon->type, dmg, rad, extra);
            
            if (weapon->type == WEAPON_DEATH_AURA) {
                // Death Aura Logic: Check enemies in radius and deal damage
                u8 hitCount = 0;
                for (u16 enemyIndex = 0; enemyIndex < MAX_REMOTE_ENTITIES; enemyIndex++) {
                    Entity* enemy = &globalVariables.currentConnectionState.remoteEntities[enemyIndex];
                    if (enemy->entityType == ENTITY_CHARACTER && enemy->character.characterType == CHARACTER_ENEMY) {
                        if (CheckCollisionCircles(globalVariables.currentConnectionState.localPosition, rad, enemy->character.position, PLAYER_RADIUS)) {
                            Player_ApplyLifesteal(&globalVariables.currentConnectionState, enemyIndex, dmg, true, 0.5f);
                            Network_SendDamage(&globalVariables.currentConnectionState, enemyIndex, dmg, WEAPON_DEATH_AURA);
                            enemy->character.health -= dmg; // Local Prediction
                            Render_SpawnDamagePopup(enemy->character.position, dmg, YELLOW);
                            hitCount++;
                            if (hitCount >= 100) break;
                        }
                    }
                }
            }
            
            weapon->cooldownTimer = weapon->stats.attackSpeed / attr->attackSpeed;
        }
    }
}

//~ End of Weapons
