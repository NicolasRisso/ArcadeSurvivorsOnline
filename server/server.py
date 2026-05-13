import socket
import struct
import time
import random
import math

# Constants
SERVER_IP = "0.0.0.0"
SERVER_PORT = 12345
HEARTBEAT_TIMEOUT = 30.0
MAX_ENEMIES = 3000
MAX_PLAYERS = 4
SNAPSHOT_CYCLE_TIME = 2.5
SNAPSHOT_TICKS = 50 # 2.5s / 0.05s
BATCH_SIZE = MAX_ENEMIES // SNAPSHOT_TICKS

# Packet Types
PACKET_ID_REQUEST = 0
PACKET_ID_RESPONSE = 1
PACKET_HEARTBEAT = 2
PACKET_HEARTBEAT_ACK = 3
PACKET_VELOCITY_UPDATE = 4
PACKET_WORLD_STATE = 5
PACKET_ENTITY_SPAWN = 6
PACKET_ENTITY_SNAPSHOT = 7
PACKET_ENEMY_DEATH_REPORT = 8
PACKET_ENTITY_DESPAWN = 9
PACKET_WEAPON_FIRE = 10

# Projectile Types
PROJECTILE_UNDEFINED = 0
PROJECTILE_FIREBALL = 1

# Projectile Config
PROJECTILE_SPEED = 500.0
PROJECTILE_LIFETIME = 3.0

# Character Types
CHARACTER_UNDEFINED = 0
CHARACTER_PLAYER = 1
CHARACTER_ENEMY = 2

# Entity Types
ENTITY_UNDEFINED = 0
ENTITY_CHARACTER = 1
ENTITY_PROJECTILE = 2

# Weapon Types
WEAPON_UNDEFINED = 0
WEAPON_FIREBALL_RING = 1

# Avoidance Config
ENEMY_AVOIDANCE_RADIUS = 45.0
ENEMY_AVOIDANCE_FORCE = 0.5

# Spawner Config
SPAWN_INTERVAL = 1.75
# Shapes: (weight, min_count, max_count, min_dist, max_dist)
SPAWN_CONFIG = {
    "SINGLE": (25, 1, 1, 400, 600),
    "CIRCLE": (5, 8, 14, 700, 900),
    "WALL": (3, 6, 12, 800, 1000),
    "CLUSTER": (7, 5, 10, 500, 800)
}

# Struct Formats (Little Endian, Packed)
HEADER_FORMAT = "<BId"
IDENTIFICATION_RESPONSE_FORMAT = HEADER_FORMAT
VELOCITY_UPDATE_FORMAT = HEADER_FORMAT + "ff"
WORLD_STATE_HEADER_FORMAT = HEADER_FORMAT + "I"
PLAYER_STATE_FORMAT = "Iffff"
ENTITY_SPAWN_FORMAT = HEADER_FORMAT + "IBBffIff"
ENTITY_SNAPSHOT_HEADER_FORMAT = HEADER_FORMAT + "HH"
SINGLE_SNAPSHOT_FORMAT = "ff"
ENEMY_DEATH_REPORT_HEADER_FORMAT = HEADER_FORMAT + "I"
ENTITY_DESPAWN_FORMAT = HEADER_FORMAT + "I"
WEAPON_FIRE_FORMAT = HEADER_FORMAT + "B"

