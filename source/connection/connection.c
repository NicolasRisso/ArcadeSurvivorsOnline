#include "connection.h"
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>

static SOCKET clientSocket = INVALID_SOCKET;
static struct sockaddr_in serverAddress;

bool Network_InitConnection(ConnectionState* connectionState) {
    WSADATA windowsSocketData;
    if (WSAStartup(MAKEWORD(2, 2), &windowsSocketData) != 0) {
        printf("Winsock initialization failed\n");
        return false;
    }

    clientSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (clientSocket == INVALID_SOCKET) {
        printf("Socket creation failed\n");
        WSACleanup();
        return false;
    }

    // Set non-blocking mode
    u_long nonBlockingMode = 1;
    ioctlsocket(clientSocket, FIONBIO, &nonBlockingMode);

    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, SERVER_IP, &serverAddress.sin_addr) <= 0) {
        printf("Invalid address/ Address not supported\n");
        return false;
    }

    connectionState->isConnected = false;
    connectionState->localPlayerIdentification = 0;
    connectionState->lastHeartbeatSent = 0;
    connectionState->lastHeartbeatReceived = GetTime();
    connectionState->lastVelocitySentTime = 0;
    connectionState->pendingKillsCount = 0;
    connectionState->pendingDamageCount = 0;
    connectionState->health = DEFAULT_MAX_HEALTH;
    connectionState->maxHealth = DEFAULT_MAX_HEALTH;
    connectionState->damageFlashTimer = 0.0f;
    connectionState->iframeTimer = 0.0f;

    for (int i = 0; i < MAX_REMOTE_PLAYERS; i++) {
        connectionState->playerAttributes[i] = (PlayerAttributes){
            .maxHealth = DEFAULT_MAX_HEALTH,
            .damage = DEFAULT_DAMAGE,
            .attackSpeed = DEFAULT_ATTACK_SPEED,
            .movementSpeed = DEFAULT_MOVEMENT_SPEED,
            .size = DEFAULT_SIZE,
            .xpGained = DEFAULT_XP_GAINED,
            .lifeSteal = DEFAULT_LIFESTEAL
        };
    }

    for (i32 entityIndex = 0; entityIndex < MAX_REMOTE_ENTITIES; entityIndex++) {
        connectionState->remoteEntities[entityIndex].entityType = ENTITY_UNDEFINED;
    }

    // Send identification request
    PacketIDRequest identificationRequest;
    identificationRequest.header.type = PACKET_ID_REQUEST;
    identificationRequest.header.playerIdentification = 0;
    identificationRequest.header.timestamp = GetTime();
    sendto(clientSocket, (char*)&identificationRequest, sizeof(identificationRequest), 0, (struct sockaddr*)&serverAddress, sizeof(serverAddress));

    printf("Identification request sent to server...\n");

    return true;
}

