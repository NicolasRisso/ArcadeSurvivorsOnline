#ifndef CONNECTION_H
#define CONNECTION_H

#include <stdint.h>
#include <stdbool.h>

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
    PACKET_POSITION_UPDATE = 4,
    PACKET_WORLD_STATE = 5 // Not strictly used if we broadcast POSITION_UPDATE
} PacketType;

#pragma pack(push, 1)
typedef struct {
    uint8_t type;
    uint32_t player_id;
    double timestamp;
} PacketHeader;

typedef struct {
    PacketHeader header;
} PacketIDRequest;

typedef struct {
    PacketHeader header;
    float x;
    float y;
} PacketIDResponse;

typedef struct {
    PacketHeader header;
} PacketHeartbeat;

typedef struct {
    PacketHeader header;
} PacketHeartbeatAck;

typedef struct {
    PacketHeader header;
    float x;
    float y;
} PacketPositionUpdate;
#pragma pack(pop)

typedef struct {
    uint32_t id;
    float x;
    float y;
    bool active;
} RemotePlayer;

#define MAX_REMOTE_PLAYERS 32

typedef struct {
    uint32_t local_player_id;
    float local_x;
    float local_y;
    bool connected;
    
    double last_heartbeat_sent;
    double last_heartbeat_ack;
    
    RemotePlayer remote_players[MAX_REMOTE_PLAYERS];
} ConnectionState;

bool InitConnection(ConnectionState* state);
void UpdateConnection(ConnectionState* state);
void SendPosition(ConnectionState* state, float x, float y);
void CloseConnection();

#endif // CONNECTION_H
