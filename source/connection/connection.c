#include "connection.h"
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include "raylib.h"


static SOCKET client_socket = INVALID_SOCKET;
static struct sockaddr_in server_addr;

bool InitConnection(ConnectionState* state) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("Winsock init failed\n");
        return false;
    }

    client_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (client_socket == INVALID_SOCKET) {
        printf("Socket creation failed\n");
        WSACleanup();
        return false;
    }

    // Set non-blocking
    u_long mode = 1;
    ioctlsocket(client_socket, FIONBIO, &mode);

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    state->connected = false;
    state->local_player_id = 0;
    state->last_heartbeat_sent = 0;
    state->last_heartbeat_ack = GetTime();

    for (int i = 0; i < MAX_REMOTE_PLAYERS; i++) {
        state->remote_players[i].active = false;
    }

    // Send ID request
    PacketIDRequest req;
    req.header.type = PACKET_ID_REQUEST;
    req.header.player_id = 0;
    req.header.timestamp = GetTime();
    sendto(client_socket, (char*)&req, sizeof(req), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));

    printf("ID Request sent to server...\n");

    return true;
}

void UpdateConnection(ConnectionState* state) {
    char buffer[1024];
    struct sockaddr_in from_addr;
    int from_len = sizeof(from_addr);

    // Receive packets
    int bytes_received;
    while ((bytes_received = recvfrom(client_socket, buffer, sizeof(buffer), 0, (struct sockaddr*)&from_addr, &from_len)) > 0) {
        PacketHeader* header = (PacketHeader*)buffer;

        switch (header->type) {
            case PACKET_ID_RESPONSE: {
                PacketIDResponse* res = (PacketIDResponse*)buffer;
                state->local_player_id = res->header.player_id;
                state->local_x = res->x;
                state->local_y = res->y;
                state->connected = true;
                state->last_heartbeat_ack = GetTime();
                printf("Connected! ID: %u, Spawn: (%.1f, %.1f)\n", state->local_player_id, state->local_x, state->local_y);
                break;
            }
            case PACKET_HEARTBEAT_ACK: {
                state->last_heartbeat_ack = GetTime();
                break;
            }
            case PACKET_POSITION_UPDATE: {
                PacketPositionUpdate* pos = (PacketPositionUpdate*)buffer;
                if (pos->header.player_id == state->local_player_id) break;

                // Update remote player
                int slot = -1;
                for (int i = 0; i < MAX_REMOTE_PLAYERS; i++) {
                    if (state->remote_players[i].active && state->remote_players[i].id == pos->header.player_id) {
                        slot = i;
                        break;
                    }
                }

                if (slot == -1) {
                    // Find empty slot
                    for (int i = 0; i < MAX_REMOTE_PLAYERS; i++) {
                        if (!state->remote_players[i].active) {
                            slot = i;
                            break;
                        }
                    }
                }

                if (slot != -1) {
                    state->remote_players[slot].id = pos->header.player_id;
                    state->remote_players[slot].x = pos->x;
                    state->remote_players[slot].y = pos->y;
                    state->remote_players[slot].active = true;
                }
                break;
            }
        }
    }

    // Heartbeat logic
    double now = GetTime();
    if (state->connected) {
        if (now - state->last_heartbeat_sent > HEARTBEAT_INTERVAL) {
            PacketHeartbeat hb;
            hb.header.type = PACKET_HEARTBEAT;
            hb.header.player_id = state->local_player_id;
            hb.header.timestamp = now;
            sendto(client_socket, (char*)&hb, sizeof(hb), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
            state->last_heartbeat_sent = now;
        }

        // Retry heartbeat if no ACK after 500ms
        if (now - state->last_heartbeat_ack > HEARTBEAT_RETRY_INTERVAL && now - state->last_heartbeat_sent < HEARTBEAT_INTERVAL) {
             // Re-send if we haven't received an ACK for the last one after 500ms
             // To keep it simple, we just send again if the gap is too large
             if (now - state->last_heartbeat_sent > HEARTBEAT_RETRY_INTERVAL) {
                 PacketHeartbeat hb;
                 hb.header.type = PACKET_HEARTBEAT;
                 hb.header.player_id = state->local_player_id;
                 hb.header.timestamp = now;
                 sendto(client_socket, (char*)&hb, sizeof(hb), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
                 state->last_heartbeat_sent = now;
             }
        }

        if (now - state->last_heartbeat_ack > DISCONNECT_TIMEOUT) {
            printf("Connection timed out\n");
            state->connected = false;
        }
    } else {
        // If not connected, retry ID request periodically
        static double last_id_req = 0;
        if (now - last_id_req > 2.0) {
            PacketIDRequest req;
            req.header.type = PACKET_ID_REQUEST;
            req.header.player_id = 0;
            req.header.timestamp = now;
            sendto(client_socket, (char*)&req, sizeof(req), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
            last_id_req = now;
        }
    }
}

void SendPosition(ConnectionState* state, float x, float y) {
    if (!state->connected) return;

    PacketPositionUpdate pkt;
    pkt.header.type = PACKET_POSITION_UPDATE;
    pkt.header.player_id = state->local_player_id;
    pkt.header.timestamp = GetTime();
    pkt.x = x;
    pkt.y = y;

    sendto(client_socket, (char*)&pkt, sizeof(pkt), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
}

void CloseConnection() {
    if (client_socket != INVALID_SOCKET) {
        closesocket(client_socket);
        WSACleanup();
    }
}
