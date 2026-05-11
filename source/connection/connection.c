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
    inet_pton(AF_INET, SERVER_IP, &serverAddress.sin_addr);

    connectionState->isConnected = false;
    connectionState->localPlayerIdentification = 0;
    connectionState->lastHeartbeatSent = 0;
    connectionState->lastHeartbeatReceived = GetTime();

    for (i32 entityIndex = 0; entityIndex < MAX_REMOTE_PLAYERS; entityIndex++) {
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
    char receiveBuffer[2048]; 
    struct sockaddr_in fromAddress;
    int fromAddressLength = sizeof(fromAddress);

    // Receive packets
    int bytesReceived;
    while ((bytesReceived = recvfrom(clientSocket, receiveBuffer, sizeof(receiveBuffer), 0, (struct sockaddr*)&fromAddress, &fromAddressLength)) > 0) {
        PacketHeader* packetHeader = (PacketHeader*)receiveBuffer;

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

                i32 availableSlotIndex = -1;
                for (i32 entityIndex = 0; entityIndex < MAX_REMOTE_PLAYERS; entityIndex++) {
                    if (connectionState->remoteEntities[entityIndex].entityType != ENTITY_UNDEFINED && connectionState->remoteEntities[entityIndex].identification == velocityUpdate->header.playerIdentification) {
                        availableSlotIndex = entityIndex;
                        break;
                    }
                }

                if (availableSlotIndex == -1) {
                    for (i32 entityIndex = 0; entityIndex < MAX_REMOTE_PLAYERS; entityIndex++) {
                        if (connectionState->remoteEntities[entityIndex].entityType == ENTITY_UNDEFINED) {
                            availableSlotIndex = entityIndex;
                            break;
                        }
                    }
                }

                if (availableSlotIndex != -1) {
                    connectionState->remoteEntities[availableSlotIndex].identification = velocityUpdate->header.playerIdentification;
                    connectionState->remoteEntities[availableSlotIndex].entityType = ENTITY_CHARACTER;
                    connectionState->remoteEntities[availableSlotIndex].velocity = velocityUpdate->velocity;
                }
                break;
            }
            case PACKET_WORLD_STATE: {
                PacketWorldState* worldState = (PacketWorldState*)receiveBuffer;
                for (u32 playerIndex = 0; playerIndex < worldState->count; playerIndex++) {
                    RemotePlayerState* remotePlayerState = &worldState->players[playerIndex];
                    if (remotePlayerState->identification == connectionState->localPlayerIdentification) continue;

                    i32 availableSlotIndex = -1;
                    for (i32 entityIndex = 0; entityIndex < MAX_REMOTE_PLAYERS; entityIndex++) {
                        if (connectionState->remoteEntities[entityIndex].entityType != ENTITY_UNDEFINED && connectionState->remoteEntities[entityIndex].identification == remotePlayerState->identification) {
                            availableSlotIndex = entityIndex;
                            break;
                        }
                    }

                    if (availableSlotIndex == -1) {
                        for (i32 entityIndex = 0; entityIndex < MAX_REMOTE_PLAYERS; entityIndex++) {
                            if (connectionState->remoteEntities[entityIndex].entityType == ENTITY_UNDEFINED) {
                                availableSlotIndex = entityIndex;
                                break;
                            }
                        }
                    }

                    if (availableSlotIndex != -1) {
                        connectionState->remoteEntities[availableSlotIndex].identification = remotePlayerState->identification;
                        connectionState->remoteEntities[availableSlotIndex].entityType = ENTITY_CHARACTER;
                        connectionState->remoteEntities[availableSlotIndex].velocity = remotePlayerState->velocity;
                    }
                }
                break;
            }
        }
    }

    // Heartbeat logic
    double currentTimestamp = GetTime();
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
        static double lastIdentificationRequestTimestamp = 0;
        if (currentTimestamp - lastIdentificationRequestTimestamp > 2.0) {
            PacketIDRequest identificationRequest;
            identificationRequest.header.type = PACKET_ID_REQUEST;
            identificationRequest.header.playerIdentification = 0;
            identificationRequest.header.timestamp = currentTimestamp;
            sendto(clientSocket, (char*)&identificationRequest, sizeof(identificationRequest), 0, (struct sockaddr*)&serverAddress, sizeof(serverAddress));
            lastIdentificationRequestTimestamp = currentTimestamp;
        }
    }
}

void Network_SendVelocity(ConnectionState* connectionState, Vector2 velocity) {
    if (!connectionState->isConnected) return;

    PacketVelocityUpdate velocityUpdatePacket;
    velocityUpdatePacket.header.type = PACKET_VELOCITY_UPDATE;
    velocityUpdatePacket.header.playerIdentification = connectionState->localPlayerIdentification;
    velocityUpdatePacket.header.timestamp = GetTime();
    velocityUpdatePacket.velocity = velocity;

    sendto(clientSocket, (char*)&velocityUpdatePacket, sizeof(velocityUpdatePacket), 0, (struct sockaddr*)&serverAddress, sizeof(serverAddress));
}

void Network_CloseConnection() {
    if (clientSocket != INVALID_SOCKET) {
        closesocket(clientSocket);
        WSACleanup();
    }
}
