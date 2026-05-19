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
MAX_XP_CRYSTALS = 2000
MAX_PLAYERS = 4
SNAPSHOT_CYCLE_TIME = 2.5
SNAPSHOT_TICKS = 50 # 2.5s / 0.05s
BATCH_SIZE = MAX_ENEMIES // SNAPSHOT_TICKS
XP_START_INDEX = MAX_PLAYERS + MAX_ENEMIES
XP_END_INDEX = XP_START_INDEX + MAX_XP_CRYSTALS

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
PACKET_ENTITY_DAMAGE = 11
PACKET_PROJECTILE_EXPLODE = 12
PACKET_DAMAGE_BATCH = 13
PACKET_XP_COLLECT = 14
PACKET_ATTRIBUTE_UPDATE = 15
PACKET_NOTIFICATION = 16

# Projectile Types
PROJECTILE_UNDEFINED = 0
PROJECTILE_FIREBALL = 1
PROJECTILE_CRYSTAL = 2
PROJECTILE_BOMB = 3
PROJECTILE_SPIKE = 4
PROJECTILE_EXPLOSION = 5

# Projectile Config
PROJECTILE_SPEED = 500.0
PROJECTILE_LIFETIME = 3.0
CRYSTAL_SPEED = 800.0
CRYSTAL_LIFETIME = 5.0
BOMB_DELAY = 2.0
BOMB_LIFETIME = 2.5 # slightly more than delay to ensure it stays until explosion logic
SPIKE_LIFETIME = 3.0

# Damage values
DAMAGE_FIREBALL = 50.0
DAMAGE_CRYSTAL = 100.0
DAMAGE_AURA = 15.0 # per tick
DAMAGE_BOMB = 500.0
DAMAGE_SPIKE = 20.0 # per tick
SPIKE_MAX_DAMAGE = 150.0

FIREBALL_RADIUS = 50.0
BOMB_RADIUS = 150.0

ENEMY_HEALTH = 100.0

# Character Types
CHARACTER_UNDEFINED = 0
CHARACTER_PLAYER = 1
CHARACTER_ENEMY = 2

# Enemy Classes
ENEMY_CLASS_NORMAL = 0
ENEMY_CLASS_FAST = 1
ENEMY_CLASS_TANK = 2
ENEMY_CLASS_BOSS = 3

# Entity Types
ENTITY_UNDEFINED = 0
ENTITY_CHARACTER = 1
ENTITY_PROJECTILE = 2
ENTITY_XP_CRYSTAL = 3

# Weapon Types
WEAPON_UNDEFINED = 0
WEAPON_FIREBALL_RING = 1
WEAPON_CRYSTAL_STAFF = 2
WEAPON_DEATH_AURA = 3
WEAPON_BOMB_SHOES = 4
WEAPON_NATURE_SPIKES = 5

# Avoidance Config
ENEMY_AVOIDANCE_RADIUS = 45.0
ENEMY_AVOIDANCE_FORCE = 0.5

# Spawner Config
SPAWN_INTERVAL = 1.75
# Shapes: (weight, min_count, max_count, min_dist, max_dist, enemy_class, unlock_difficulty)
SPAWN_CONFIG = {
    "SINGLE": (20, 1, 1, 400, 600, ENEMY_CLASS_NORMAL, 0.0),
    "CIRCLE": (5, 8, 14, 700, 900, ENEMY_CLASS_NORMAL, 10.0),
    "WALL": (3, 6, 12, 800, 1000, ENEMY_CLASS_NORMAL, 20.0),
    "CLUSTER": (6, 5, 10, 500, 800, ENEMY_CLASS_NORMAL, 2.0),
    
    # Fast groups
    "SINGLE_FAST": (15, 1, 3, 400, 600, ENEMY_CLASS_FAST, 5.0),
    "CIRCLE_FAST": (3, 6, 10, 700, 900, ENEMY_CLASS_FAST, 25.0),
    "CLUSTER_FAST": (5, 4, 8, 500, 800, ENEMY_CLASS_FAST, 15.0),
    
    # Tank groups
    "SINGLE_TANK": (10, 1, 1, 400, 600, ENEMY_CLASS_TANK, 30.0),
    "CLUSTER_TANK": (3, 2, 4, 500, 800, ENEMY_CLASS_TANK, 40.0),
}