void Network_UpdateConnection(ConnectionState* connectionState) {
    char receiveBuffer[4096]; 
    struct sockaddr_in fromAddress;
    int fromAddressLength = sizeof(fromAddress);

    // Receive packets
    int bytesReceived;
    while ((bytesReceived = recvfrom(clientSocket, receiveBuffer, sizeof(receiveBuffer), 0, (struct sockaddr*)&fromAddress, &fromAddressLength)) > 0) {
        PacketHeader* packetHeader = (PacketHeader*)receiveBuffer;
        // printf("DEBUG: Received packet type %d (%d bytes)\n", packetHeader->type, bytesReceived);

        switch (packetHeader->type) {
            case PACKET_ID_RESPONSE: {
                PacketIDResponse* identificationResponse = (PacketIDResponse*)receiveBuffer;
                connectionState->localPlayerIdentification = identificationResponse->header.playerIdentification;
                connectionState->isConnected = true;
                connectionState->lastHeartbeatReceived = GetTime();
                printf("Connected! Identification: %u\n", connectionState->localPlayerIdentification);
                break;
            }
            case PACKET_HEARTBEAT_ACK: {
                connectionState->lastHeartbeatReceived = GetTime();
                break;
            }
            case PACKET_VELOCITY_UPDATE: {
                PacketVelocityUpdate* velocityUpdate = (PacketVelocityUpdate*)receiveBuffer;
                if (velocityUpdate->header.playerIdentification == connectionState->localPlayerIdentification) break;

                // For remote players, we still use the identification as index for now 
                // (Server should ensure they fit in MAX_REMOTE_ENTITIES)
                u32 entityIndex = velocityUpdate->header.playerIdentification % MAX_REMOTE_ENTITIES;
                connectionState->remoteEntities[entityIndex].entityType = ENTITY_CHARACTER;
                connectionState->remoteEntities[entityIndex].character.characterType = CHARACTER_PLAYER;
                connectionState->remoteEntities[entityIndex].character.velocity = velocityUpdate->velocity;
                break;
            }
            case PACKET_WORLD_STATE: {
                PacketWorldState* worldState = (PacketWorldState*)receiveBuffer;
                for (u32 playerIndex = 0; playerIndex < worldState->count; playerIndex++) {
                    RemotePlayerState* remotePlayerState = &worldState->players[playerIndex];
                    if (remotePlayerState->identification == connectionState->localPlayerIdentification) {
                        // Reconcile local position with server
                        connectionState->localPosition = Vector2Lerp(connectionState->localPosition, remotePlayerState->position, 0.5f);
                        // Authoritative health reconciliation
                        if (connectionState->health != remotePlayerState->health) {
                            connectionState->health = remotePlayerState->health;
                        }
                        continue;
                    }

                    u32 entityIndex = remotePlayerState->identification % MAX_REMOTE_ENTITIES;
                    connectionState->remoteEntities[entityIndex].entityType = ENTITY_CHARACTER;
                    connectionState->remoteEntities[entityIndex].character.characterType = CHARACTER_PLAYER;
                    connectionState->remoteEntities[entityIndex].character.velocity = remotePlayerState->velocity;
                    connectionState->remoteEntities[entityIndex].character.position = remotePlayerState->position;
                    connectionState->remoteEntities[entityIndex].character.targetPosition = remotePlayerState->position;
                    connectionState->remoteEntities[entityIndex].character.weaponsMask = remotePlayerState->weaponsMask; 
                    connectionState->remoteEntities[entityIndex].character.health = remotePlayerState->health; 
                }
                break;
            }
            case PACKET_ENTITY_SPAWN: {
                PacketEntitySpawn* spawn = (PacketEntitySpawn*)receiveBuffer;
                u32 entityIndex = spawn->entityIndex % MAX_REMOTE_ENTITIES;
                connectionState->remoteEntities[entityIndex].entityType = (EntityType)spawn->entityType;
                if (connectionState->remoteEntities[entityIndex].entityType == ENTITY_CHARACTER) {
                    connectionState->remoteEntities[entityIndex].character.characterType = (CharacterType)spawn->characterType;
                    connectionState->remoteEntities[entityIndex].character.position = spawn->position;
                    connectionState->remoteEntities[entityIndex].character.targetPosition = spawn->position;
                    connectionState->remoteEntities[entityIndex].character.velocity = spawn->velocity;
                    connectionState->remoteEntities[entityIndex].character.spawnTime = GetTime();
                    connectionState->remoteEntities[entityIndex].character.targetPlayerID = spawn->targetPlayerID;
                    connectionState->remoteEntities[entityIndex].character.health = spawn->health;
                    connectionState->remoteEntities[entityIndex].character.maxHealth = spawn->maxHealth;
                    printf("SPAWN: Character %u (Type %d) HP: %.1f/%.1f\n", entityIndex, spawn->characterType, spawn->health, spawn->maxHealth);
                } else if (connectionState->remoteEntities[entityIndex].entityType == ENTITY_PROJECTILE) {
                    connectionState->remoteEntities[entityIndex].proj.type = (ProjectileType)spawn->characterType;
                    connectionState->remoteEntities[entityIndex].proj.position = spawn->position;
                    connectionState->remoteEntities[entityIndex].proj.velocity = spawn->velocity;
                    
                    // Lifetime (Predicted locally)
                    if (connectionState->remoteEntities[entityIndex].proj.type == PROJECTILE_BOMB) {
                        connectionState->remoteEntities[entityIndex].proj.lifetime = 2.0f; // BOMB_DELAY
                    } else if (connectionState->remoteEntities[entityIndex].proj.type == PROJECTILE_SPIKE) {
                        connectionState->remoteEntities[entityIndex].proj.lifetime = 3.0f; // SPIKE_LIFETIME
                    } else if (connectionState->remoteEntities[entityIndex].proj.type == PROJECTILE_EXPLOSION) {
                        connectionState->remoteEntities[entityIndex].proj.lifetime = 0.5f; // EXPLOSION_LIFETIME
                    } else {
                        connectionState->remoteEntities[entityIndex].proj.lifetime = PROJECTILE_LIFETIME;
                    }

                    connectionState->remoteEntities[entityIndex].proj.ownerID = spawn->targetPlayerID;
                    connectionState->remoteEntities[entityIndex].proj.hitCount = 0;
                    connectionState->remoteEntities[entityIndex].proj.damageAccumulated = 0;
                    connectionState->remoteEntities[entityIndex].proj.tickTimer = 0;
                    connectionState->remoteEntities[entityIndex].proj.radius = spawn->maxHealth;
                    connectionState->remoteEntities[entityIndex].proj.damage = spawn->health;
                    connectionState->remoteEntities[entityIndex].proj.pierce = spawn->extraParam;
                    
                    // Prediction: If this is an explosion that WE predicted, ignore it.
                    if (connectionState->remoteEntities[entityIndex].proj.type == PROJECTILE_EXPLOSION) {
                        if (spawn->targetPlayerID == connectionState->localPlayerIdentification) {
                            connectionState->remoteEntities[entityIndex].entityType = ENTITY_UNDEFINED;
                            return; // Skip spawn
                        }
                    }

                    printf("SPAWN: Projectile %u (Type %d) Vel: (%.1f, %.1f)\n", 
                           entityIndex, (int)connectionState->remoteEntities[entityIndex].proj.type, spawn->velocity.x, spawn->velocity.y);
                } else if (connectionState->remoteEntities[entityIndex].entityType == ENTITY_XP_CRYSTAL) {
                    connectionState->remoteEntities[entityIndex].xpCrystal.position = spawn->position;
                    connectionState->remoteEntities[entityIndex].xpCrystal.xpValue = spawn->health;
                    connectionState->remoteEntities[entityIndex].xpCrystal.isMagnetized = false;
                    connectionState->remoteEntities[entityIndex].xpCrystal.magnetizedTimer = 0.0f;
                    connectionState->remoteEntities[entityIndex].xpCrystal.targetPlayerID = 0;
                    printf("SPAWN: XP Crystal %u (Value: %.1f)\n", entityIndex, spawn->health);
                }
                break;
            }
            case PACKET_ENTITY_DAMAGE: {
                PacketEntityDamage* damagePacket = (PacketEntityDamage*)receiveBuffer;
                u32 targetID = damagePacket->entityIndex;
                
                // 1. Local Player took damage (verified from server)
                if (targetID == connectionState->localPlayerIdentification) {
                    if (connectionState->iframeTimer <= 0) {
                        connectionState->health -= damagePacket->damage;
                        connectionState->iframeTimer = 0.5f;
                        connectionState->damageFlashTimer = 0.5f;
                    }
                } else {
                    // 2. Remote Player or Enemy took damage
                    u32 entityIndex = targetID % MAX_REMOTE_ENTITIES;
                    if (connectionState->remoteEntities[entityIndex].entityType == ENTITY_CHARACTER) {
                        if (connectionState->remoteEntities[entityIndex].character.characterType == CHARACTER_PLAYER) {
                            connectionState->remoteEntities[entityIndex].character.health -= damagePacket->damage;
                            connectionState->remoteEntities[entityIndex].character.damageFlashTimer = 0.5f;
                        } else {
                            // Enemy took damage
                            // Prediction: Ignore damage from ourselves (already applied)
                            if (damagePacket->header.playerIdentification != connectionState->localPlayerIdentification) {
                                connectionState->remoteEntities[entityIndex].character.health -= damagePacket->damage;
                            }
                            connectionState->remoteEntities[entityIndex].character.damageFlashTimer = 0.15f;
                        }
                    }
                }
                break;
            }
            case PACKET_ENTITY_SNAPSHOT: {
                PacketEntitySnapshot* snapshot = (PacketEntitySnapshot*)receiveBuffer;
                for (u32 i = 0; i < snapshot->count; i++) {
                    u32 entityIndex = snapshot->firstEntityIndex + i;
                    if (entityIndex >= MAX_REMOTE_ENTITIES) break;

                    if (connectionState->remoteEntities[entityIndex].entityType == ENTITY_CHARACTER && 
                        connectionState->remoteEntities[entityIndex].character.characterType == CHARACTER_ENEMY) {
                        
                        // Ignore (0,0) placeholders from server
                        if (snapshot->positions[i].x == 0.0f && snapshot->positions[i].y == 0.0f) continue;

                        connectionState->remoteEntities[entityIndex].character.targetPosition = snapshot->positions[i];
                    }
                }
                break;
            }
            case PACKET_ENTITY_DESPAWN: {
                PacketEntityDespawn* despawn = (PacketEntityDespawn*)receiveBuffer;
                u32 entityIndex = despawn->entityIndex % MAX_REMOTE_ENTITIES;
                connectionState->remoteEntities[entityIndex].entityType = ENTITY_UNDEFINED;
                break;
            }
            case PACKET_ATTRIBUTE_UPDATE: {
                PacketAttributeUpdate* attrUpdate = (PacketAttributeUpdate*)receiveBuffer;
                u32 playerIndex = (attrUpdate->header.playerIdentification - 1) % MAX_REMOTE_PLAYERS;
                connectionState->playerAttributes[playerIndex] = attrUpdate->attributes;
                printf("ATTRIBUTES: Player %u updated attributes.\n", attrUpdate->header.playerIdentification);
                break;
            }
        }
    }

    // Heartbeat logic
    f64 currentTimestamp = GetTime();
    if (connectionState->isConnected) {
        if (currentTimestamp - connectionState->lastHeartbeatSent > HEARTBEAT_INTERVAL) {
            PacketHeartbeat heartbeatPacket;
            heartbeatPacket.header.type = PACKET_HEARTBEAT;
            heartbeatPacket.header.playerIdentification = connectionState->localPlayerIdentification;
            heartbeatPacket.header.timestamp = currentTimestamp;
            sendto(clientSocket, (char*)&heartbeatPacket, sizeof(heartbeatPacket), 0, (struct sockaddr*)&serverAddress, sizeof(serverAddress));
            connectionState->lastHeartbeatSent = currentTimestamp;
        }

        if (currentTimestamp - connectionState->lastHeartbeatReceived > DISCONNECT_TIMEOUT) {
            printf("Connection timed out\n");
            connectionState->isConnected = false;
        }
    } else {
        static f64 lastIdentificationRequestTimestamp = 0;
        if (currentTimestamp - lastIdentificationRequestTimestamp > 2.0) {
            PacketIDRequest identificationRequest;
            identificationRequest.header.type = PACKET_ID_REQUEST;
            identificationRequest.header.playerIdentification = 0;
            identificationRequest.header.timestamp = currentTimestamp;
            sendto(clientSocket, (char*)&identificationRequest, sizeof(identificationRequest), 0, (struct sockaddr*)&serverAddress, sizeof(serverAddress));
            lastIdentificationRequestTimestamp = currentTimestamp;
            printf("Retrying connection to server...\n");
        }
    }
}

