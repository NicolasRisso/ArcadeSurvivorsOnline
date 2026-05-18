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
    PACKET_ENTITY_SNAPSHOT = 7,
    PACKET_ENEMY_DEATH_REPORT = 8,
    PACKET_ENTITY_DESPAWN = 9,
    PACKET_WEAPON_FIRE = 10,
    PACKET_ENTITY_DAMAGE = 11,
    PACKET_PROJECTILE_EXPLODE = 12,
    PACKET_DAMAGE_BATCH = 13,
    PACKET_XP_COLLECT = 14,
    PACKET_ATTRIBUTE_UPDATE = 15
} PacketType;

#define MAX_REMOTE_PLAYERS 4
#define MAX_REMOTE_ENTITIES 5100
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
    Vector2 position;
    Vector2 velocity;
} PacketVelocityUpdate;

typedef struct {
    u32 identification;
    Vector2 position;
    Vector2 velocity;
    u8 weaponsMask;
    f32 health;
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
    Vector2 velocity;
    f32 health;
    f32 maxHealth;
    i32 extraParam;
} PacketEntitySpawn;

typedef struct {
    PacketHeader header;
    u16 firstEntityIndex;
    u16 count;
    Vector2 positions[128]; 
} PacketEntitySnapshot;

typedef struct {
    PacketHeader header;
    u32 count;
    u32 enemyIDs[128]; // Batch of killed enemies
} PacketEnemyDeathReport;

typedef struct {
    PacketHeader header;
    u32 entityIndex;
} PacketEntityDespawn;

typedef struct {
    PacketHeader header;
    u8 weaponType;
    f32 damage;
    f32 radius;
    i32 extraParam;
} PacketWeaponFire;

typedef struct {
    PacketHeader header;
    u32 entityIndex;
    f32 damage;
} PacketEntityDamage;

typedef struct {
    PacketHeader header;
    u32 projectileIndex;
} PacketProjectileExplode;

typedef struct {
    u32 entityIndex;
    f32 damage;
} DamageEntry;

typedef struct {
    PacketHeader header;
    u32 count;
    DamageEntry entries[128];
} PacketDamageBatch;

typedef struct {
    PacketHeader header;
    u32 crystalIndex;
} PacketXPCollect;

typedef struct {
    PacketHeader header;
    PlayerAttributes attributes;
} PacketAttributeUpdate;

#pragma pack(pop)

typedef struct ConnectionState {
    u32 localPlayerIdentification;
    Vector2 localPosition;
    f32 health;
    f32 maxHealth;
    bool isConnected;
    
    f64 lastHeartbeatSent;
    f64 lastHeartbeatReceived;
    f64 lastVelocitySentTime;
    
    f32 damageFlashTimer;
    f32 iframeTimer;
    
    PlayerAttributes playerAttributes[MAX_REMOTE_PLAYERS];
    Entity remoteEntities[MAX_REMOTE_ENTITIES];

    u32 pendingKills[512];
    u32 pendingKillsCount;

    DamageEntry pendingDamage[512];
    u32 pendingDamageCount;
    
    VisualEffect localVisualEffects[128];
} ConnectionState;

bool Network_InitConnection(ConnectionState* state);
void Network_UpdateConnection(ConnectionState* state);
void Network_SendVelocity(ConnectionState* state, Vector2 velocity);
void Network_SendDeathReport(ConnectionState* state);
void Network_SendWeaponFire(ConnectionState* state, u8 weaponType, f32 damage, f32 radius, i32 extraParam);
void Network_SendDamage(ConnectionState* state, u32 entityIndex, f32 damage);
void Network_SendDamageBatch(ConnectionState* state);
void Network_SendXPCollect(ConnectionState* state, u32 crystalIndex);
void Network_SendProjectileExplode(ConnectionState* state, u32 projectileIndex);
void Network_SendAttributeUpdate(ConnectionState* state, PlayerAttributes attr);
void Network_QueueDeath(ConnectionState* state, u32 enemyID);
void Network_CloseConnection();

static inline bool Network_IsConnected(ConnectionState* state) {
    if(state) return state->isConnected;
    return false;
}

#endif // CONNECTION_H