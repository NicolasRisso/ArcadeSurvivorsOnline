#ifndef CONNECTION_H
#define CONNECTION_H

#include "main.h"

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 12345
#define HEARTBEAT_INTERVAL 5.0
#define HEARTBEAT_RETRY_INTERVAL 0.5
#define DISCONNECT_TIMEOUT 30.0
#define NETWORK_TPS 20.0
#define NETWORK_UPDATE_INTERVAL (1.0 / NETWORK_TPS)

typedef enum {
    PACKET_ID_REQUEST = 0,
    PACKET_ID_RESPONSE = 1,
    PACKET_HEARTBEAT = 2,
    PACKET_HEARTBEAT_ACK = 3,
    PACKET_VELOCITY_UPDATE = 4,
    PACKET_WORLD_STATE = 5,
    PACKET_ENTITY_SPAWN = 6,
    PACKET_ENTITY_SNAPSHOT = 7
} PacketType;

#define MAX_REMOTE_PLAYERS 32
#define MAX_REMOTE_ENTITIES 128
#define MAX_PACKET_SIZE 4096

#pragma pack(push, 1)
typedef struct {
    u8 type;
    u32 playerIdentification;
    f64 timestamp;
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
    u32 identification;
    Vector2 position;
    Vector2 velocity;
} RemotePlayerState;

typedef struct {
    PacketHeader header;
    u32 count;
    RemotePlayerState players[MAX_REMOTE_PLAYERS];
} PacketWorldState;

typedef struct {
    PacketHeader header;
    u32 entityIndex;
    u8 entityType;
    u8 characterType;
    Vector2 position;
    u32 targetPlayerID;
} PacketEntitySpawn;

typedef struct {
    u32 entityIndex;
    Vector2 position;
    u32 targetPlayerID;
} EntitySnapshot;

typedef struct {
    PacketHeader header;
    u32 count;
    EntitySnapshot snapshots[MAX_REMOTE_ENTITIES];
} PacketEntitySnapshot;

#pragma pack(pop)

typedef struct {
    u32 localPlayerIdentification;
    Vector2 localPosition;
    bool isConnected;
    
    f64 lastHeartbeatSent;
    f64 lastHeartbeatReceived;
    f64 lastVelocitySentTime;
    
    Entity remoteEntities[MAX_REMOTE_ENTITIES];
} ConnectionState;

bool Network_InitConnection(ConnectionState* state);
void Network_UpdateConnection(ConnectionState* state);
void Network_SendVelocity(ConnectionState* state, Vector2 velocity);
void Network_CloseConnection();

static inline bool Network_IsConnected(ConnectionState* state) {
    if(state) return state->isConnected;
    return false;
}

#endif // CONNECTION_H