void Network_SendVelocity(ConnectionState* connectionState, Vector2 velocity) {
    if (!connectionState->isConnected) return;

    f64 currentTime = GetTime();
    if (currentTime - connectionState->lastVelocitySentTime < NETWORK_UPDATE_INTERVAL) return;

    PacketVelocityUpdate velocityUpdatePacket;
    velocityUpdatePacket.header.type = PACKET_VELOCITY_UPDATE;
    velocityUpdatePacket.header.playerIdentification = connectionState->localPlayerIdentification;
    velocityUpdatePacket.header.timestamp = currentTime;
    velocityUpdatePacket.position = connectionState->localPosition;
    velocityUpdatePacket.velocity = velocity;

    sendto(clientSocket, (char*)&velocityUpdatePacket, sizeof(velocityUpdatePacket), 0, (struct sockaddr*)&serverAddress, sizeof(serverAddress));
    connectionState->lastVelocitySentTime = currentTime;
}

void Network_QueueDeath(ConnectionState* state, u32 enemyID) {
    if (state->pendingKillsCount < 512) {
        state->pendingKills[state->pendingKillsCount++] = enemyID;
    }
}

void Network_SendDeathReport(ConnectionState* state) {
    if (!state->isConnected || state->pendingKillsCount == 0) return;

    f64 currentTime = GetTime();
    // We send every tick now (no timer)

    // Send in batches of 128
    u32 batchCount = (state->pendingKillsCount > 128) ? 128 : state->pendingKillsCount;

    PacketEnemyDeathReport reportPacket;
    reportPacket.header.type = PACKET_ENEMY_DEATH_REPORT;
    reportPacket.header.playerIdentification = state->localPlayerIdentification;
    reportPacket.header.timestamp = currentTime;
    reportPacket.count = batchCount;
    
    for (u32 i = 0; i < batchCount; i++) {
        reportPacket.enemyIDs[i] = state->pendingKills[i];
    }

    sendto(clientSocket, (char*)&reportPacket, sizeof(PacketHeader) + 4 + (batchCount * 4), 0, (struct sockaddr*)&serverAddress, sizeof(serverAddress));
    
    // Shift remaining kills
    if (batchCount < state->pendingKillsCount) {
        memmove(state->pendingKills, state->pendingKills + batchCount, (state->pendingKillsCount - batchCount) * sizeof(u32));
    }
    state->pendingKillsCount -= batchCount;
}

