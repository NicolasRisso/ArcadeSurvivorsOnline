#include "main.h"
#include "connection/connection.h"
#include <math.h>

int main(void) {
    // Initialization
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Arcade Survivors Online");

    ConnectionState connState = { 0 };
    if (!Network_InitConnection(&connState)) {
        CloseWindow();
        return 1;
    }

    Camera2D camera = { 0 };
    camera.target = connState.local_pos;
    camera.offset = (Vector2){ SCREEN_WIDTH / 2.0f, SCREEN_HEIGHT / 2.0f };
    camera.rotation = 0.0f;
    camera.zoom = 1.0f;

    SetTargetFPS(TARGET_FPS);

    while (!WindowShouldClose()) {
        // Update
        Network_UpdateConnection(&connState);

        f32 dt = GetFrameTime();

        // Update Remote Players (Dead Reckoning)
        for (i32 i = 0; i < MAX_REMOTE_PLAYERS; i++) {
            if (connState.remote_players[i].active) {
                connState.remote_players[i].pos.x += connState.remote_players[i].velocity.x * dt;
                connState.remote_players[i].pos.y += connState.remote_players[i].velocity.y * dt;
            }
        }

        if (Network_IsConnected(&connState)) {
            Vector2 movement = { 0 };
            if (IsKeyDown(KEY_W)) movement.y -= 1;
            if (IsKeyDown(KEY_S)) movement.y += 1;
            if (IsKeyDown(KEY_A)) movement.x -= 1;
            if (IsKeyDown(KEY_D)) movement.x += 1;

            Vector2 velocity = { 0 };
            if (movement.x != 0 || movement.y != 0) {
                // Normalize movement
                const f32 length = sqrtf(movement.x * movement.x + movement.y * movement.y);
                movement.x /= length;
                movement.y /= length;

                velocity.x = movement.x * PLAYER_SPEED;
                velocity.y = movement.y * PLAYER_SPEED;

                connState.local_pos.x += velocity.x * dt;
                connState.local_pos.y += velocity.y * dt;
            }
            
            // Send velocity update (even if zero, to stop movement on others)
            // Optimization: only send if changed? For now, following user's simple rule.
            Network_SendVelocity(&connState, velocity);
        }

        // Camera follow
        camera.target = connState.local_pos;

        // Draw
        BeginDrawing();
            ClearBackground(RAYWHITE);

            BeginMode2D(camera);
                // Draw Map (10k x 10k grey box)
                DrawRectangle(-MAP_SIZE/2, -MAP_SIZE/2, MAP_SIZE, MAP_SIZE, LIGHTGRAY);
                
                // Draw grid lines for perspective
                for (i32 i = -MAP_SIZE/2; i <= MAP_SIZE/2; i += 500) {
                    DrawLine(i, -MAP_SIZE/2, i, MAP_SIZE/2, GRAY);
                    DrawLine(-MAP_SIZE/2, i, MAP_SIZE/2, i, GRAY);
                }

                // Draw Remote Players
                for (i32 i = 0; i < MAX_REMOTE_PLAYERS; i++) {
                    if (connState.remote_players[i].active) {
                        DrawCircleV(connState.remote_players[i].pos, PLAYER_RADIUS, RED);
                        DrawText(TextFormat("ID: %u", connState.remote_players[i].id), connState.remote_players[i].pos.x - 20, connState.remote_players[i].pos.y - 40, 10, DARKGRAY);
                    }
                }

                // Draw Local Player
                if (connState.connected) {
                    DrawCircleV(connState.local_pos, PLAYER_RADIUS, BLUE);
                    DrawText(TextFormat("ME (ID: %u)", connState.local_player_id), connState.local_pos.x - 30, connState.local_pos.y - 40, 12, DARKBLUE);
                } else {
                    DrawText("Connecting...", connState.local_pos.x - 40, connState.local_pos.y, 20, DARKGRAY);
                }

            EndMode2D();

            // UI
            DrawFPS(10, 10);
            if (!connState.connected) {
                DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(BLACK, 0.3f));
                DrawText("CONNECTING TO SERVER...", SCREEN_WIDTH/2 - 150, SCREEN_HEIGHT/2, 20, WHITE);
            }

        EndDrawing();
    }

    Network_CloseConnection();
    CloseWindow();

    return 0;
}
