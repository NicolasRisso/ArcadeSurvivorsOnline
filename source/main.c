#include "raylib.h"
#include "connection/connection.h"
#include <math.h>

#define MAP_SIZE 10000.0f
#define PLAYER_SPEED 300.0f
#define PLAYER_RADIUS 20.0f

int main(void) {
    // Initialization
    const int screenWidth = 1280;
    const int screenHeight = 720;

    InitWindow(screenWidth, screenHeight, "Arcade Survivors Online");

    ConnectionState connState = { 0 };
    if (!InitConnection(&connState)) {
        CloseWindow();
        return 1;
    }

    Camera2D camera = { 0 };
    camera.target = (Vector2){ connState.local_x, connState.local_y };
    camera.offset = (Vector2){ screenWidth / 2.0f, screenHeight / 2.0f };
    camera.rotation = 0.0f;
    camera.zoom = 1.0f;

    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        // Update
        UpdateConnection(&connState);

        if (connState.connected) {
            Vector2 movement = { 0 };
            if (IsKeyDown(KEY_W)) movement.y -= 1;
            if (IsKeyDown(KEY_S)) movement.y += 1;
            if (IsKeyDown(KEY_A)) movement.x -= 1;
            if (IsKeyDown(KEY_D)) movement.x += 1;

            if (movement.x != 0 || movement.y != 0) {
                // Normalize movement
                float length = sqrtf(movement.x * movement.x + movement.y * movement.y);
                movement.x /= length;
                movement.y /= length;

                connState.local_x += movement.x * PLAYER_SPEED * GetFrameTime();
                connState.local_y += movement.y * PLAYER_SPEED * GetFrameTime();

                // Send position update
                SendPosition(&connState, connState.local_x, connState.local_y);
            }
        }

        // Camera follow
        camera.target = (Vector2){ connState.local_x, connState.local_y };

        // Draw
        BeginDrawing();
            ClearBackground(RAYWHITE);

            BeginMode2D(camera);
                // Draw Map (10k x 10k grey box)
                DrawRectangle(-MAP_SIZE/2, -MAP_SIZE/2, MAP_SIZE, MAP_SIZE, LIGHTGRAY);
                
                // Draw grid lines for perspective
                for (int i = -MAP_SIZE/2; i <= MAP_SIZE/2; i += 500) {
                    DrawLine(i, -MAP_SIZE/2, i, MAP_SIZE/2, GRAY);
                    DrawLine(-MAP_SIZE/2, i, MAP_SIZE/2, i, GRAY);
                }

                // Draw Remote Players
                for (int i = 0; i < MAX_REMOTE_PLAYERS; i++) {
                    if (connState.remote_players[i].active) {
                        DrawCircle(connState.remote_players[i].x, connState.remote_players[i].y, PLAYER_RADIUS, RED);
                        DrawText(TextFormat("ID: %u", connState.remote_players[i].id), connState.remote_players[i].x - 20, connState.remote_players[i].y - 40, 10, DARKGRAY);
                    }
                }

                // Draw Local Player
                if (connState.connected) {
                    DrawCircle(connState.local_x, connState.local_y, PLAYER_RADIUS, BLUE);
                    DrawText(TextFormat("ME (ID: %u)", connState.local_player_id), connState.local_x - 30, connState.local_y - 40, 12, DARKBLUE);
                } else {
                    DrawText("Connecting...", connState.local_x - 40, connState.local_y, 20, DARKGRAY);
                }

            EndMode2D();

            // UI
            DrawFPS(10, 10);
            if (!connState.connected) {
                DrawRectangle(0, 0, screenWidth, screenHeight, Fade(BLACK, 0.3f));
                DrawText("CONNECTING TO SERVER...", screenWidth/2 - 150, screenHeight/2, 20, WHITE);
            }

        EndDrawing();
    }

    CloseConnection();
    CloseWindow();

    return 0;
}