void Network_SendWeaponFire(ConnectionState* state, u8 weaponType, f32 damage, f32 radius, i32 extraParam) {
    if (!state->isConnected) return;

    PacketWeaponFire packet;
    packet.header.type = PACKET_WEAPON_FIRE;
    packet.header.playerIdentification = state->localPlayerIdentification;
    packet.header.timestamp = GetTime();
    packet.weaponType = weaponType;
    packet.damage = damage;
    packet.radius = radius;
    packet.extraParam = extraParam;

    sendto(clientSocket, (char*)&packet, sizeof(packet), 0, (struct sockaddr*)&serverAddress, sizeof(serverAddress));
}

void Network_SendDamage(ConnectionState* state, u32 entityIndex, f32 damage) {
    if (state->pendingDamageCount < 512) {
        state->pendingDamage[state->pendingDamageCount].entityIndex = entityIndex;
        state->pendingDamage[state->pendingDamageCount].damage = damage;
        state->pendingDamageCount++;
    }
}

void Network_SendDamageBatch(ConnectionState* state) {
    if (!state->isConnected || state->pendingDamageCount == 0) return;

    f64 currentTime = GetTime();
    u32 batchCount = (state->pendingDamageCount > 128) ? 128 : state->pendingDamageCount;

    PacketDamageBatch batchPacket;
    batchPacket.header.type = PACKET_DAMAGE_BATCH;
    batchPacket.header.playerIdentification = state->localPlayerIdentification;
    batchPacket.header.timestamp = currentTime;
    batchPacket.count = batchCount;
    
    for (u32 i = 0; i < batchCount; i++) {
        batchPacket.entries[i] = state->pendingDamage[i];
    }

    sendto(clientSocket, (char*)&batchPacket, sizeof(PacketHeader) + 4 + (batchCount * sizeof(DamageEntry)), 0, (struct sockaddr*)&serverAddress, sizeof(serverAddress));
    
    if (batchCount < state->pendingDamageCount) {
        memmove(state->pendingDamage, state->pendingDamage + batchCount, (state->pendingDamageCount - batchCount) * sizeof(DamageEntry));
    }
    state->pendingDamageCount -= batchCount;
}

