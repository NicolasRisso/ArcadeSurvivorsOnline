import socket
import struct
import time
import random
import math

# Constants
SERVER_IP = "0.0.0.0"
SERVER_PORT = 12345
HEARTBEAT_TIMEOUT = 30.0

# Packet Types
PACKET_ID_REQUEST = 0
PACKET_ID_RESPONSE = 1
PACKET_HEARTBEAT = 2
PACKET_HEARTBEAT_ACK = 3
PACKET_VELOCITY_UPDATE = 4
PACKET_WORLD_STATE = 5

# Struct Formats (Little Endian, Packed)
# Header: type (B), playerIdentification (I), timestamp (d)
HEADER_FORMAT = "<BId"
# ID Response: Header only
IDENTIFICATION_RESPONSE_FORMAT = HEADER_FORMAT
# Velocity Update: Header + velocity_x (f), velocity_y (f)
VELOCITY_UPDATE_FORMAT = HEADER_FORMAT + "ff"
# World State: Header + count (I)
WORLD_STATE_HEADER_FORMAT = HEADER_FORMAT + "I"
# Player State in World State: identification (I), velocity_x (f), velocity_y (f)
PLAYER_STATE_FORMAT = "Iff"

class Server:
    def __init__(self):
        self.serverSocket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.serverSocket.bind((SERVER_IP, SERVER_PORT))
        self.serverSocket.setblocking(False)
        self.players = {} # (address): {identification, x, y, velocity_x, velocity_y, lastHeartbeatReceived}
        self.nextPlayerIdentification = 1
        self.lastBroadcastTime = 0
        self.broadcastInterval = 0.05 # 20Hz
        print(f"Server started on {SERVER_IP}:{SERVER_PORT}")

    def get_random_spawn_position(self):
        angle = random.uniform(0, 2 * math.pi)
        radius = random.uniform(0, 500)
        position_x = radius * math.cos(angle)
        position_y = radius * math.sin(angle)
        return position_x, position_y

    def run(self):
        while True:
            try:
                data, address = self.serverSocket.recvfrom(2048)
                self.handle_packet(data, address)
            except BlockingIOError:
                pass
            
            self.cleanup_disconnected_players()
            self.broadcast_world_state_snapshot()
            time.sleep(0.01)

    def handle_packet(self, data, address):
        if len(data) < struct.calcsize(HEADER_FORMAT):
            return

        packetHeader = struct.unpack(HEADER_FORMAT, data[:struct.calcsize(HEADER_FORMAT)])
        packetType, playerIdentification, packetTimestamp = packetHeader

        if packetType == PACKET_ID_REQUEST:
            if address not in self.players:
                newIdentification = self.nextPlayerIdentification
                self.nextPlayerIdentification += 1
                spawnX, spawnY = self.get_random_spawn_position()
                self.players[address] = {
                    "identification": newIdentification,
                    "position_x": spawnX,
                    "position_y": spawnY,
                    "velocity_x": 0.0,
                    "velocity_y": 0.0,
                    "lastHeartbeatReceived": time.time()
                }
                print(f"New player {newIdentification} connected from {address}")
            
            player = self.players[address]
            identificationResponse = struct.pack(IDENTIFICATION_RESPONSE_FORMAT, PACKET_ID_RESPONSE, player["identification"], packetTimestamp)
            self.serverSocket.sendto(identificationResponse, address)

        elif packetType == PACKET_HEARTBEAT:
            if address in self.players:
                self.players[address]["lastHeartbeatReceived"] = time.time()
                heartbeatAcknowledgment = struct.pack(HEADER_FORMAT, PACKET_HEARTBEAT_ACK, self.players[address]["identification"], packetTimestamp)
                self.serverSocket.sendto(heartbeatAcknowledgment, address)

        elif packetType == PACKET_VELOCITY_UPDATE:
            if address in self.players:
                if len(data) >= struct.calcsize(VELOCITY_UPDATE_FORMAT):
                    _, _, _, velocityX, velocityY = struct.unpack(VELOCITY_UPDATE_FORMAT, data[:struct.calcsize(VELOCITY_UPDATE_FORMAT)])
                    self.players[address]["velocity_x"] = velocityX
                    self.players[address]["velocity_y"] = velocityY
                    self.players[address]["lastHeartbeatReceived"] = time.time()

    def broadcast_world_state_snapshot(self):
        currentTime = time.time()
        if currentTime - self.lastBroadcastTime < self.broadcastInterval:
            return
        self.lastBroadcastTime = currentTime

        if not self.players:
            return

        # Prepare world state packet
        playerList = list(self.players.values())
        playerCount = len(playerList)
        
        # Header: type, playerIdentification (0 for server), timestamp, count
        worldStateData = struct.pack(WORLD_STATE_HEADER_FORMAT, PACKET_WORLD_STATE, 0, currentTime, playerCount)
        
        for player in playerList:
            worldStateData += struct.pack(PLAYER_STATE_FORMAT, player["identification"], player["velocity_x"], player["velocity_y"])

        # Broadcast to all connected addresses
        for address in self.players:
            self.serverSocket.sendto(worldStateData, address)

    def cleanup_disconnected_players(self):
        currentTime = time.time()
        addressesToRemove = []
        for address, player in self.players.items():
            if currentTime - player["lastHeartbeatReceived"] > HEARTBEAT_TIMEOUT:
                print(f"Player {player['identification']} timed out")
                addressesToRemove.append(address)
        
        for address in addressesToRemove:
            del self.players[address]

if __name__ == "__main__":
    server = Server()
    server.run()
