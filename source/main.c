#include "main.h"
#include "connection/connection.h"

// --- Global Variables ---
GlobalVariables globalVariables = { 0 };
InputState currentInputState = { 0 };
ConnectionState currentConnectionState = { 0 };
GameState currentGameState = STATE_MAIN_MENU;
InGameState currentInGameState = IN_GAME_PLAYING;

char playerNames[MAX_PLAYERS][32] = {
    "Player 1",
    "Player 2",
    "Player 3",
    "Player 4"
};
char myNameInput[32] = "Survivor";
char joinIpAddress[64] = "127.0.0.1";

f32 playerXP = 0.0f;
f32 xpToNextLevel = 100.0f;
u16 playerLevel = 1;
f32 gameTime = 0.0f;

u32 spectatedPlayerID = 0;

u32 FindNextAlivePlayer(u32 currentSpectatedID, bool forward) {
    for (int step = 1; step <= 4; step++) {
        int nextID = currentSpectatedID + (forward ? step : -step);
        while (nextID < 1) nextID += 4;
        while (nextID > 4) nextID -= 4;

        if (nextID == currentConnectionState.localPlayerIdentification) {
            if (currentConnectionState.health > 0.0f) {
                return nextID;
            }
        } else {
            Entity* ent = &currentConnectionState.remoteEntities[nextID];
            if (ent->entityType == ENTITY_CHARACTER &&
                ent->character.characterType == CHARACTER_PLAYER &&
                ent->character.health > 0.0f) {
                return nextID;
            }
        }
    }
    return currentSpectatedID;
}