# Struct Formats (Little Endian, Packed)
HEADER_FORMAT = "<BId"
IDENTIFICATION_RESPONSE_FORMAT = HEADER_FORMAT
VELOCITY_UPDATE_FORMAT = HEADER_FORMAT + "ffff"
WORLD_STATE_HEADER_FORMAT = HEADER_FORMAT + "I"
PLAYER_STATE_FORMAT = "<IffffBf"
ENTITY_SPAWN_FORMAT = HEADER_FORMAT + "IBBffIffffi"
ENTITY_SNAPSHOT_HEADER_FORMAT = HEADER_FORMAT + "HH"
SINGLE_SNAPSHOT_FORMAT = "ff"
ENEMY_DEATH_REPORT_HEADER_FORMAT = HEADER_FORMAT + "I"
ENTITY_DESPAWN_FORMAT = HEADER_FORMAT + "I"
WEAPON_FIRE_FORMAT = HEADER_FORMAT + "Bffi"
ENTITY_DAMAGE_FORMAT = HEADER_FORMAT + "If"
PACKET_PROJECTILE_EXPLODE_FORMAT = HEADER_FORMAT + "I"
DAMAGE_BATCH_HEADER_FORMAT = HEADER_FORMAT + "I"
DAMAGE_ENTRY_FORMAT = "If"
XP_COLLECT_FORMAT = HEADER_FORMAT + "I"
ATTRIBUTE_UPDATE_FORMAT = HEADER_FORMAT + "fffffff"
NOTIFICATION_FORMAT = HEADER_FORMAT + "64sBBBffB"

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
        self.log_buffer = []
        self.last_log_flush_time = time.time()
        self.start_time = None
        self.difficulty = 0.0
        self.last_swarm_warning_cycle = -1
        self.last_boss_warning_cycle = -1
        self.last_boss_spawn_cycle = -1
        self.is_swarm_active = False
        print(f"Server started on {SERVER_IP}:{SERVER_PORT}")

    def log(self, message):
        self.log_buffer.append(f"[{time.strftime('%H:%M:%S')}] {message}")
        if len(self.log_buffer) > 1000: # Safety flush
            self.flush_logs()

    def flush_logs(self):
        if self.log_buffer:
            print("\n".join(self.log_buffer))
            self.log_buffer = []
        self.last_log_flush_time = time.time()

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

            # 1. Process ALL available packets
            while True:
                try:
                    data, address = self.serverSocket.recvfrom(2048)
                    self.handle_packet(data, address)
                except BlockingIOError:
                    break
                except Exception as e:
                    self.log(f"Error receiving packet: {e}")
                    break
            
            # 2. Update Simulation
            self.update_server_simulation(delta_time)
            self.update_events(current_time)
            self.update_spawner(current_time)
            self.cleanup_disconnected_players()
            self.broadcast_world_state_snapshot()
            self.broadcast_entity_snapshots() 
            
            # 3. Periodic Log Flush
            if current_time - self.last_log_flush_time > 1.0:
                self.flush_logs()
                
            time.sleep(0.01)

    def update_server_simulation(self, delta_time):
        current_time = time.time()
        
        # Move players based on their last reported velocity
        for player in self.players.values():
            if player.get("health", 100.0) <= 0.0:
                # Server Authoritative Respawn
                self.broadcast_notification(f"Player {player['identification']} Died", 255, 0, 0, 5.0, 1.0, False)
                max_health = player.get("attributes", [100.0])[0] if player.get("attributes") else 100.0
                player["health"] = max_health
                player["position_x"] = 0.0
                player["position_y"] = 0.0
                player["velocity_x"] = 0.0
                player["velocity_y"] = 0.0
                player["iframe_until"] = current_time + 2.0
                print(f"[DEATH] Authoritative server respawned Player {player['identification']} at (0, 0)")
            else:
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
                            
                            # If target player is dead, select alternative alive player deterministically
                            if p.get("health", 100.0) <= 0.0:
                                alive_players = [pl["identification"] for pl in self.players.values() if pl.get("health", 100.0) > 0.0]
                                if alive_players:
                                    entity["targetPlayerID"] = alive_players[index % len(alive_players)]
                                    # Reset to new target player
                                    for pl in self.players.values():
                                        if pl["identification"] == entity["targetPlayerID"]:
                                            target_pos = (pl["position_x"], pl["position_y"])
                                            break
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
                        
                        speed = entity.get("speed", 150.0)
                        entity["position_x"] += final_x * speed * delta_time
                        entity["position_y"] += final_y * speed * delta_time

        # Update projectiles
        for index, entity in list(self.entities.items()):
            if entity["type"] == ENTITY_PROJECTILE:
                # Move
                entity["position_x"] += entity["velocity_x"] * delta_time
                entity["position_y"] += entity["velocity_y"] * delta_time
                
                # Lifetime check
                spawn_duration = current_time - entity["spawnTime"]
                projectile_type = entity["charType"]
                
                max_lifetime = PROJECTILE_LIFETIME # Default 3.0
                if projectile_type == PROJECTILE_CRYSTAL: max_lifetime = CRYSTAL_LIFETIME
                elif projectile_type == PROJECTILE_BOMB: max_lifetime = BOMB_DELAY # Explode exactly at delay
                elif projectile_type == PROJECTILE_SPIKE: max_lifetime = SPIKE_LIFETIME
                elif projectile_type == PROJECTILE_EXPLOSION: max_lifetime = 0.5
                
                if spawn_duration > max_lifetime:
                    if projectile_type == PROJECTILE_BOMB:
                        self.spawn_explosion(entity["position_x"], entity["position_y"], BOMB_RADIUS, entity.get("ownerID", 0))
                    del self.entities[index]
                    self.broadcast_despawn(index)

    def update_spawner(self, current_time):
        if not self.players:
            self.start_time = None
            self.difficulty = 0.0
            self.last_swarm_warning_cycle = -1
            self.last_boss_warning_cycle = -1
            self.last_boss_spawn_cycle = -1
            self.is_swarm_active = False
            return

        if self.start_time is None:
            self.start_time = current_time
        
        self.difficulty = (current_time - self.start_time) / 6.0

        difficulty_mult = 1.0 + (self.difficulty / 20.0) * 1.1
        swarm_mult = 2.0 if self.is_swarm_active else 1.0
        actual_interval = SPAWN_INTERVAL / (difficulty_mult * swarm_mult)

        if current_time - self.last_spawner_time < actual_interval:
            return
        
        if len(self.entities) >= MAX_ENEMIES:
            return

        self.last_spawner_time = current_time
        
        # Filter available spawn groups based on difficulty progression
        filtered_shapes = []
        for shape, config in SPAWN_CONFIG.items():
            unlock_diff = config[6]
            if self.difficulty >= unlock_diff:
                filtered_shapes.append(shape)
        if not filtered_shapes:
            filtered_shapes = ["SINGLE"]

        # Spawn a random group for each player from the unlocked shapes
        available_shapes = list(filtered_shapes)
        for player in self.players.values():
            if not available_shapes:
                available_shapes = list(filtered_shapes)
            
            # Weighted random selection from available shapes (to avoid immediate repeats)
            weights = [SPAWN_CONFIG[s][0] for s in available_shapes]
            shape = random.choices(available_shapes, weights=weights, k=1)[0]
            available_shapes.remove(shape)
            
            self.spawn_random_group(player, shape)

    def spawn_random_group(self, target_player, shape):
        px, py = target_player["position_x"], target_player["position_y"]
        
        # 3. Determine count and base distance from config
        _, min_c, max_c, min_d, max_d, enemy_class, _ = SPAWN_CONFIG[shape]
        count = random.randint(min_c, max_c)
        distance = random.uniform(min_d, max_d)
        
        positions = []
        
        base_shape = "SINGLE"
        if "CIRCLE" in shape: base_shape = "CIRCLE"
        elif "WALL" in shape: base_shape = "WALL"
        elif "CLUSTER" in shape: base_shape = "CLUSTER"
        
        if base_shape == "SINGLE":
            positions.append(self.get_random_spawn_position(px, py, distance))
            
        elif base_shape == "CIRCLE":
            start_angle = random.uniform(0, 2 * math.pi)
            for i in range(count):
                angle = start_angle + (2 * math.pi * i / count)
                positions.append((px + distance * math.cos(angle), py + distance * math.sin(angle)))
                
        elif base_shape == "WALL":
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
                
        elif base_shape == "CLUSTER":
            # Center of the cluster
            cx, cy = self.get_random_spawn_position(px, py, distance)
            for _ in range(count):
                ox = random.uniform(-50, 50)
                oy = random.uniform(-50, 50)
                positions.append((cx + ox, cy + oy))
        
        # Determine stats based on enemy class
        hp = ENEMY_HEALTH
        speed = 150.0
        xp_value = 20.0
        
        if enemy_class == ENEMY_CLASS_FAST:
            hp = ENEMY_HEALTH * 0.6
            speed = 225.0
            xp_value = 40.0
        elif enemy_class == ENEMY_CLASS_TANK:
            hp = ENEMY_HEALTH * 3.0
            speed = 45.0  # Slower tank: half of 90.0
            xp_value = 100.0
            
        # Apply progressive difficulty scaling
        stat_mult = 1.0 + (self.difficulty / 20.0) * 1.25
        xp_mult = 1.0 + (self.difficulty / 30.0) * 1.25
        speed_mult = 1.0 + (self.difficulty / 20.0) * 1.05
        
        hp *= stat_mult
        speed *= speed_mult
        xp_value *= xp_mult
        
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
                "targetPlayerID": target_player["identification"],
                "health": hp,
                "max_health": hp,
                "enemyClass": enemy_class,
                "speed": speed,
                "xp_value": xp_value
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
                    "lastHeartbeatReceived": time.time(),
                    "weapons_mask": 0,
                    "attributes": (100.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.0),
                    "health": 100.0,
                    "iframe_until": 0.0
                }
                print(f"New player {newIdentification} connected from {address}")
            
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
                    _, _, _, posX, posY, velocityX, velocityY = struct.unpack(VELOCITY_UPDATE_FORMAT, data[:struct.calcsize(VELOCITY_UPDATE_FORMAT)])
                    
                    player = self.players[address]
                    # Verify player movement: check distance between client position and server prediction
                    dx = posX - player["position_x"]
                    dy = posY - player["position_y"]
                    dist = math.sqrt(dx*dx + dy*dy)
                    
                    # Accept position update only if distance is within standard limits
                    # (Allow initial spawn to bypass check when server position is 0.0)
                    if dist <= 100.0 or (player["position_x"] == 0.0 and player["position_y"] == 0.0):
                        player["position_x"] = posX
                        player["position_y"] = posY
                    
                    player["velocity_x"] = velocityX
                    player["velocity_y"] = velocityY
                    player["lastHeartbeatReceived"] = time.time()

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
                                xp_val = self.entities[eIndex].get("xp_value", 20.0)
                                ex, ey = self.entities[eIndex]["position_x"], self.entities[eIndex]["position_y"]
                                del self.entities[eIndex]
                                self.broadcast_despawn(eIndex)
                                self.spawn_xp_crystal(ex, ey, xp_val)

        elif packetType == PACKET_DAMAGE_BATCH:
            if address in self.players:
                header_size = struct.calcsize(DAMAGE_BATCH_HEADER_FORMAT)
                if len(data) >= header_size:
                    _, _, _, count = struct.unpack(DAMAGE_BATCH_HEADER_FORMAT, data[:header_size])
                    entry_size = struct.calcsize(DAMAGE_ENTRY_FORMAT)
                    for i in range(count):
                        start = header_size + (i * entry_size)
                        if start + entry_size > len(data): break
                        eIndex, damage = struct.unpack("<" + DAMAGE_ENTRY_FORMAT, data[start:start+entry_size])
                        
                        # Check if eIndex belongs to a player
                        target_player = None
                        for p in self.players.values():
                            if p["identification"] == eIndex:
                                target_player = p
                                break
                                
                        if target_player:
                            current_time = time.time()
                            if current_time >= target_player.get("iframe_until", 0.0):
                                # Verify if any active enemy is close to the player to validate collision
                                px, py = target_player["position_x"], target_player["position_y"]
                                enemy_found = False
                                base_damage = 10.0
                                for ent in self.entities.values():
                                    if ent["type"] == ENTITY_CHARACTER and ent["charType"] == CHARACTER_ENEMY:
                                        dx = px - ent["position_x"]
                                        dy = py - ent["position_y"]
                                        dist = math.sqrt(dx*dx + dy*dy)
                                        if dist <= 120.0: # generous threshold to account for network latency/interpolation
                                            enemy_found = True
                                            if ent.get("enemyClass") == ENEMY_CLASS_BOSS:
                                                base_damage = 40.0
                                            break
                                            
                                if enemy_found:
                                    stat_mult = 1.0 + (self.difficulty / 20.0) * 1.25
                                    expected_damage = base_damage * stat_mult
                                    
                                    target_player["health"] -= expected_damage
                                    target_player["iframe_until"] = current_time + 0.5
                                    if target_player["health"] <= 0.0:
                                        self.broadcast_notification(f"Player {eIndex} Died", 255, 0, 0, 5.0, 1.0, False)
                                        max_health = target_player.get("attributes", [100.0])[0] if target_player.get("attributes") else 100.0
                                        target_player["health"] = max_health
                                        target_player["position_x"] = 0.0
                                        target_player["position_y"] = 0.0
                                        target_player["velocity_x"] = 0.0
                                        target_player["velocity_y"] = 0.0
                                        target_player["iframe_until"] = current_time + 2.0
                                        print(f"[DEATH] Authoritative server respawned Player {eIndex} instantly at (0, 0)")
                                    else:
                                        # Broadcast damage to other players so they display the damage visual effect!
                                        self.broadcast_damage(eIndex, expected_damage, 0)
                        
                        elif eIndex in self.entities:
                            entity = self.entities[eIndex]
                            if entity["type"] == ENTITY_CHARACTER and entity["charType"] == CHARACTER_ENEMY:
                                entity["health"] -= damage
                                if entity["health"] <= 0:
                                    xp_val = entity.get("xp_value", 20.0)
                                    ex, ey = entity["position_x"], entity["position_y"]
                                    del self.entities[eIndex]
                                    self.broadcast_despawn(eIndex)
                                    self.spawn_xp_crystal(ex, ey, xp_val)
                                else:
                                    self.broadcast_damage(eIndex, damage, playerIdentification)

        elif packetType == PACKET_WEAPON_FIRE:
            if address in self.players:
                if len(data) >= struct.calcsize(WEAPON_FIRE_FORMAT):
                    header = data[:struct.calcsize(HEADER_FORMAT)]
                    payload = data[struct.calcsize(HEADER_FORMAT):]
                    weaponType, damage, radius, extra = struct.unpack("<Bffi", payload)
                    player = self.players[address]
                    
                    px, py = player["position_x"], player["position_y"]
                    player_id = player["identification"]

                    if weaponType == WEAPON_FIREBALL_RING:
                        num_fireballs = 8
                        for i in range(num_fireballs):
                            angle = (i / num_fireballs) * 2 * math.pi
                            vx = math.cos(angle) * 500
                            vy = math.sin(angle) * 500
                            self.spawn_projectile(ENTITY_PROJECTILE, PROJECTILE_FIREBALL, px, py, vx, vy, player_id, damage, radius)
                            
                    elif weaponType == WEAPON_CRYSTAL_STAFF:
                        # Find closest enemy to aim staff at
                        closest_enemy = None
                        min_dist = float("inf")
                        for ent in self.entities.values():
                            if ent["type"] == ENTITY_CHARACTER and ent["charType"] == CHARACTER_ENEMY:
                                dx = ent["position_x"] - px
                                dy = ent["position_y"] - py
                                dist = math.sqrt(dx*dx + dy*dy)
                                if dist < min_dist:
                                    min_dist = dist
                                    closest_enemy = ent
                                    
                        base_angle = 0.0
                        if closest_enemy:
                            base_angle = math.atan2(closest_enemy["position_y"] - py, closest_enemy["position_x"] - px)
                            
                        num = extra if extra > 0 else 1
                        for i in range(num):
                            angle = base_angle + (i - (num-1)/2.0) * 0.2
                            vx = math.cos(angle) * 600
                            vy = math.sin(angle) * 600
                            self.spawn_projectile(ENTITY_PROJECTILE, PROJECTILE_CRYSTAL, px, py, vx, vy, player_id, damage, radius)
                            
                    elif weaponType == WEAPON_DEATH_AURA:
                        player["weapons_mask"] |= (1 << (weaponType - 1))
                        
                    elif weaponType == WEAPON_BOMB_SHOES:
                        self.spawn_projectile(ENTITY_PROJECTILE, PROJECTILE_BOMB, px, py, 0, 0, player_id, damage, radius)
                        
                    elif weaponType == WEAPON_NATURE_SPIKES:
                        num = extra if extra > 0 else 3
                        for _ in range(num):
                            rx = px + random.uniform(-200, 200)
                            ry = py + random.uniform(-200, 200)
                            self.spawn_projectile(ENTITY_PROJECTILE, PROJECTILE_SPIKE, rx, ry, 0, 0, player_id, damage, radius)

        elif packetType == PACKET_ENTITY_DAMAGE:
            if address in self.players:
                if len(data) >= struct.calcsize(ENTITY_DAMAGE_FORMAT):
                    _, _, _, eIndex, damage = struct.unpack(ENTITY_DAMAGE_FORMAT, data[:struct.calcsize(ENTITY_DAMAGE_FORMAT)])
                    
                    # 1. Local Player taking damage (Validation request)
                    if eIndex == playerIdentification:
                        player = self.players[address]
                        current_time = time.time()
                        if current_time >= player.get("iframe_until", 0.0):
                            # Verify if any active enemy is close to the player to validate collision
                            px, py = player["position_x"], player["position_y"]
                            enemy_found = False
                            for ent in self.entities.values():
                                if ent["type"] == ENTITY_CHARACTER and ent["charType"] == CHARACTER_ENEMY:
                                    dx = px - ent["position_x"]
                                    dy = py - ent["position_y"]
                                    dist = math.sqrt(dx*dx + dy*dy)
                                    if dist <= 80.0: # generous threshold to account for network latency/interpolation
                                        enemy_found = True
                                        break
                            
                            if enemy_found:
                                player["health"] -= damage
                                player["iframe_until"] = current_time + 0.5
                                if player["health"] <= 0.0:
                                    self.broadcast_notification(f"Player {playerIdentification} Died", 255, 0, 0, 5.0, 1.0, False)
                                    max_health = player.get("attributes", [100.0])[0] if player.get("attributes") else 100.0
                                    player["health"] = max_health
                                    player["position_x"] = 0.0
                                    player["position_y"] = 0.0
                                    player["velocity_x"] = 0.0
                                    player["velocity_y"] = 0.0
                                    player["iframe_until"] = current_time + 2.0
                                    print(f"[DEATH] Authoritative server respawned Player {playerIdentification} instantly at (0, 0)")
                                else:
                                    # Broadcast to other players so they display the damage visual effect on this player!
                                    self.broadcast_damage(playerIdentification, damage, 0)
                    
                    # 2. Enemy taking damage
                    elif eIndex in self.entities:
                        entity = self.entities[eIndex]
                        if entity["type"] == ENTITY_CHARACTER and entity["charType"] == CHARACTER_ENEMY:
                            entity["health"] -= damage
                            if entity["health"] <= 0:
                                xp_val = entity.get("xp_value", 20.0)
                                ex, ey = entity["position_x"], entity["position_y"]
                                self.log(f"Enemy {eIndex} killed by player {playerIdentification}")
                                del self.entities[eIndex]
                                self.broadcast_despawn(eIndex)
                                self.spawn_xp_crystal(ex, ey, xp_val)
                            else:
                                self.broadcast_damage(eIndex, damage, playerIdentification)

        elif packetType == PACKET_PROJECTILE_EXPLODE:
            if address in self.players:
                if len(data) >= struct.calcsize(PACKET_PROJECTILE_EXPLODE_FORMAT):
                    _, _, _, projectileIndex = struct.unpack(PACKET_PROJECTILE_EXPLODE_FORMAT, data[:struct.calcsize(PACKET_PROJECTILE_EXPLODE_FORMAT)])
                    if projectileIndex in self.entities:
                        proj = self.entities[projectileIndex]
                        if proj["type"] == ENTITY_PROJECTILE:
                            self.spawn_explosion(proj["position_x"], proj["position_y"], proj["max_health"], proj.get("ownerID", 0))
                        del self.entities[projectileIndex]
                        self.broadcast_despawn(projectileIndex)

        elif packetType == PACKET_ATTRIBUTE_UPDATE:
            if address in self.players:
                if len(data) >= struct.calcsize(ATTRIBUTE_UPDATE_FORMAT):
                    unpacked = struct.unpack(ATTRIBUTE_UPDATE_FORMAT, data[:struct.calcsize(ATTRIBUTE_UPDATE_FORMAT)])
                    attributes = unpacked[3:] # fffffff
                    
                    # Proportional health adjustment
                    old_max = self.players[address]["attributes"][0]
                    new_max = attributes[0]
                    diff = new_max - old_max
                    if diff != 0:
                        self.players[address]["health"] += diff
                        if self.players[address]["health"] > new_max:
                            self.players[address]["health"] = new_max
                    
                    self.players[address]["attributes"] = attributes
                    print(f"Player {playerIdentification} updated attributes: {attributes}")
                    # Broadcast to others
                    self.broadcast_attribute_update(playerIdentification, attributes)

        elif packetType == PACKET_XP_COLLECT:
            if address in self.players:
                if len(data) >= struct.calcsize(XP_COLLECT_FORMAT):
                    _, _, _, crystalIndex = struct.unpack(XP_COLLECT_FORMAT, data[:struct.calcsize(XP_COLLECT_FORMAT)])
                    if crystalIndex in self.entities:
                        if self.entities[crystalIndex]["type"] == ENTITY_XP_CRYSTAL:
                            self.log(f"XP Crystal {crystalIndex} collected by player {playerIdentification}")
                            del self.entities[crystalIndex]
                            self.broadcast_despawn(crystalIndex)


    def spawn_projectile(self, entType, projType, x, y, vx, vy, ownerID, damage=0, radius=0, extra=0):
        eIndex = self.nextEntityIndex
        self.nextEntityIndex += 1
        if self.nextEntityIndex >= MAX_ENEMIES + MAX_PLAYERS:
            self.nextEntityIndex = MAX_PLAYERS
            
        self.entities[eIndex] = {
            "type": entType,
            "charType": projType,
            "position_x": x,
            "position_y": y,
            "velocity_x": vx,
            "velocity_y": vy,
            "spawnTime": time.time(),
            "ownerID": ownerID,
            "health": damage,
            "max_health": radius,
            "extraParam": extra
        }
        self.broadcast_spawn(eIndex)

    def spawn_explosion(self, x, y, radius, owner_id):
        eIndex = self.nextEntityIndex
        self.nextEntityIndex += 1
        if self.nextEntityIndex >= MAX_ENEMIES + MAX_PLAYERS:
            self.nextEntityIndex = MAX_PLAYERS
        
        self.entities[eIndex] = {
            "type": ENTITY_PROJECTILE,
            "charType": PROJECTILE_EXPLOSION,
            "position_x": x,
            "position_y": y,
            "velocity_x": 0,
            "velocity_y": 0,
            "spawnTime": time.time(),
            "health": 0.5, # Use health field as lifetime/scale for explosions
            "max_health": radius,
            "ownerID": owner_id
        }
        self.broadcast_spawn(eIndex)

    def broadcast_notification(self, message, r, g, b, duration, flash_duration, ignore_queue):
        # Pack header: packetType, playerIdentification, timestamp
        packet = struct.pack(
            NOTIFICATION_FORMAT,
            PACKET_NOTIFICATION,
            0,
            time.time(),
            message.encode('utf-8')[:63],
            r, g, b,
            duration,
            flash_duration,
            1 if ignore_queue else 0
        )
        for address in self.players:
            self.serverSocket.sendto(packet, address)

    def update_events(self, current_time):
        if not self.players or self.start_time is None:
            return
        
        elapsed = current_time - self.start_time
        
        # 1. Swarm Event (Every 120s)
        swarm_cycle_idx = int(elapsed // 120.0)
        swarm_time_in_cycle = elapsed % 120.0
        
        if swarm_time_in_cycle >= 115.0 and self.last_swarm_warning_cycle < swarm_cycle_idx:
            self.last_swarm_warning_cycle = swarm_cycle_idx
            self.broadcast_notification("SWARM INCOMING", 255, 255, 0, 5.0, 1.0, True)
            self.log("Event: Broadcasted Swarm Incoming warning")
            
        if swarm_time_in_cycle < 20.0 and swarm_cycle_idx > 0:
            self.is_swarm_active = True
        else:
            self.is_swarm_active = False
            
        # 2. Boss Event (Every 210s)
        boss_cycle_idx = int(elapsed // 210.0)
        boss_time_in_cycle = elapsed % 210.0
        
        if boss_time_in_cycle >= 205.0 and self.last_boss_warning_cycle < boss_cycle_idx:
            self.last_boss_warning_cycle = boss_cycle_idx
            self.broadcast_notification("BOSS INCOMING", 255, 0, 0, 5.0, 1.0, True)
            self.log("Event: Broadcasted Boss Incoming warning")
            
        if boss_time_in_cycle < 1.0 and boss_cycle_idx > 0 and self.last_boss_spawn_cycle < boss_cycle_idx:
            self.last_boss_spawn_cycle = boss_cycle_idx
            self.spawn_boss()

    def spawn_boss(self):
        if not self.players:
            return
        
        # Pick a random player to target/spawn around
        target_player = random.choice(list(self.players.values()))
        px, py = target_player["position_x"], target_player["position_y"]
        rx, ry = self.get_random_spawn_position(px, py, 500.0)
        
        # Apply progressive difficulty scaling
        stat_mult = 1.0 + (self.difficulty / 20.0) * 1.25
        xp_mult = 1.0 + (self.difficulty / 30.0) * 1.25
        speed_mult = 1.0 + (self.difficulty / 20.0) * 1.05
        
        hp = 3000.0 * stat_mult
        speed = 150.0 * speed_mult
        xp_value = 1000.0 * xp_mult
        
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
            "targetPlayerID": target_player["identification"],
            "health": hp,
            "max_health": hp,
            "enemyClass": ENEMY_CLASS_BOSS,
            "speed": speed,
            "xp_value": xp_value
        }
        self.broadcast_spawn(eIndex)
        self.log(f"Spawned BOSS at ({rx:.1f}, {ry:.1f}) targeting player {target_player['identification']} (HP: {hp:.1f}, Speed: {speed:.1f})")

    def broadcast_damage(self, entityIndex, damage, ownerID):
        packet = struct.pack(ENTITY_DAMAGE_FORMAT, PACKET_ENTITY_DAMAGE, ownerID, time.time(), entityIndex, damage)
        for address in self.players:
            self.serverSocket.sendto(packet, address)

    def get_spawn_packet(self, entityIndex):
        entity = self.entities[entityIndex]
        extra_param = entity.get("extraParam", 0)
        if entity["type"] == ENTITY_CHARACTER and entity.get("charType", 0) == CHARACTER_ENEMY:
            extra_param = entity.get("enemyClass", 0)
            
        return struct.pack(ENTITY_SPAWN_FORMAT, PACKET_ENTITY_SPAWN, 0, time.time(), 
                            entityIndex, entity["type"], entity.get("charType", 0), 
                            entity["position_x"], entity["position_y"],
                            entity.get("targetPlayerID", 0) if entity["type"] == ENTITY_CHARACTER else entity.get("ownerID", 0),
                            entity.get("velocity_x", 0), entity.get("velocity_y", 0),
                            entity.get("health", 0.0), entity.get("max_health", 0.0),
                            extra_param)

    def broadcast_spawn(self, entityIndex):
        packet = self.get_spawn_packet(entityIndex)
        for address in self.players:
            self.serverSocket.sendto(packet, address)

    def broadcast_despawn(self, entityIndex):
        packet = struct.pack(ENTITY_DESPAWN_FORMAT, PACKET_ENTITY_DESPAWN, 0, time.time(), entityIndex)
        for address in self.players:
            self.serverSocket.sendto(packet, address)

    def broadcast_attribute_update(self, playerIdentification, attributes):
        packet = struct.pack(ATTRIBUTE_UPDATE_FORMAT, PACKET_ATTRIBUTE_UPDATE, playerIdentification, time.time(), *attributes)
        for address in self.players:
            self.serverSocket.sendto(packet, address)

    def spawn_xp_crystal(self, x, y, value=20.0):
        # 1. Try to find a free slot in the XP range
        free_index = -1
        nearest_index = -1
        min_dist = 999999
        
        for i in range(XP_START_INDEX, XP_END_INDEX):
            if i not in self.entities:
                if free_index == -1: free_index = i
            else:
                # Calculate distance for potential merge (if pool full or very close)
                dist_sq = (self.entities[i]["position_x"] - x)**2 + (self.entities[i]["position_y"] - y)**2
                if dist_sq < min_dist:
                    min_dist = dist_sq
                    nearest_index = i
        
        # 2. Merging logic: if pool full or extremely close (40 units)
        if (free_index == -1 and nearest_index != -1) or (nearest_index != -1 and min_dist < 40**2):
            self.entities[nearest_index]["health"] += value
            # Broadcast a spawn update to let clients know the value changed (health field)
            self.broadcast_spawn(nearest_index)
            return

        # 3. Spawn new one if space
        if free_index != -1:
            self.entities[free_index] = {
                "type": ENTITY_XP_CRYSTAL,
                "charType": 0,
                "position_x": x,
                "position_y": y,
                "health": value, # Using health field for XP amount
                "spawnTime": time.time()
            }
            self.broadcast_spawn(free_index)

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
                                          player["velocity_x"], player["velocity_y"],
                                          player.get("weapons_mask", 0),
                                          player.get("health", 100.0))

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