void Network_SendXPCollect(ConnectionState* state, u32 crystalIndex) {
    if (!state->isConnected) return;

    PacketXPCollect xpPacket;
    xpPacket.header.type = PACKET_XP_COLLECT;
    xpPacket.header.playerIdentification = state->localPlayerIdentification;
    xpPacket.header.timestamp = GetTime();
    xpPacket.crystalIndex = crystalIndex;

    sendto(clientSocket, (char*)&xpPacket, sizeof(xpPacket), 0, (struct sockaddr*)&serverAddress, sizeof(serverAddress));
}

void Network_SendProjectileExplode(ConnectionState* state, u32 projectileIndex) {
    if (!state->isConnected) return;

    PacketProjectileExplode explodePacket;
    explodePacket.header.type = PACKET_PROJECTILE_EXPLODE;
    explodePacket.header.playerIdentification = state->localPlayerIdentification;
    explodePacket.header.timestamp = GetTime();
    explodePacket.projectileIndex = projectileIndex;

    sendto(clientSocket, (char*)&explodePacket, sizeof(explodePacket), 0, (struct sockaddr*)&serverAddress, sizeof(serverAddress));
}

void Network_SendAttributeUpdate(ConnectionState* state, PlayerAttributes attr) {
    if (!state->isConnected) return;

    PacketAttributeUpdate packet;
    packet.header.type = PACKET_ATTRIBUTE_UPDATE;
    packet.header.playerIdentification = state->localPlayerIdentification;
    packet.header.timestamp = GetTime();
    packet.attributes = attr;

    sendto(clientSocket, (char*)&packet, sizeof(packet), 0, (struct sockaddr*)&serverAddress, sizeof(serverAddress));
}


void Network_CloseConnection() {
    if (clientSocket != INVALID_SOCKET) {
        closesocket(clientSocket);
        WSACleanup();
    }
}
