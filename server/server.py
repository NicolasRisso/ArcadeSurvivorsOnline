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
PACKET_ENTITY_SPAWN = 6
PACKET_ENTITY_SNAPSHOT = 7

# Entity Types
ENTITY_UNDEFINED = 0
ENTITY_CHARACTER = 1

# Character Types
CHARACTER_UNDEFINED = 0
CHARACTER_PLAYER = 1
CHARACTER_ENEMY = 2

# Avoidance Config
ENEMY_AVOIDANCE_RADIUS = 45.0
ENEMY_AVOIDANCE_FORCE = 0.5

# Struct Formats (Little Endian, Packed)
HEADER_FORMAT = "<BId"
IDENTIFICATION_RESPONSE_FORMAT = HEADER_FORMAT
VELOCITY_UPDATE_FORMAT = HEADER_FORMAT + "ff"
WORLD_STATE_HEADER_FORMAT = HEADER_FORMAT + "I"
PLAYER_STATE_FORMAT = "Iff"
ENTITY_SPAWN_FORMAT = HEADER_FORMAT + "IBBff"
ENTITY_SNAPSHOT_HEADER_FORMAT = HEADER_FORMAT + "I"
SINGLE_SNAPSHOT_FORMAT = "IffI"

class Server:
    def __init__(self):
        self.serverSocket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.serverSocket.bind((SERVER_IP, SERVER_PORT))
        self.serverSocket.setblocking(False)
        self.players = {} # (address): {identification, position_x, position_y, velocity_x, velocity_y, lastHeartbeatReceived}
        self.entities = {} # {index}: {type, charType, position_x, position_y, velocity_x, velocity_y, spawnTime, targetPlayerID}
        self.nextPlayerIdentification = 1
        self.nextEntityIndex = 100 
        self.lastBroadcastTime = 0
        self.lastSnapshotTime = 0
        self.broadcastInterval = 0.05 
        self.snapshotInterval = 2.5 
        print(f"Server started on {SERVER_IP}:{SERVER_PORT}")

    def get_random_spawn_position(self, center_x=0, center_y=0, distance=1000):
        angle = random.uniform(0, 2 * math.pi)
        position_x = center_x + distance * math.cos(angle)
        position_y = center_y + distance * math.sin(angle)
        return position_x, position_y

    def run(self):
        last_time = time.time()
        while True:
            current_time = time.time()
            delta_time = current_time - last_time
            last_time = current_time

            try:
                data, address = self.serverSocket.recvfrom(2048)
                self.handle_packet(data, address)
            except BlockingIOError:
                pass
            
            self.update_server_simulation(delta_time)
            self.cleanup_disconnected_players()
            self.broadcast_world_state_snapshot()
            self.broadcast_entity_snapshots()
            time.sleep(0.01)

    def update_server_simulation(self, delta_time):
        current_time = time.time()
        
        # Move players based on their last reported velocity
        for player in self.players.values():
            player["position_x"] += player["velocity_x"] * delta_time
            player["position_y"] += player["velocity_y"] * delta_time

        # Update enemies
        for index, entity in self.entities.items():
            if entity["type"] == ENTITY_CHARACTER and entity["charType"] == CHARACTER_ENEMY:
                
                # Apply 3s delay
                if current_time - entity["spawnTime"] < 3.0:
                    continue

                # Avoidance from other enemies (calculated first)
                avoid_x, avoid_y = 0, 0
                for other_index, other in self.entities.items():
                    if index == other_index: continue
                    if other["type"] == ENTITY_CHARACTER and other["charType"] == CHARACTER_ENEMY:
                        diff_x = entity["position_x"] - other["position_x"]
                        diff_y = entity["position_y"] - other["position_y"]
                        distance = math.sqrt(diff_x*diff_x + diff_y*diff_y)
                        
                        if 0 < distance < ENEMY_AVOIDANCE_RADIUS:
                            force = (1.0 - (distance / ENEMY_AVOIDANCE_RADIUS)) * ENEMY_AVOIDANCE_FORCE
                            avoid_x += (diff_x / distance) * force
                            avoid_y += (diff_y / distance) * force

                # Random targeting
                if not entity.get("targetPlayerID"):
                    if self.players:
                        random_player = random.choice(list(self.players.values()))
                        entity["targetPlayerID"] = random_player["identification"]
                
                if entity.get("targetPlayerID"):
                    # Find the target player's current position
                    target_pos = None
                    for p in self.players.values():
                        if p["identification"] == entity["targetPlayerID"]:
                            target_pos = (p["position_x"], p["position_y"])
                            break
                    
                    if target_pos:
                        dx = target_pos[0] - entity["position_x"]
                        dy = target_pos[1] - entity["position_y"]
                        dist = math.sqrt(dx*dx + dy*dy)
                        
                        steer_x, steer_y = 0, 0
                        if dist > 1.0:
                            steer_x = dx / dist
                            steer_y = dy / dist
                        
                        # Combine pursuit and avoidance
                        final_x = steer_x + avoid_x
                        final_y = steer_y + avoid_y
                        final_len = math.sqrt(final_x*final_x + final_y*final_y)
                        
                        if final_len > 0.1:
                            speed = 150.0 # 50% of player speed
                            entity["position_x"] += (final_x / final_len) * speed * delta_time
                            entity["position_y"] += (final_y / final_len) * speed * delta_time

    def handle_packet(self, data, address):
        if len(data) < struct.calcsize(HEADER_FORMAT):
            return

        packetHeader = struct.unpack(HEADER_FORMAT, data[:struct.calcsize(HEADER_FORMAT)])
        packetType, playerIdentification, packetTimestamp = packetHeader

        if packetType == PACKET_ID_REQUEST:
            if address not in self.players:
                newIdentification = self.nextPlayerIdentification
                self.nextPlayerIdentification += 1
                spawnX, spawnY = 0, 0
                self.players[address] = {
                    "identification": newIdentification,
                    "position_x": spawnX,
                    "position_y": spawnY,
                    "velocity_x": 0.0,
                    "velocity_y": 0.0,
                    "lastHeartbeatReceived": time.time()
                }
                print(f"New player {newIdentification} connected from {address}")
                
                # Spawn 10 enemies around this new player
                for _ in range(10):
                    ex, ey = self.get_random_spawn_position(spawnX, spawnY, 800)
                    eIndex = self.nextEntityIndex
                    self.nextEntityIndex += 1
                    self.entities[eIndex] = {
                        "type": ENTITY_CHARACTER,
                        "charType": CHARACTER_ENEMY,
                        "position_x": ex,
                        "position_y": ey,
                        "spawnTime": time.time(),
                        "targetPlayerID": 0
                    }
                    self.broadcast_spawn(eIndex)
            
            player = self.players[address]
            identificationResponse = struct.pack(IDENTIFICATION_RESPONSE_FORMAT, PACKET_ID_RESPONSE, player["identification"], packetTimestamp)
            self.serverSocket.sendto(identificationResponse, address)
            
            # Send current world entities to the new player
            for eIndex, entity in self.entities.items():
                spawnPacket = struct.pack(ENTITY_SPAWN_FORMAT, PACKET_ENTITY_SPAWN, 0, time.time(), 
                                         eIndex, entity["type"], entity["charType"], 
                                         entity["position_x"], entity["position_y"])
                self.serverSocket.sendto(spawnPacket, address)

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

    def broadcast_spawn(self, entityIndex):
        entity = self.entities[entityIndex]
        packet = struct.pack(ENTITY_SPAWN_FORMAT, PACKET_ENTITY_SPAWN, 0, time.time(), 
                            entityIndex, entity["type"], entity["charType"], 
                            entity["position_x"], entity["position_y"])
        for address in self.players:
            self.serverSocket.sendto(packet, address)

    def broadcast_world_state_snapshot(self):
        currentTime = time.time()
        if currentTime - self.lastBroadcastTime < self.broadcastInterval:
            return
        self.lastBroadcastTime = currentTime

        if not self.players:
            return

        playerList = list(self.players.values())
        playerCount = len(playerList)
        
        worldStateData = struct.pack(WORLD_STATE_HEADER_FORMAT, PACKET_WORLD_STATE, 0, currentTime, playerCount)
        for player in playerList:
            worldStateData += struct.pack(PLAYER_STATE_FORMAT, player["identification"], player["velocity_x"], player["velocity_y"])

        for address in self.players:
            self.serverSocket.sendto(worldStateData, address)

    def broadcast_entity_snapshots(self):
        currentTime = time.time()
        if currentTime - self.lastSnapshotTime < self.snapshotInterval:
            return
        self.lastSnapshotTime = currentTime

        if not self.entities:
            return

        entityList = list(self.entities.items())
        count = len(entityList)
        
        snapshotData = struct.pack(ENTITY_SNAPSHOT_HEADER_FORMAT, PACKET_ENTITY_SNAPSHOT, 0, currentTime, count)
        for index, entity in entityList:
            snapshotData += struct.pack(SINGLE_SNAPSHOT_FORMAT, index, entity["position_x"], entity["position_y"], entity.get("targetPlayerID", 0))

        for address in self.players:
            self.serverSocket.sendto(snapshotData, address)

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