void DrawHeart(Vector2 center, float size, Color color) {
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

void DrawTombstone(Vector2 pos, const char* name, Color nameColor) {
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

// --- Multiplayer & Lobby High-Fidelity UI Helpers ---

bool IsPlayerConnected(int playerID) {
    if (!currentConnectionState.isConnected) return false;
    if ((int)currentConnectionState.localPlayerIdentification == playerID) return true;
    if (playerID > 0 && playerID <= MAX_PLAYERS) {
        Entity* ent = &currentConnectionState.remoteEntities[playerID];
        if (ent->entityType == ENTITY_CHARACTER && ent->character.characterType == CHARACTER_PLAYER) {
            return true;
        }
    }
    return false;
}

typedef struct {
    Vector2 position;
    Vector2 velocity;
    float size;
    float alpha;
    Color color;
} MenuParticle;

#define MAX_MENU_PARTICLES 80
MenuParticle menuParticles[MAX_MENU_PARTICLES];
bool particlesInitialized = false;

void UpdateAndDrawMenuParticles(float deltaTime) {
    if (!particlesInitialized) {
        for (int i = 0; i < MAX_MENU_PARTICLES; i++) {
            menuParticles[i].position = (Vector2){ (float)(rand() % SCREEN_WIDTH), (float)(rand() % SCREEN_HEIGHT) };
            menuParticles[i].velocity = (Vector2){ (float)((rand() % 40) - 20) / 10.0f, (float)((rand() % 40) - 20) / 10.0f };
            menuParticles[i].size = (float)((rand() % 6) + 2);
            menuParticles[i].alpha = (float)(rand() % 100) / 100.0f * 0.4f + 0.1f;
            
            int colorIndex = rand() % 4;
            if (colorIndex == 0) menuParticles[i].color = (Color){ 120, 220, 255, 255 }; // Neo cyan
            else if (colorIndex == 1) menuParticles[i].color = (Color){ 255, 150, 200, 255 }; // Neo pink
            else if (colorIndex == 2) menuParticles[i].color = (Color){ 180, 150, 255, 255 }; // Neo violet
            else menuParticles[i].color = (Color){ 255, 230, 150, 255 }; // Neo gold
        }
        particlesInitialized = true;
    }
    
    for (int i = 0; i < MAX_MENU_PARTICLES; i++) {
        menuParticles[i].position.x += menuParticles[i].velocity.x * deltaTime * 10.0f;
        menuParticles[i].position.y += menuParticles[i].velocity.y * deltaTime * 10.0f;
        
        if (menuParticles[i].position.x < 0) menuParticles[i].position.x += SCREEN_WIDTH;
        if (menuParticles[i].position.x > SCREEN_WIDTH) menuParticles[i].position.x -= SCREEN_WIDTH;
        if (menuParticles[i].position.y < 0) menuParticles[i].position.y += SCREEN_HEIGHT;
        if (menuParticles[i].position.y > SCREEN_HEIGHT) menuParticles[i].position.y -= SCREEN_HEIGHT;
        
        float sizePulse = menuParticles[i].size + sinf(GetTime() + i) * 1.5f;
        if (sizePulse < 1.0f) sizePulse = 1.0f;
        
        DrawCircleV(menuParticles[i].position, sizePulse + 3.0f, Fade(menuParticles[i].color, menuParticles[i].alpha * 0.3f));
        DrawCircleV(menuParticles[i].position, sizePulse, Fade(menuParticles[i].color, menuParticles[i].alpha));
    }
}

bool DrawCustomButton(Rectangle rect, const char* text, Color baseColor, Color hoverColor, Vector2 mousePos, float deltaTime, float* animProgress) {
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

void DrawCustomTextBox(Rectangle rect, char* textBuffer, int maxLen, bool* active, const char* label, Vector2 mousePos) {
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

void DrawMainMenu(Vector2 mousePos, float deltaTime) {
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
    DrawCustomTextBox((Rectangle){ cardX + 50, cardY + 40, 300, 40 }, myNameInput, 31, &nameBoxActive, "YOUR NAME", mousePos);
    
    static float hostAnim = 0.0f;
    Rectangle hostRect = (Rectangle){ cardX + 50, cardY + 120, 300, 45 };
    if (DrawCustomButton(hostRect, "HOST GAME", Fade(DARKGREEN, 0.6f), Fade(LIME, 0.8f), mousePos, deltaTime, &hostAnim)) {
        printf("HOST: Spawning server...\n");
        system("start python server/server.py");
        
        double startTime = GetTime();
        while (GetTime() - startTime < 1.2) {
            // Wait to ensure Python server binds
        }
        
        if (Network_InitConnection(&currentConnectionState, "127.0.0.1")) {
            Network_SendNameUpdate(&currentConnectionState, myNameInput);
            currentGameState = STATE_LOBBY;
        } else {
            printf("HOST ERROR: Failed to connect to server.\n");
        }
    }
    
    static float joinAnim = 0.0f;
    Rectangle joinRect = (Rectangle){ cardX + 50, cardY + 185, 300, 45 };
    if (DrawCustomButton(joinRect, "JOIN GAME", Fade(BLUE, 0.6f), Fade(SKYBLUE, 0.8f), mousePos, deltaTime, &joinAnim)) {
        currentGameState = STATE_JOIN_IP;
    }
    
    static float exitAnim = 0.0f;
    Rectangle exitRect = (Rectangle){ cardX + 50, cardY + 250, 300, 45 };
    if (DrawCustomButton(exitRect, "EXIT TO DESKTOP", Fade(DARKGRAY, 0.6f), Fade(RED, 0.8f), mousePos, deltaTime, &exitAnim)) {
        currentInputState.quitApplication = true;
    }
}

void DrawJoinInputScreen(Vector2 mousePos, float deltaTime) {
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
    DrawCustomTextBox((Rectangle){ cardX + 50, cardY + 40, 350, 40 }, myNameInput, 31, &nameBoxActive, "YOUR NAME", mousePos);
    
    static bool ipBoxActive = false;
    DrawCustomTextBox((Rectangle){ cardX + 50, cardY + 115, 350, 40 }, joinIpAddress, 63, &ipBoxActive, "SERVER IP ADDRESS", mousePos);
    
    static float connectAnim = 0.0f;
    Rectangle connectRect = (Rectangle){ cardX + 50, cardY + 185, 350, 45 };
    if (DrawCustomButton(connectRect, "CONNECT", Fade(SKYBLUE, 0.6f), Fade(BLUE, 0.8f), mousePos, deltaTime, &connectAnim)) {
        printf("JOIN: Connecting to %s...\n", joinIpAddress);
        if (Network_InitConnection(&currentConnectionState, joinIpAddress)) {
            Network_SendNameUpdate(&currentConnectionState, myNameInput);
            currentGameState = STATE_LOBBY;
        } else {
            printf("JOIN ERROR: Failed to connect to server at %s.\n", joinIpAddress);
        }
    }
    
    static float backAnim = 0.0f;
    Rectangle backRect = (Rectangle){ cardX + 50, cardY + 245, 350, 45 };
    if (DrawCustomButton(backRect, "BACK", Fade(DARKGRAY, 0.6f), Fade(GRAY, 0.8f), mousePos, deltaTime, &backAnim)) {
        currentGameState = STATE_MAIN_MENU;
    }
}

void DrawLobby(Vector2 mousePos, float deltaTime) {
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
    
    for (int i = 1; i <= 4; i++) {
        float slotX = cardX + 40.0f;
        float slotY = cardY + 25.0f + (i - 1) * (65.0f + 10.0f);
        float slotWidth = cardWidth - 80.0f;
        float slotHeight = 65.0f;
        
        Rectangle slotRec = (Rectangle){ slotX, slotY, slotWidth, slotHeight };
        
        if (IsPlayerConnected(i)) {
            DrawRectangleRounded(slotRec, 0.15f, 4, Fade(BLACK, 0.45f));
            DrawRectangleRoundedLines(slotRec, 0.15f, 4, Fade(WHITE, 0.2f));
            
            Color iconCol = RED;
            if (i == 1) iconCol = GOLD;
            else if (i == (int)currentConnectionState.localPlayerIdentification) iconCol = SKYBLUE;
            
            DrawCircle(slotX + 35, slotY + slotHeight/2.0f, 15, iconCol);
            
            const char* letter = (i == 1) ? "H" : TextFormat("%d", i);
            int letterWidth = MeasureText(letter, 12);
            DrawText(letter, slotX + 35 - letterWidth/2.0f, slotY + slotHeight/2.0f - 6, 12, BLACK);
            
            DrawText(playerNames[i - 1], slotX + 70, slotY + 22, 20, WHITE);
            
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
    strcpy(tempName, myNameInput);
    
    DrawCustomTextBox((Rectangle){ cardX + 40, cardY + cardHeight - 80, 300, 40 }, myNameInput, 31, &lobbyNameActive, "CHANGE YOUR NAME", mousePos);
    
    if (strcmp(tempName, myNameInput) != 0) {
        Network_SendNameUpdate(&currentConnectionState, myNameInput);
        u32 localID = currentConnectionState.localPlayerIdentification;
        if (localID > 0 && localID <= MAX_PLAYERS) {
            strncpy(playerNames[localID - 1], myNameInput, 31);
            playerNames[localID - 1][31] = '\0';
        }
    }
    
    if (currentConnectionState.localPlayerIdentification == 1) {
        static float startBtnAnim = 0.0f;
        Rectangle startBtnRect = (Rectangle){ cardX + cardWidth - 220, cardY + cardHeight - 80, 180, 40 };
        
        if (DrawCustomButton(startBtnRect, "START GAME", (Color){ 245, 130, 48, 255 }, (Color){ 253, 191, 111, 255 }, mousePos, deltaTime, &startBtnAnim)) {
            Network_SendStartGame(&currentConnectionState);
        }
    } else {
        float pulseAlpha = 0.5f + 0.3f * sinf(GetTime() * 4.0f);
        DrawRectangleRounded((Rectangle){ cardX + cardWidth - 250, cardY + cardHeight - 80, 210, 40 }, 0.2f, 4, Fade(BLACK, 0.4f));
        DrawRectangleRoundedLines((Rectangle){ cardX + cardWidth - 250, cardY + cardHeight - 80, 210, 40 }, 0.2f, 4, Fade(WHITE, 0.15f));
        
        int textWidth = MeasureText("Waiting for Host...", 14);
        DrawText("Waiting for Host...", cardX + cardWidth - 250 + (210 - textWidth) / 2.0f, cardY + cardHeight - 80 + 13, 14, Fade(GOLD, pulseAlpha));
    }
}

// --- Main Entry Point ---
int main(void) {
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Arcade Survivors Online");



    // Initialize player weapons and relics - Start with 1 random weapon
    for (int i = 0; i < 4; i++) {
        globalVariables.playerWeapons[i].type = WEAPON_UNDEFINED;
        globalVariables.playerRelics[i].type = RELIC_UNDEFINED;
        globalVariables.playerRelics[i].level = 0;
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
        if (currentGameState == STATE_LOBBY || currentGameState == STATE_IN_GAME) {
            Network_UpdateConnection(&currentConnectionState);
        }
        Input_Update(&currentInputState);

        f32 deltaTime = GetFrameTime();
        Vector2 mousePos = GetMousePosition();
        
        if (currentGameState == STATE_IN_GAME) {
            if (currentConnectionState.isConnected) {
                gameTime += deltaTime;
            } else {
                gameTime = 0.0f;
            }

            // Update active notification
            if (currentConnectionState.notificationCount > 0) {
                ClientNotification* activeNotif = &currentConnectionState.notificationQueue[0];
                activeNotif->timeElapsed += deltaTime;
                if (activeNotif->timeElapsed >= activeNotif->duration) {
                    // Shift notifications queue forward
                    for (int i = 0; i < currentConnectionState.notificationCount - 1; i++) {
                        currentConnectionState.notificationQueue[i] = currentConnectionState.notificationQueue[i + 1];
                    }
                    currentConnectionState.notificationCount--;
                    if (currentConnectionState.notificationCount > 0) {
                        currentConnectionState.notificationQueue[0].timeElapsed = 0.0f;
                    }
                }
            }

            // Predict and Interpolate movement and count down visual timers for characters
            for (i32 entityIndex = 0; entityIndex < MAX_REMOTE_ENTITIES; entityIndex++) {
                Entity* entity = &currentConnectionState.remoteEntities[entityIndex];
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
            if (currentConnectionState.damageFlashTimer > 0) {
                currentConnectionState.damageFlashTimer -= deltaTime;
                if (currentConnectionState.damageFlashTimer < 0) currentConnectionState.damageFlashTimer = 0;
            }
            if (currentConnectionState.iframeTimer > 0) {
                currentConnectionState.iframeTimer -= deltaTime;
                if (currentConnectionState.iframeTimer < 0) currentConnectionState.iframeTimer = 0;
            }
            if (currentConnectionState.isConnected) {
                currentConnectionState.gameTime += deltaTime;
            }

            Enemy_UpdateMovement(deltaTime);
            Player_UpdateMovement(deltaTime);
            Weapons_Update(deltaTime);
            Projectile_UpdateMovement(deltaTime);
            Network_SendDeathReport(&currentConnectionState);
            Network_SendDamageBatch(&currentConnectionState);

            // Update XP Crystals (Magnetization and Collection)
            if (currentConnectionState.health > 0.0f && currentInGameState != IN_GAME_SPECTATING) {
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
                            u32 localIndex = (currentConnectionState.localPlayerIdentification - 1) % MAX_REMOTE_PLAYERS;
                            PlayerAttributes* attr = &currentConnectionState.playerAttributes[localIndex];
                            
                            playerXP += entity->xpCrystal.xpValue * attr->xpGained;
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
            }

            // Trigger Upgrade Menu if pending levels
            if (pendingLevels > 0 && !isChoosingUpgrade && currentInGameState != IN_GAME_SPECTATING) {
                GenerateUpgradeOptions(upgradeOptions);
                isChoosingUpgrade = true;
                pendingLevels--;
            }

            bool isSpectating = (currentConnectionState.health <= 0.0f && currentConnectionState.teamLives <= 0);
            if (isSpectating) {
                currentInGameState = IN_GAME_SPECTATING;
                isChoosingUpgrade = false;
                pendingLevels = 0;
            } else {
                currentInGameState = IN_GAME_PLAYING;
            }

            if (currentInGameState == IN_GAME_SPECTATING) {
                bool currentSpectatedIsAlive = false;
                if (spectatedPlayerID == currentConnectionState.localPlayerIdentification) {
                    if (currentConnectionState.health > 0.0f) currentSpectatedIsAlive = true;
                } else {
                    Entity* ent = &currentConnectionState.remoteEntities[spectatedPlayerID];
                    if (ent->entityType == ENTITY_CHARACTER &&
                        ent->character.characterType == CHARACTER_PLAYER &&
                        ent->character.health > 0.0f) {
                        currentSpectatedIsAlive = true;
                    }
                }

                if (!currentSpectatedIsAlive || spectatedPlayerID == 0) {
                    spectatedPlayerID = FindNextAlivePlayer(currentConnectionState.localPlayerIdentification, true);
                }

                Vector2 targetPos = currentConnectionState.localPosition;
                if (spectatedPlayerID == currentConnectionState.localPlayerIdentification) {
                    targetPos = currentConnectionState.localPosition;
                } else {
                    Entity* ent = &currentConnectionState.remoteEntities[spectatedPlayerID];
                    if (ent->entityType == ENTITY_CHARACTER && ent->character.characterType == CHARACTER_PLAYER) {
                        targetPos = ent->character.position;
                    }
                }
                camera.target = targetPos;
            } else {
                camera.target = currentConnectionState.localPosition;
                spectatedPlayerID = currentConnectionState.localPlayerIdentification;
            }
        }

        BeginDrawing();
            if (currentGameState == STATE_IN_GAME) {
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

                    // Update and Render Local Damage Popups
                    for (int i = 0; i < 256; i++) {
                        Entity* popupEnt = &currentConnectionState.localDamagePopups[i];
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

                    for (i32 entityIndex = 0; entityIndex < MAX_REMOTE_ENTITIES; entityIndex++) {
                        const Entity* entity = &currentConnectionState.remoteEntities[entityIndex];
                        if (entity->entityType == ENTITY_CHARACTER && entity->character.characterType == CHARACTER_PLAYER) {
                            // Draw Death Aura for remote players if alive
                            if (entity->character.health > 0.0f && (entity->character.weaponsMask & (1 << (WEAPON_DEATH_AURA - 1)))) {
                                u32 remoteID = entityIndex;
                                u32 remoteIndex = (remoteID - 1) % MAX_REMOTE_PLAYERS;
                                f32 remoteSizeMult = currentConnectionState.playerAttributes[remoteIndex].size;
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

                    if (currentConnectionState.isConnected) {
                        u32 localID = currentConnectionState.localPlayerIdentification;
                        u32 idx = (localID - 1) % MAX_PLAYERS;
                        if (currentConnectionState.health <= 0.0f) {
                            DrawTombstone(currentConnectionState.localPosition, playerNames[idx], BLUE);
                        } else {
                            DrawCircleV(currentConnectionState.localPosition, PLAYER_RADIUS, BLUE);
                            
                            // Draw local player high-frequency pulse damage flash if timer is active
                            if (currentConnectionState.damageFlashTimer > 0) {
                                f32 flashAlpha = (sinf(currentConnectionState.damageFlashTimer * 75.0f) > 0.0f) ? 0.7f : 0.0f;
                                if (flashAlpha > 0.0f) {
                                    DrawCircleV(currentConnectionState.localPosition, PLAYER_RADIUS, Fade(WHITE, flashAlpha));
                                }
                            }
                            
                            int textWidth = MeasureText(playerNames[idx], 12);
                            DrawText(playerNames[idx], currentConnectionState.localPosition.x - textWidth / 2, currentConnectionState.localPosition.y - 40, 12, BLUE);
                            
                            // Draw local player's death aura
                            u32 localIndex = (currentConnectionState.localPlayerIdentification - 1) % MAX_REMOTE_PLAYERS;
                            PlayerAttributes* attr = &currentConnectionState.playerAttributes[localIndex];
                            for (int i = 0; i < 4; i++) {
                                if (globalVariables.playerWeapons[i].type == WEAPON_DEATH_AURA) {
                                    f32 currentAuraRadius = globalVariables.playerWeapons[i].stats.size * attr->size;
                                    DrawCircleLinesV(currentConnectionState.localPosition, currentAuraRadius, Fade(BLACK, 0.3f));
                                    DrawCircleV(currentConnectionState.localPosition, currentAuraRadius, Fade(BLACK, 0.1f));
                                    break;
                                }
                            }
                        }
                    } else {
                        DrawText("Searching for server...", currentConnectionState.localPosition.x - 60, currentConnectionState.localPosition.y, 20, WHITE);
                    }
                EndMode2D();
                
                // UI Overlay
                DrawXPBar();
                DrawGameTimer();
                if (isChoosingUpgrade) DrawUpgradeCards();
                if (IsKeyDown(KEY_TAB)) DrawStatsOverlay();

                // Draw glassmorphic spectator panel at bottom center if spectating
                if (currentInGameState == IN_GAME_SPECTATING) {
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
                    u32 sIdx = (spectatedPlayerID - 1) % MAX_PLAYERS;
                    const char* targetName = playerNames[sIdx];
                    if (spectatedPlayerID == currentConnectionState.localPlayerIdentification) {
                        targetName = TextFormat("%s (YOU)", playerNames[sIdx]);
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
                if (currentConnectionState.notificationCount > 0) {
                    ClientNotification* activeNotif = &currentConnectionState.notificationQueue[0];
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
                
                if (!currentConnectionState.isConnected) {
                    DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(BLACK, 0.6f));
                    DrawText("CONNECTING TO SERVER...", SCREEN_WIDTH/2 - 150, SCREEN_HEIGHT/2, 20, WHITE);
                    DrawText(TextFormat("Target IP: %s:%d", joinIpAddress, SERVER_PORT), SCREEN_WIDTH/2 - 100, SCREEN_HEIGHT/2 + 40, 10, GRAY);
                } else {
                    DrawText("CONNECTED", 10, 30, 20, GREEN);
                    
                    // Draw local health bar
                    f32 healthPercent = currentConnectionState.health / currentConnectionState.maxHealth;
                    if (healthPercent < 0.0f) healthPercent = 0.0f;
                    DrawRectangle(10, 60, 200, 20, DARKGRAY);
                    DrawRectangle(10, 60, (int)(200 * healthPercent), 20, RED);
                    DrawRectangleLines(10, 60, 200, 20, BLACK);
                    DrawText(TextFormat("%.0f / %.0f", currentConnectionState.health, currentConnectionState.maxHealth), 70, 65, 10, WHITE);
                    
                    // Draw lives counter next to the health bar
                    DrawHeart((Vector2){ 235, 70 }, 10, RED);
                    DrawText(TextFormat("x %d", currentConnectionState.teamLives), 255, 62, 16, WHITE);
                }
            } else {
                ClearBackground((Color){ 10, 12, 18, 255 });
                UpdateAndDrawMenuParticles(deltaTime);
                
                if (currentGameState == STATE_MAIN_MENU) {
                    DrawMainMenu(mousePos, deltaTime);
                } else if (currentGameState == STATE_JOIN_IP) {
                    DrawJoinInputScreen(mousePos, deltaTime);
                } else if (currentGameState == STATE_LOBBY) {
                    DrawLobby(mousePos, deltaTime);
                }
                
                DrawFPS(10, 10);
            }
        EndDrawing();
    }

    Network_CloseConnection();
    CloseWindow();
    return 0;
}

// --- Enemy Implementation ---
u32 GetAlternativeTargetPlayerID(u32 deadPlayerID, u32 enemyIndex) {
    u32 alivePlayers[4];
    u32 aliveCount = 0;
    
    // Check if local player is alive
    if (currentConnectionState.isConnected && currentConnectionState.health > 0.0f) {
        alivePlayers[aliveCount++] = currentConnectionState.localPlayerIdentification;
    }
    
    // Check if remote players are alive
    for (u32 i = 1; i <= 4; i++) {
        if (i == currentConnectionState.localPlayerIdentification) continue;
        
        Entity* remotePlayer = &currentConnectionState.remoteEntities[i];
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
    for (i32 entityIndex = 0; entityIndex < MAX_REMOTE_ENTITIES; entityIndex++) {
        Entity* entity = &currentConnectionState.remoteEntities[entityIndex];
        if (entity->entityType == ENTITY_CHARACTER && entity->character.characterType == CHARACTER_ENEMY) {
            
            // 1. Calculate direction to target player
            Vector2 targetPosition = entity->character.position; // Default to staying put
            bool targetFound = false;

            if (entity->character.targetPlayerID != 0) {
                // If target player is dead, select alternative alive player deterministically
                u32 currentTargetID = entity->character.targetPlayerID;
                bool targetIsDead = false;
                
                if (currentTargetID == currentConnectionState.localPlayerIdentification) {
                    if (currentConnectionState.health <= 0.0f) {
                        targetIsDead = true;
                    }
                } else {
                    u32 targetIndex = currentTargetID % MAX_REMOTE_ENTITIES;
                    Entity* targetPlayer = &currentConnectionState.remoteEntities[targetIndex];
                    if (targetPlayer->entityType == ENTITY_CHARACTER && 
                        targetPlayer->character.characterType == CHARACTER_PLAYER && 
                        targetPlayer->character.health <= 0.0f) {
                        targetIsDead = true;
                    }
                }
                
                if (targetIsDead) {
                    entity->character.targetPlayerID = GetAlternativeTargetPlayerID(currentTargetID, entityIndex);
                }

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
    if (currentInGameState == IN_GAME_SPECTATING || currentConnectionState.health <= 0.0f) return;

    u32 localIndex = (currentConnectionState.localPlayerIdentification - 1) % MAX_REMOTE_PLAYERS;
    PlayerAttributes* attr = &currentConnectionState.playerAttributes[localIndex];

    for (i32 i = 0; i < 4; i++) {
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
            
            Network_SendWeaponFire(&currentConnectionState, (u8)weapon->type, dmg, rad, extra);
            
            if (weapon->type == WEAPON_DEATH_AURA) {
                // Death Aura Logic: Check enemies in radius and deal damage
                int hitCount = 0;
                for (i32 enemyIndex = 0; enemyIndex < MAX_REMOTE_ENTITIES; enemyIndex++) {
                    Entity* enemy = &currentConnectionState.remoteEntities[enemyIndex];
                    if (enemy->entityType == ENTITY_CHARACTER && enemy->character.characterType == CHARACTER_ENEMY) {
                        if (CheckCollisionCircles(currentConnectionState.localPosition, rad, enemy->character.position, PLAYER_RADIUS)) {
                            ApplyLifesteal(&currentConnectionState, enemyIndex, dmg, true, 0.5f);
                            Network_SendDamage(&currentConnectionState, enemyIndex, dmg, WEAPON_DEATH_AURA);
                            enemy->character.health -= dmg; // Local Prediction
                            SpawnDamagePopup(enemy->character.position, dmg, YELLOW);
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

            u32 ownerIndex = (proj->ownerID - 1) % MAX_REMOTE_PLAYERS;
            PlayerAttributes* ownerAttr = &currentConnectionState.playerAttributes[ownerIndex];
            bool isLocalOwner = (proj->ownerID == currentConnectionState.localPlayerIdentification);

            // Collision check with enemies (Client prediction)
            if (proj->type == PROJECTILE_FIREBALL) {
                for (i32 enemyIndex = 0; enemyIndex < MAX_REMOTE_ENTITIES; enemyIndex++) {
                    Entity* remoteEntity = &currentConnectionState.remoteEntities[enemyIndex];
                    if (remoteEntity->entityType == ENTITY_CHARACTER && remoteEntity->character.characterType == CHARACTER_ENEMY) {
                        if (CheckCollisionCircles(proj->position, 10, remoteEntity->character.position, PLAYER_RADIUS)) {
                            // Prediction: Hide fireball and show local explosion instantly
                            f32 explosionRadius = proj->radius;
                            f32 explosionDamage = 50.0f * ownerAttr->damage;

                            for (int v = 0; v < 128; v++) {
                                if (!currentConnectionState.localVisualEffects[v].active) {
                                    currentConnectionState.localVisualEffects[v].active = true;
                                    currentConnectionState.localVisualEffects[v].position = proj->position;
                                    currentConnectionState.localVisualEffects[v].radius = explosionRadius;
                                    currentConnectionState.localVisualEffects[v].lifetime = 0.5f;
                                    break;
                                }
                            }
                            
                            // Explosion damage report
                            for (i32 otherIndex = 0; otherIndex < MAX_REMOTE_ENTITIES; otherIndex++) {
                                Entity* other = &currentConnectionState.remoteEntities[otherIndex];
                                if (other->entityType == ENTITY_CHARACTER && other->character.characterType == CHARACTER_ENEMY) {
                                    if (CheckCollisionCircles(proj->position, explosionRadius, other->character.position, PLAYER_RADIUS)) {
                                        if (isLocalOwner) {
                                            ApplyLifesteal(&currentConnectionState, otherIndex, explosionDamage, true, 1.0f);
                                            SpawnDamagePopup(other->character.position, explosionDamage, YELLOW);
                                        }
                                        Network_SendDamage(&currentConnectionState, otherIndex, explosionDamage, isLocalOwner ? WEAPON_FIREBALL_RING : WEAPON_UNDEFINED); // DAMAGE_FIREBALL
                                        other->character.health -= explosionDamage; // Local Prediction
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
                                f32 crystalDamage = 100.0f * ownerAttr->damage;
                                if (isLocalOwner) {
                                    ApplyLifesteal(&currentConnectionState, enemyIndex, crystalDamage, false, 1.0f);
                                    SpawnDamagePopup(remoteEntity->character.position, crystalDamage, YELLOW);
                                }
                                Network_SendDamage(&currentConnectionState, enemyIndex, crystalDamage, isLocalOwner ? WEAPON_CRYSTAL_STAFF : WEAPON_UNDEFINED); // DAMAGE_CRYSTAL
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
                    for (int v = 0; v < 128; v++) {
                        if (!currentConnectionState.localVisualEffects[v].active) {
                            currentConnectionState.localVisualEffects[v].active = true;
                            currentConnectionState.localVisualEffects[v].position = proj->position;
                            currentConnectionState.localVisualEffects[v].radius = bombRadius;
                            currentConnectionState.localVisualEffects[v].lifetime = 0.5f;
                            break;
                        }
                    }
                
                    for (i32 otherIndex = 0; otherIndex < MAX_REMOTE_ENTITIES; otherIndex++) {
                        Entity* other = &currentConnectionState.remoteEntities[otherIndex];
                        if (other->entityType == ENTITY_CHARACTER && other->character.characterType == CHARACTER_ENEMY) {
                            if (CheckCollisionCircles(proj->position, bombRadius, other->character.position, PLAYER_RADIUS)) {
                                if (isLocalOwner) {
                                    ApplyLifesteal(&currentConnectionState, otherIndex, bombDamage, true, 0.5f);
                                    SpawnDamagePopup(other->character.position, bombDamage, YELLOW);
                                }
                                Network_SendDamage(&currentConnectionState, otherIndex, bombDamage, isLocalOwner ? WEAPON_BOMB_SHOES : WEAPON_UNDEFINED); // DAMAGE_BOMB
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

                    for (i32 otherIndex = 0; otherIndex < MAX_REMOTE_ENTITIES; otherIndex++) {
                        Entity* other = &currentConnectionState.remoteEntities[otherIndex];
                        if (other->entityType == ENTITY_CHARACTER && other->character.characterType == CHARACTER_ENEMY) {
                            if (CheckCollisionCircles(proj->position, spikeRadius, other->character.position, PLAYER_RADIUS)) {
                                if (isLocalOwner) {
                                    ApplyLifesteal(&currentConnectionState, otherIndex, spikeDamage, true, 1.0f);
                                    SpawnDamagePopup(other->character.position, spikeDamage, YELLOW);
                                }
                                Network_SendDamage(&currentConnectionState, otherIndex, spikeDamage, isLocalOwner ? WEAPON_NATURE_SPIKES : WEAPON_UNDEFINED); // DAMAGE_SPIKE
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

void Weapon_FireFireballRing(Vector2 position, u32 ownerID) {
    // Deprecated: Now handled by server via Network_SendWeaponFire
}

void SpawnDamagePopup(Vector2 position, f32 damage, Color color) {
    for (int i = 0; i < 256; i++) {
        if (currentConnectionState.localDamagePopups[i].entityType == ENTITY_UNDEFINED) {
            f32 offsetX = (f32)(rand() % 31 - 15);
            f32 offsetY = (f32)(rand() % 31 - 15);
            
            currentConnectionState.localDamagePopups[i].entityType = ENTITY_DAMAGE_POPUP;
            currentConnectionState.localDamagePopups[i].damagePopup.position = (Vector2){ position.x + offsetX, position.y + offsetY };
            currentConnectionState.localDamagePopups[i].damagePopup.damageValue = damage;
            currentConnectionState.localDamagePopups[i].damagePopup.lifetime = 0.0f;
            currentConnectionState.localDamagePopups[i].damagePopup.color = color;
            break;
        }
    }
}

// --- Player Implementation ---
void Player_UpdateMovement(f32 deltaTime) {
    if (!Network_IsConnected(&currentConnectionState)) return;

    if (currentInGameState == IN_GAME_SPECTATING) {
        Network_SendVelocity(&currentConnectionState, (Vector2){ 0, 0 });
        return;
    }

    // Check for predicted local respawn when health <= 0
    if (currentConnectionState.health <= 0.0f) {
        if (currentConnectionState.teamLives > 0) {
            currentConnectionState.health = currentConnectionState.maxHealth;
            currentConnectionState.localPosition = (Vector2){ 0, 0 };
            currentConnectionState.iframeTimer = 2.0f; // 2 seconds spawn protection
            currentConnectionState.damageFlashTimer = 2.0f; // 2 seconds spawn protection visual damage flash
            
            // Predict life decrement
            currentConnectionState.teamLives--;

            // Notify server of respawn position
            Network_SendVelocity(&currentConnectionState, (Vector2){ 0, 0 });
            printf("[DEATH] Player died! Predicted local respawn at spawn (0, 0)...\n");
        } else {
            // Out of lives! Send zero velocity/position to server
            Network_SendVelocity(&currentConnectionState, (Vector2){ 0, 0 });
            return;
        }
    }

    u32 localIndex = (currentConnectionState.localPlayerIdentification - 1) % MAX_REMOTE_PLAYERS;
    PlayerAttributes* attr = &currentConnectionState.playerAttributes[localIndex];

    Vector2 movementVelocity = (Vector2){ 0, 0 };
    movementVelocity.x = currentInputState.movementDirection.x * PLAYER_SPEED * attr->movementSpeed;
    movementVelocity.y = currentInputState.movementDirection.y * PLAYER_SPEED * attr->movementSpeed;

    currentConnectionState.localPosition.x += movementVelocity.x * deltaTime;
    currentConnectionState.localPosition.y += movementVelocity.y * deltaTime;
    
    // Cap player position within map boundaries (including player radius)
    float mapLimit = MAP_SIZE / 2.0f - PLAYER_RADIUS;
    if (currentConnectionState.localPosition.x < -mapLimit) currentConnectionState.localPosition.x = -mapLimit;
    if (currentConnectionState.localPosition.x > mapLimit) currentConnectionState.localPosition.x = mapLimit;
    if (currentConnectionState.localPosition.y < -mapLimit) currentConnectionState.localPosition.y = -mapLimit;
    if (currentConnectionState.localPosition.y > mapLimit) currentConnectionState.localPosition.y = mapLimit;
    
    // Local player-enemy collision damage prediction
    if (currentConnectionState.iframeTimer <= 0) {
        for (i32 enemyIndex = 0; enemyIndex < MAX_REMOTE_ENTITIES; enemyIndex++) {
            Entity* enemy = &currentConnectionState.remoteEntities[enemyIndex];
            if (enemy->entityType == ENTITY_CHARACTER && 
                enemy->character.characterType == CHARACTER_ENEMY && 
                enemy->character.health > 0) {
                
                if (CheckCollisionCircles(currentConnectionState.localPosition, PLAYER_RADIUS, enemy->character.position, PLAYER_RADIUS)) {
                    f32 clientDifficulty = gameTime / 6.0f;
                    f32 stat_mult = 1.0f + (clientDifficulty / 20.0f) * 1.25f;
                    
                    f32 baseDamage = 10.0f;
                    if (enemy->character.enemyClass == ENEMY_CLASS_BOSS) {
                        baseDamage = 40.0f;
                    }
                    f32 predictedDamage = baseDamage * stat_mult;
                    
                    currentConnectionState.health -= predictedDamage;
                    currentConnectionState.iframeTimer = 0.5f;
                    currentConnectionState.damageFlashTimer = 0.5f;
                    
                    Network_SendDamage(&currentConnectionState, currentConnectionState.localPlayerIdentification, predictedDamage, WEAPON_UNDEFINED);
                    break;
                }
            }
        }
    }
    
    Network_SendVelocity(&currentConnectionState, movementVelocity);
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

    for (int i = 0; i < 4; i++) {
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
    f32 healthDiff = attr.maxHealth - currentConnectionState.maxHealth;
    if (healthDiff != 0.0f) {
        currentConnectionState.health += healthDiff;
    }

    Player_UpdateAttributes(&currentConnectionState, attr);
}

void ApplyLifesteal(ConnectionState* state, u32 enemyIndex, f32 damage, bool isAoE, f32 weaponMult) {
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

// --- Input System Implementation ---
void Input_Update(InputState* state) {
    state->movementDirection = (Vector2){ 0, 0 };
    
    if (currentGameState == STATE_IN_GAME) {
        if (currentInGameState == IN_GAME_SPECTATING) {
            if (IsKeyPressed(KEY_A) || IsKeyPressed(KEY_LEFT)) {
                spectatedPlayerID = FindNextAlivePlayer(spectatedPlayerID, false);
            }
            if (IsKeyPressed(KEY_D) || IsKeyPressed(KEY_RIGHT)) {
                spectatedPlayerID = FindNextAlivePlayer(spectatedPlayerID, true);
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
    if (isChoosingUpgrade) {
        if (IsKeyPressed(KEY_ONE) && upgradeOptions[0].type != 0) { ApplyUpgrade(0); isChoosingUpgrade = false; }
        if (IsKeyPressed(KEY_TWO) && upgradeOptions[1].type != 0) { ApplyUpgrade(1); isChoosingUpgrade = false; }
        if (IsKeyPressed(KEY_THREE) && upgradeOptions[2].type != 0) { ApplyUpgrade(2); isChoosingUpgrade = false; }
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
                u32 playerID = (u32)(entity - currentConnectionState.remoteEntities);
                u32 idx = (playerID - 1) % MAX_PLAYERS;
                const char* displayName = playerNames[idx];
                if (entity->character.health <= 0.0f) {
                    DrawTombstone(entity->character.position, displayName, MAROON);
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
                f32 radius = PLAYER_RADIUS;
                Color baseColor = PURPLE;
                const char* labelText = "ENEMY";
                int barWidth = 40;
                
                if (entity->character.enemyClass == ENEMY_CLASS_FAST) {
                    radius = PLAYER_RADIUS * 0.75f;
                    baseColor = ORANGE;
                    labelText = "FAST";
                    barWidth = 30;
                } else if (entity->character.enemyClass == ENEMY_CLASS_TANK) {
                    radius = PLAYER_RADIUS * 1.5f;
                    baseColor = DARKPURPLE;
                    labelText = "TANK";
                    barWidth = 60;
                } else if (entity->character.enemyClass == ENEMY_CLASS_BOSS) {
                    radius = PLAYER_RADIUS * 2.5f;
                    baseColor = MAROON;
                    labelText = "BOSS";
                    barWidth = 100;
                }
                
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

void DrawGameTimer(void) {
    if (!currentConnectionState.isConnected) return;
    
    f32 elapsed = currentConnectionState.gameTime;
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

typedef struct UpgradeCandidate {
    bool isRelic;
    u8 type;
} UpgradeCandidate;

void GenerateUpgradeOptions(LevelUpOption options[3]) {
    UpgradeCandidate candidates[12];
    int candidateCount = 0;

    // Count owned weapons and relics
    int ownedWeaponsCount = 0;
    for (int i = 0; i < 4; i++) {
        if (globalVariables.playerWeapons[i].type != WEAPON_UNDEFINED) {
            ownedWeaponsCount++;
        }
    }

    int ownedRelicsCount = 0;
    for (int i = 0; i < 4; i++) {
        if (globalVariables.playerRelics[i].type != RELIC_UNDEFINED) {
            ownedRelicsCount++;
        }
    }

    // 1. Gather Weapon candidates
    for (int wType = 1; wType <= 5; wType++) {
        int ownedIndex = -1;
        for (int i = 0; i < 4; i++) {
            if (globalVariables.playerWeapons[i].type == wType) {
                ownedIndex = i;
                break;
            }
        }
        
        if (ownedIndex != -1) {
            // Owned: can upgrade if level < 15
            if (globalVariables.playerWeapons[ownedIndex].level < 15) {
                candidates[candidateCount++] = (UpgradeCandidate){ .isRelic = false, .type = (u8)wType };
            }
        } else {
            // Not owned: can acquire if inventory not full
            if (ownedWeaponsCount < 4) {
                candidates[candidateCount++] = (UpgradeCandidate){ .isRelic = false, .type = (u8)wType };
            }
        }
    }

    // 2. Gather Relic candidates
    for (int rType = 1; rType <= 7; rType++) {
        int ownedIndex = -1;
        for (int i = 0; i < 4; i++) {
            if (globalVariables.playerRelics[i].type == rType) {
                ownedIndex = i;
                break;
            }
        }
        
        if (ownedIndex != -1) {
            // Owned: can upgrade if level < 5
            if (globalVariables.playerRelics[ownedIndex].level < 5) {
                candidates[candidateCount++] = (UpgradeCandidate){ .isRelic = true, .type = (u8)rType };
            }
        } else {
            // Not owned: can acquire if inventory not full
            if (ownedRelicsCount < 4) {
                candidates[candidateCount++] = (UpgradeCandidate){ .isRelic = true, .type = (u8)rType };
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
    for (int i = 0; i < 3; i++) {
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

void ApplyUpgrade(int optionIndex) {
    LevelUpOption option = upgradeOptions[optionIndex];
    if (option.type == 0) return;

    u8 newLevel = 0;
    if (option.isRelic) {
        RelicType type = (RelicType)option.type;
        // Check if we already have it
        for (int i = 0; i < 4; i++) {
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
            for (int i = 0; i < 4; i++) {
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
        for (int i = 0; i < 4; i++) {
            if (globalVariables.playerWeapons[i].type == type) {
                Weapon_Upgrade(&globalVariables.playerWeapons[i]);
                newLevel = globalVariables.playerWeapons[i].level;
                break;
            }
        }
        
        if (newLevel == 0) {
            // Find empty slot
            for (int i = 0; i < 4; i++) {
                if (globalVariables.playerWeapons[i].type == WEAPON_UNDEFINED) {
                    Weapon_Initialize(&globalVariables.playerWeapons[i], type);
                    newLevel = 1;
                    break;
                }
            }
        }
    }

    if (newLevel > 0) {
        Network_SendUpgradeUpdate(&currentConnectionState, option.isRelic, option.type, newLevel);
    }
}

void DrawUpgradeCards(void) {
    float cardWidth = 220.0f;
    float cardHeight = 150.0f;
    float spacing = 20.0f;
    float totalWidth = (cardWidth * 3) + (spacing * 2);
    float startX = (SCREEN_WIDTH - totalWidth) / 2.0f;
    float startY = SCREEN_HEIGHT - cardHeight - 20.0f;
    
    for (int i = 0; i < 3; i++) {
        if (upgradeOptions[i].type == 0) continue;
        
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
        strncpy(nameBuffer, upgradeOptions[i].name, sizeof(nameBuffer) - 1);
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
        if (upgradeOptions[i].isRelic) {
            for (int j = 0; j < 4; j++) {
                if (globalVariables.playerRelics[j].type == upgradeOptions[i].type) {
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
            for (int j = 0; j < 4; j++) {
                if (globalVariables.playerWeapons[j].type == upgradeOptions[i].type) {
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
        DrawRectangle(card.x + 160, card.y + 35, 40, 40, upgradeOptions[i].color);
        DrawRectangleLines(card.x + 160, card.y + 35, 40, 40, WHITE);
        
        // Draw Description
        DrawText(upgradeOptions[i].description, card.x + 10, descY, 14, GRAY);
    }
}

void DrawStatsOverlay(void) {
    // 1. Dark fullscreen backdrop with 50% opacity
    DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(BLACK, 0.5f));
    
    // 2. Centered HUD Panel
    Rectangle hud = { 128, 72, 1024, 576 };
    DrawRectangleRec(hud, Fade(BLACK, 0.85f));
    DrawRectangleLinesEx(hud, 3, GOLD);
    
    // Determine which player we are spectating/viewing
    u32 targetPlayerID = spectatedPlayerID;
    if (targetPlayerID == 0) {
        targetPlayerID = currentConnectionState.localPlayerIdentification;
    }
    
    // 3. Header title
    const char* headerText = "";
    if (targetPlayerID == currentConnectionState.localPlayerIdentification) {
        headerText = TextFormat("PLAYER PROFILE & STATS (YOU - ID: %u)", targetPlayerID);
    } else {
        headerText = TextFormat("PLAYER PROFILE & STATS (PLAYER %u)", targetPlayerID);
    }
    DrawText(headerText, hud.x + (hud.width - MeasureText(headerText, 24)) / 2.0f, hud.y + 20, 24, GOLD);
    DrawLine(hud.x + 50, hud.y + 60, hud.x + 974, hud.y + 60, GRAY);
    
    // Get player attributes
    u32 spectatedIndex = (targetPlayerID - 1) % MAX_REMOTE_PLAYERS;
    PlayerAttributes* attr = &currentConnectionState.playerAttributes[spectatedIndex];
    
    // Reconstruct weapons and relics for display
    Weapon displayWeapons[4];
    Relic displayRelics[4];
    for (int i = 0; i < 4; i++) {
        displayWeapons[i].type = WEAPON_UNDEFINED;
        displayWeapons[i].level = 0;
        displayRelics[i].type = RELIC_UNDEFINED;
        displayRelics[i].level = 0;
    }
    
    if (targetPlayerID == currentConnectionState.localPlayerIdentification) {
        for (int i = 0; i < 4; i++) {
            displayWeapons[i] = globalVariables.playerWeapons[i];
            displayRelics[i] = globalVariables.playerRelics[i];
        }
    } else {
        u32 entityIndex = targetPlayerID % MAX_REMOTE_ENTITIES;
        Entity* entity = &currentConnectionState.remoteEntities[entityIndex];
        if (entity->entityType == ENTITY_CHARACTER && entity->character.characterType == CHARACTER_PLAYER) {
            int slotCount = 0;
            for (int w = 0; w < 5; w++) {
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
            for (int r = 0; r < 7; r++) {
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
    for (int i = 0; i < 4; i++) {
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
    for (int i = 0; i < 4; i++) {
        float slotY = hud.y + 325 + i * 32;
        Relic* r = &displayRelics[i];
        if (r->type == RELIC_UNDEFINED) {
            DrawText(TextFormat("[%d] [Empty Relic Slot]", i + 1), leftX + 15, slotY, 14, DARKGRAY);
        } else {
            DrawRectangle(leftX + 15, slotY + 2, 10, 10, relicColors[r->type]);
            DrawRectangleLines(leftX + 15, slotY + 2, 10, 10, WHITE);
            
            int pct = 0;
            switch (r->type) {
                case RELIC_HEALTH: pct = (int)(r->level * RELIC_LEVELUP_HEALTH * 100); break;
                case RELIC_DAMAGE: pct = (int)(r->level * RELIC_LEVELUP_DAMAGE * 100); break;
                case RELIC_ATTACK_SPEED: pct = (int)(r->level * RELIC_LEVELUP_ATTACKSPEED * 100); break;
                case RELIC_SIZE: pct = (int)(r->level * RELIC_LEVELUP_SIZE * 100); break;
                case RELIC_MOVEMENT_SPEED: pct = (int)(r->level * RELIC_LEVELUP_MOVEMENTSPEED * 100); break;
                case RELIC_XP_GAIN: pct = (int)(r->level * RELIC_LEVELUP_XPGAIN * 100); break;
                case RELIC_LIFE_STEAL: pct = (int)(r->level * RELIC_LEVELUP_LIFESTEAL * 100); break;
                default: break;
            }
            
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
