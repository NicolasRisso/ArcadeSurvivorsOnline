#ifndef CONNECTION_H
#define CONNECTION_H

#include <stdbool.h>
#include "modern_types.h"
#include "raylib.h"

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 12345
#define HEARTBEAT_INTERVAL 5.0
#define HEARTBEAT_RETRY_INTERVAL 0.5
#define DISCONNECT_TIMEOUT 30.0

typedef enum {
    PACKET_ID_REQUEST = 0,
    PACKET_ID_RESPONSE = 1,
    PACKET_HEARTBEAT = 2,
    PACKET_HEARTBEAT_ACK = 3,
    PACKET_VELOCITY_UPDATE = 4,
    PACKET_WORLD_STATE = 5
} PacketType;

#define MAX_REMOTE_PLAYERS 32

#pragma pack(push, 1)
typedef struct {
    u8 type;
    u32 player_id;
    double timestamp;
} PacketHeader;

typedef struct {
    PacketHeader header;
} PacketIDRequest;

typedef struct {
    PacketHeader header;
} PacketIDResponse;

typedef struct {
    PacketHeader header;
} PacketHeartbeat;

typedef struct {
    PacketHeader header;
} PacketHeartbeatAck;

typedef struct {
    PacketHeader header;
    Vector2 velocity;
} PacketVelocityUpdate;

typedef struct {
    u32 id;
    Vector2 velocity;
} RemotePlayerState;

typedef struct {
    PacketHeader header;
    u32 count;
    RemotePlayerState players[MAX_REMOTE_PLAYERS];
} PacketWorldState;
#pragma pack(pop)

typedef struct {
    u32 id;
    Vector2 pos;
    Vector2 velocity;
    bool active;
} RemotePlayer;

typedef struct {
    u32 local_player_id;
    Vector2 local_pos;
    bool connected;
    
    double last_heartbeat_sent;
    double last_heartbeat_ack;
    
    RemotePlayer remote_players[MAX_REMOTE_PLAYERS];
} ConnectionState;

bool Network_InitConnection(ConnectionState* state);
void Network_UpdateConnection(ConnectionState* state);
void Network_SendVelocity(ConnectionState* state, Vector2 velocity);
void Network_CloseConnection();

static inline bool Network_IsConnected(ConnectionState* state) {
    if(state) return state->connected;
    return false;
}

#endif // CONNECTION_H