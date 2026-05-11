#include "connection.h"
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>

static SOCKET client_socket = INVALID_SOCKET;
static struct sockaddr_in server_addr;

bool Network_InitConnection(ConnectionState* state) {
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

    for (i32 i = 0; i < MAX_REMOTE_PLAYERS; i++) {
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

void Network_UpdateConnection(ConnectionState* state) {
    char buffer[2048]; // Increased for world state
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
                state->connected = true;
                state->last_heartbeat_ack = GetTime();
                printf("Connected! ID: %u\n", state->local_player_id);
                break;
            }
            case PACKET_HEARTBEAT_ACK: {
                state->last_heartbeat_ack = GetTime();
                break;
            }
            case PACKET_VELOCITY_UPDATE: {
                PacketVelocityUpdate* vel = (PacketVelocityUpdate*)buffer;
                if (vel->header.player_id == state->local_player_id) break;

                i32 slot = -1;
                for (i32 i = 0; i < MAX_REMOTE_PLAYERS; i++) {
                    if (state->remote_players[i].active && state->remote_players[i].id == vel->header.player_id) {
                        slot = i;
                        break;
                    }
                }

                if (slot == -1) {
                    for (i32 i = 0; i < MAX_REMOTE_PLAYERS; i++) {
                        if (!state->remote_players[i].active) {
                            slot = i;
                            break;
                        }
                    }
                }

                if (slot != -1) {
                    state->remote_players[slot].id = vel->header.player_id;
                    state->remote_players[slot].velocity = vel->velocity;
                    state->remote_players[slot].active = true;
                }
                break;
            }
            case PACKET_WORLD_STATE: {
                PacketWorldState* world = (PacketWorldState*)buffer;
                for (u32 i = 0; i < world->count; i++) {
                    RemotePlayerState* ps = &world->players[i];
                    if (ps->id == state->local_player_id) continue;

                    i32 slot = -1;
                    for (i32 j = 0; j < MAX_REMOTE_PLAYERS; j++) {
                        if (state->remote_players[j].active && state->remote_players[j].id == ps->id) {
                            slot = j;
                            break;
                        }
                    }

                    if (slot == -1) {
                        for (i32 j = 0; j < MAX_REMOTE_PLAYERS; j++) {
                            if (!state->remote_players[j].active) {
                                slot = j;
                                break;
                            }
                        }
                    }

                    if (slot != -1) {
                        state->remote_players[slot].id = ps->id;
                        state->remote_players[slot].velocity = ps->velocity;
                        state->remote_players[slot].active = true;
                    }
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

        if (now - state->last_heartbeat_ack > DISCONNECT_TIMEOUT) {
            printf("Connection timed out\n");
            state->connected = false;
        }
    } else {
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

void Network_SendVelocity(ConnectionState* state, Vector2 velocity) {
    if (!state->connected) return;

    PacketVelocityUpdate pkt;
    pkt.header.type = PACKET_VELOCITY_UPDATE;
    pkt.header.player_id = state->local_player_id;
    pkt.header.timestamp = GetTime();
    pkt.velocity = velocity;

    sendto(client_socket, (char*)&pkt, sizeof(pkt), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
}

void Network_CloseConnection() {
    if (client_socket != INVALID_SOCKET) {
        closesocket(client_socket);
        WSACleanup();
    }
}