class Server:
    def __init__(self):
        self.serverSocket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.serverSocket.bind((SERVER_IP, SERVER_PORT))
        self.serverSocket.setblocking(False)
        self.players = {} # (address): {identification, position_x, position_y, velocity_x, velocity_y, lastHeartbeatReceived}
        self.entities = {} # {index}: {type, charType, position_x, position_y, velocity_x, velocity_y, spawnTime, targetPlayerID}
        self.nextPlayerIdentification = 1
        self.nextEntityIndex = MAX_PLAYERS 
        self.lastBroadcastTime = 0
        self.snapshot_tick_index = 0
        self.last_spawner_time = 0
        self.broadcastInterval = 0.05 
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
            self.update_spawner(current_time)
            self.cleanup_disconnected_players()
            self.broadcast_world_state_snapshot()
            self.broadcast_entity_snapshots() # Called every tick now
            time.sleep(0.01)

    def update_server_simulation(self, delta_time):
        current_time = time.time()
        
        # Move players based on their last reported velocity
        for player in self.players.values():
            player["position_x"] += player["velocity_x"] * delta_time
            player["position_y"] += player["velocity_y"] * delta_time

        # Update enemies
        for index, entity in list(self.entities.items()):
            if entity["type"] == ENTITY_CHARACTER and entity["charType"] == CHARACTER_ENEMY:
                
                # Avoidance from other enemies
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
                    target_found = False
                    for p in self.players.values():
                        if p["identification"] == entity["targetPlayerID"]:
                            target_pos = (p["position_x"], p["position_y"])
                            target_found = True
                            break
                    
                    # If the player is gone, clear the target to trigger re-targeting next tick
                    if not target_found:
                        entity["targetPlayerID"] = 0
                        continue
                    
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
                        
                        # Normalize final vector
                        final_len = math.sqrt(final_x*final_x + final_y*final_y)
                        if final_len > 0.001:
                            final_x /= final_len
                            final_y /= final_len
                        else:
                            final_x, final_y = 0, 0
                        
                        speed = 150.0
                        entity["position_x"] += final_x * speed * delta_time
                        entity["position_y"] += final_y * speed * delta_time

        # Update projectiles
        for index, entity in list(self.entities.items()):
            if entity["type"] == ENTITY_PROJECTILE:
                # Move
                entity["position_x"] += entity["velocity_x"] * delta_time
                entity["position_y"] += entity["velocity_y"] * delta_time
                
                # Lifetime check
                if current_time - entity["spawnTime"] > PROJECTILE_LIFETIME:
                    del self.entities[index]
                    self.broadcast_despawn(index)

    def update_spawner(self, current_time):
        if current_time - self.last_spawner_time < SPAWN_INTERVAL:
            return
        
        if len(self.entities) >= MAX_ENEMIES:
            return

        if not self.players:
            return
            
        self.last_spawner_time = current_time
        
        # Spawn a random group for each player
        available_shapes = list(SPAWN_CONFIG.keys())
        for player in self.players.values():
            if not available_shapes:
                available_shapes = list(SPAWN_CONFIG.keys())
            
            # Weighted random selection from available shapes (to avoid immediate repeats)
            weights = [SPAWN_CONFIG[s][0] for s in available_shapes]
            shape = random.choices(available_shapes, weights=weights, k=1)[0]
            available_shapes.remove(shape)
            
            self.spawn_random_group(player, shape)

    def spawn_random_group(self, target_player, shape):
        px, py = target_player["position_x"], target_player["position_y"]
        
        # 3. Determine count and base distance from config
        _, min_c, max_c, min_d, max_d = SPAWN_CONFIG[shape]
        count = random.randint(min_c, max_c)
        distance = random.uniform(min_d, max_d)
        
        positions = []
        
        if shape == "SINGLE":
            positions.append(self.get_random_spawn_position(px, py, distance))
            
        elif shape == "CIRCLE":
            start_angle = random.uniform(0, 2 * math.pi)
            for i in range(count):
                angle = start_angle + (2 * math.pi * i / count)
                positions.append((px + distance * math.cos(angle), py + distance * math.sin(angle)))
                
        elif shape == "WALL":
            angle = random.uniform(0, 2 * math.pi)
            center_x = px + distance * math.cos(angle)
            center_y = py + distance * math.sin(angle)
            
            # Tangent direction
            tx = -math.sin(angle)
            ty = math.cos(angle)
            
            spacing = 40.0
            for i in range(count):
                offset = (i - count/2) * spacing
                positions.append((center_x + tx * offset, center_y + ty * offset))
                
        elif shape == "CLUSTER":
            # Center of the cluster
            cx, cy = self.get_random_spawn_position(px, py, distance)
            for _ in range(count):
                ox = random.uniform(-50, 50)
                oy = random.uniform(-50, 50)
                positions.append((cx + ox, cy + oy))
        
        # 4. Spawn the entities
        for rx, ry in positions:
            eIndex = self.nextEntityIndex
            self.nextEntityIndex += 1
            if self.nextEntityIndex >= MAX_ENEMIES + MAX_PLAYERS:
                self.nextEntityIndex = MAX_PLAYERS
            
            self.entities[eIndex] = {
                "type": ENTITY_CHARACTER,
                "charType": CHARACTER_ENEMY,
                "position_x": rx,
                "position_y": ry,
                "spawnTime": time.time(),
                "targetPlayerID": target_player["identification"]
            }
            self.broadcast_spawn(eIndex)
            
        print(f"Spawned {count} enemies in {shape} pattern around player {target_player['identification']}")

    def handle_packet(self, data, address):
        if len(data) < struct.calcsize(HEADER_FORMAT):
            return

        packetHeader = struct.unpack(HEADER_FORMAT, data[:struct.calcsize(HEADER_FORMAT)])
        packetType, playerIdentification, packetTimestamp = packetHeader

        if packetType == PACKET_ID_REQUEST:
            print(f"Received ID request from {address}")
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
                # (Removed legacy spawn 10 enemies on connect - now handled by 2.5s spawner)
            
            player = self.players[address]
            identificationResponse = struct.pack(IDENTIFICATION_RESPONSE_FORMAT, PACKET_ID_RESPONSE, player["identification"], packetTimestamp)
            self.serverSocket.sendto(identificationResponse, address)
            
            # Send current world entities to the new player
            for eIndex in self.entities:
                spawnPacket = self.get_spawn_packet(eIndex)
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

        elif packetType == PACKET_ENEMY_DEATH_REPORT:
            if address in self.players:
                if len(data) >= struct.calcsize(ENEMY_DEATH_REPORT_HEADER_FORMAT):
                    _, _, _, count = struct.unpack(ENEMY_DEATH_REPORT_HEADER_FORMAT, data[:struct.calcsize(ENEMY_DEATH_REPORT_HEADER_FORMAT)])
                    
                    id_format = "I" * count
                    ids = struct.unpack("<" + id_format, data[struct.calcsize(ENEMY_DEATH_REPORT_HEADER_FORMAT):struct.calcsize(ENEMY_DEATH_REPORT_HEADER_FORMAT) + (count * 4)])
                    
                    for eIndex in ids:
                        if eIndex in self.entities:
                            # Verify it's an enemy (security)
                            if self.entities[eIndex]["charType"] == CHARACTER_ENEMY:
                                print(f"Enemy {eIndex} killed by player {playerIdentification}")
                                del self.entities[eIndex]
                                self.broadcast_despawn(eIndex)

        elif packetType == PACKET_WEAPON_FIRE:
            if address in self.players:
                if len(data) >= struct.calcsize(WEAPON_FIRE_FORMAT):
                    _, _, _, weaponType = struct.unpack(WEAPON_FIRE_FORMAT, data[:struct.calcsize(WEAPON_FIRE_FORMAT)])
                    player = self.players[address]
                    
                    if weaponType == WEAPON_FIREBALL_RING: # Need to define WEAPON_FIREBALL_RING or use 1
                        self.fire_fireball_ring(player)

    def fire_fireball_ring(self, player):
        directions = [
            (0, -1), # North
            (0, 1),  # South
            (1, 0),  # East
            (-1, 0)  # West
        ]
        
        for dx, dy in directions:
            eIndex = self.nextEntityIndex
            self.nextEntityIndex += 1
            if self.nextEntityIndex >= MAX_ENEMIES + MAX_PLAYERS:
                self.nextEntityIndex = MAX_PLAYERS
            
            self.entities[eIndex] = {
                "type": ENTITY_PROJECTILE,
                "charType": PROJECTILE_FIREBALL,
                "position_x": player["position_x"],
                "position_y": player["position_y"],
                "velocity_x": dx * PROJECTILE_SPEED,
                "velocity_y": dy * PROJECTILE_SPEED,
                "spawnTime": time.time(),
                "ownerID": player["identification"]
            }
            self.broadcast_spawn(eIndex)

    def get_spawn_packet(self, entityIndex):
        entity = self.entities[entityIndex]
        return struct.pack(ENTITY_SPAWN_FORMAT, PACKET_ENTITY_SPAWN, 0, time.time(), 
                            entityIndex, entity["type"], entity["charType"], 
                            entity["position_x"], entity["position_y"],
                            entity.get("targetPlayerID", 0) if entity["type"] == ENTITY_CHARACTER else entity.get("ownerID", 0),
                            entity.get("velocity_x", 0), entity.get("velocity_y", 0))

    def broadcast_spawn(self, entityIndex):
        packet = self.get_spawn_packet(entityIndex)
        for address in self.players:
            self.serverSocket.sendto(packet, address)

    def broadcast_despawn(self, entityIndex):
        packet = struct.pack(ENTITY_DESPAWN_FORMAT, PACKET_ENTITY_DESPAWN, 0, time.time(), entityIndex)
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
            worldStateData += struct.pack(PLAYER_STATE_FORMAT, 
                                          player["identification"], 
                                          player["position_x"], player["position_y"],
                                          player["velocity_x"], player["velocity_y"])

        for address in self.players:
            self.serverSocket.sendto(worldStateData, address)

    def broadcast_entity_snapshots(self):
        currentTime = time.time()
        # We broadcast every tick, but only a slice of enemies
        if currentTime - self.lastBroadcastTime < self.broadcastInterval:
            return
        
        if not self.players:
            return

        # Calculate range for this tick
        first_index = MAX_PLAYERS + (self.snapshot_tick_index * BATCH_SIZE)
        last_index = first_index + BATCH_SIZE
        
        # Gather positions for entities in this range
        positions = []
        for i in range(first_index, last_index):
            entity = self.entities.get(i)
            if entity and entity["charType"] == CHARACTER_ENEMY:
                positions.append((entity["position_x"], entity["position_y"]))
            else:
                # If no enemy, we send (0,0) as a placeholder to keep sequence aligned
                # The client will ignore indices that don't have an active enemy
                positions.append((0.0, 0.0))

        # Pack the header: Type, ID(0), Timestamp, firstID, count
        snapshotData = struct.pack(ENTITY_SNAPSHOT_HEADER_FORMAT, PACKET_ENTITY_SNAPSHOT, 0, currentTime, first_index, len(positions))
        
        # Pack positions
        for pos in positions:
            snapshotData += struct.pack(SINGLE_SNAPSHOT_FORMAT, pos[0], pos[1])

        for address in self.players:
            self.serverSocket.sendto(snapshotData, address)
            
        # Increment tick index for next time
        self.snapshot_tick_index = (self.snapshot_tick_index + 1) % SNAPSHOT_TICKS

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
