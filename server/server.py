import socket
import struct
import time
import random
import math

# ==============================================================================
# Global Game Constants
# ==============================================================================
SERVER_IP_ADDRESS = "0.0.0.0"
SERVER_PORT = 12345
HEARTBEAT_TIMEOUT = 30.0
MAXIMUM_ENEMIES = 3000
MAXIMUM_XP_CRYSTALS = 2000
MAXIMUM_PLAYERS = 4
SNAPSHOT_CYCLE_TIME = 2.5
SNAPSHOT_TICKS = 50  # 2.5 seconds / 0.05 seconds
BATCH_SIZE = MAXIMUM_ENEMIES // SNAPSHOT_TICKS
XP_START_INDEX = MAXIMUM_PLAYERS + MAXIMUM_ENEMIES
XP_END_INDEX = XP_START_INDEX + MAXIMUM_XP_CRYSTALS

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
PACKET_UPGRADE_UPDATE = 17
PACKET_NAME_UPDATE = 18
PACKET_START_GAME = 19

# Projectile Types
PROJECTILE_UNDEFINED = 0
PROJECTILE_FIREBALL = 1
PROJECTILE_CRYSTAL = 2
PROJECTILE_BOMB = 3
PROJECTILE_SPIKE = 4
PROJECTILE_EXPLOSION = 5

# Projectile Configuration
PROJECTILE_SPEED = 500.0
PROJECTILE_LIFETIME = 3.0
CRYSTAL_SPEED = 800.0
CRYSTAL_LIFETIME = 5.0
BOMB_DELAY = 2.0
BOMB_LIFETIME = 2.5
SPIKE_LIFETIME = 3.0

# Damage values
DAMAGE_FIREBALL = 50.0
DAMAGE_CRYSTAL = 100.0
DAMAGE_AURA = 15.0  # per tick
DAMAGE_BOMB = 500.0
DAMAGE_SPIKE = 20.0  # per tick
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
WORLD_STATE_HEADER_FORMAT = HEADER_FORMAT + "ffiI"
PLAYER_STATE_FORMAT = "<IffffBf"
ENTITY_SPAWN_FORMAT = HEADER_FORMAT + "IBBffIffffi"
ENTITY_SNAPSHOT_HEADER_FORMAT = HEADER_FORMAT + "I"
SINGLE_SNAPSHOT_FORMAT = "<Hff"
ENEMY_DEATH_REPORT_HEADER_FORMAT = HEADER_FORMAT + "I"
ENTITY_DESPAWN_FORMAT = HEADER_FORMAT + "I"
WEAPON_FIRE_FORMAT = HEADER_FORMAT + "Bffi"
ENTITY_DAMAGE_FORMAT = HEADER_FORMAT + "If"
PACKET_PROJECTILE_EXPLODE_FORMAT = HEADER_FORMAT + "I"
DAMAGE_BATCH_HEADER_FORMAT = HEADER_FORMAT + "I"
DAMAGE_ENTRY_FORMAT = "IfB"
XP_COLLECT_FORMAT = HEADER_FORMAT + "I"
ATTRIBUTE_UPDATE_FORMAT = HEADER_FORMAT + "fffffff"
NOTIFICATION_FORMAT = HEADER_FORMAT + "64sBBBffB"
UPGRADE_UPDATE_FORMAT = HEADER_FORMAT + "BBB"
PACKET_NAME_UPDATE_FORMAT = HEADER_FORMAT + "I32s"
PACKET_START_GAME_FORMAT = HEADER_FORMAT

# ==============================================================================
# Helper Modules (Internal Classes)
# ==============================================================================

class SpatialHashGrid:
    """A highly scalable 2D Spatial Hash Grid to reduce collision queries from O(N^2) to O(N)."""
    def __init__(self, cell_size=100.0):
        self.cell_size = cell_size
        self.grid = {}  # Maps (cell_x, cell_y) -> set of entity_indexes

    def clear(self):
        self.grid.clear()

    def insert(self, entity_index, position_x, position_y):
        cell_x = int(position_x // self.cell_size)
        cell_y = int(position_y // self.cell_size)
        cell_key = (cell_x, cell_y)
        if cell_key not in self.grid:
            self.grid[cell_key] = set()
        self.grid[cell_key].add(entity_index)

    def query_neighbors(self, position_x, position_y, radius):
        minimum_cell_x = int((position_x - radius) // self.cell_size)
        maximum_cell_x = int((position_x + radius) // self.cell_size)
        minimum_cell_y = int((position_y - radius) // self.cell_size)
        maximum_cell_y = int((position_y + radius) // self.cell_size)

        neighbors = []
        for cell_x in range(minimum_cell_x, maximum_cell_x + 1):
            for cell_y in range(minimum_cell_y, maximum_cell_y + 1):
                cell_entities = self.grid.get((cell_x, cell_y))
                if cell_entities:
                    neighbors.extend(cell_entities)
        return neighbors


class NetworkManager:
    """Encapsulates raw UDP socket transmissions, serialization, and packet polling."""
    def __init__(self):
        self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.server_socket.bind((SERVER_IP_ADDRESS, SERVER_PORT))
        self.server_socket.setblocking(False)
        print(f"Server socket active on {SERVER_IP_ADDRESS}:{SERVER_PORT}")

    def poll_packets(self):
        packets = []
        while True:
            try:
                data, address = self.server_socket.recvfrom(4096)
                packets.append((data, address))
            except BlockingIOError:
                break
            except Exception as exception:
                print(f"[NETWORK ERROR] Error receiving packet: {exception}")
                break
        return packets

    def send_to(self, data, address):
        try:
            self.server_socket.sendto(data, address)
        except Exception as exception:
            print(f"[NETWORK ERROR] Error sending to {address}: {exception}")

    def broadcast(self, data, player_addresses):
        for address in player_addresses:
            self.send_to(data, address)


class EntityManager:
    """Manages active characters, projectiles, and experience crystal entities."""
    def __init__(self):
        self.players = {}  # Maps address -> player_dictionary
        self.entities = {}  # Maps entity_index -> entity_dictionary
        self.next_player_identification = 1
        self.next_entity_index = MAXIMUM_PLAYERS

    def register_player(self, address):
        if address not in self.players:
            player_identification = self.next_player_identification
            self.next_player_identification += 1
            self.players[address] = {
                "identification": player_identification,
                "position_x": 0.0,
                "position_y": 0.0,
                "velocity_x": 0.0,
                "velocity_y": 0.0,
                "last_heartbeat_received": time.time(),
                "weapons_mask": 0,
                "weapon_levels": [0, 0, 0, 0, 0],
                "relic_levels": [0, 0, 0, 0, 0, 0, 0],
                "attributes": (100.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.0),
                "health": 100.0,
                "invulnerability_frame_until": 0.0,
                "name": f"Player {player_identification}"
            }
            return self.players[address], True
        return self.players[address], False

    def remove_player(self, address):
        if address in self.players:
            player = self.players[address]
            print(f"Removing player {player['identification']} from {address}")
            del self.players[address]

    def get_next_entity_index(self):
        index = self.next_entity_index
        self.next_entity_index += 1
        if self.next_entity_index >= MAXIMUM_ENEMIES + MAXIMUM_PLAYERS:
            self.next_entity_index = MAXIMUM_PLAYERS
        return index

    def spawn_entity(self, entity_index, entity_type, character_type, position_x, position_y, **kwargs):
        entity = {
            "type": entity_type,
            "character_type": character_type,
            "position_x": position_x,
            "position_y": position_y,
            "spawn_time": time.time(),
        }
        entity.update(kwargs)
        self.entities[entity_index] = entity
        return entity

    def despawn_entity(self, entity_index):
        if entity_index in self.entities:
            del self.entities[entity_index]

    def cleanup_inactive_players(self, timeout_duration):
        current_time = time.time()
        expired_addresses = []
        for address, player in self.players.items():
            if current_time - player["last_heartbeat_received"] > timeout_duration:
                print(f"Player {player['identification']} timed out.")
                expired_addresses.append(address)
        
        for address in expired_addresses:
            self.remove_player(address)
        return len(expired_addresses) > 0


class SpawnerSystem:
    """Manages progression schedules, swarms, boss spawns, and XP crystal pooling."""
    def __init__(self):
        self.start_time = None
        self.difficulty = 0.0
        self.last_spawner_time = 0.0
        self.last_swarm_warning_cycle = -1
        self.last_boss_warning_cycle = -1
        self.last_boss_spawn_cycle = -1
        self.is_swarm_active = False

    def reset(self):
        self.start_time = None
        self.difficulty = 0.0
        self.last_spawner_time = 0.0
        self.last_swarm_warning_cycle = -1
        self.last_boss_warning_cycle = -1
        self.last_boss_spawn_cycle = -1
        self.is_swarm_active = False

    def get_random_spawn_position(self, center_x=0.0, center_y=0.0, distance=1000.0):
        angle = random.uniform(0.0, 2.0 * math.pi)
        position_x = center_x + distance * math.cos(angle)
        position_y = center_y + distance * math.sin(angle)
        return position_x, position_y

    def update(self, current_time, game_started, entity_manager, network_manager, broadcast_spawn_function):
        if not entity_manager.players or not game_started:
            self.reset()
            return

        if self.start_time is None:
            self.start_time = current_time

        self.difficulty = (current_time - self.start_time) / 6.0

        # Calculate spawn intervals based on scaling difficulty
        difficulty_multiplier = 1.0 + (self.difficulty / 20.0) * 1.25
        swarm_multiplier = 2.0 if self.is_swarm_active else 1.0
        actual_interval = SPAWN_INTERVAL / (difficulty_multiplier * swarm_multiplier)

        if current_time - self.last_spawner_time >= actual_interval:
            self.last_spawner_time = current_time
            
            if len(entity_manager.entities) < MAXIMUM_ENEMIES:
                # Find available spawn formations based on difficulty
                unlocked_shapes = []
                for shape, configuration in SPAWN_CONFIG.items():
                    unlock_difficulty = configuration[6]
                    if self.difficulty >= unlock_difficulty:
                        unlocked_shapes.append(shape)
                if not unlocked_shapes:
                    unlocked_shapes = ["SINGLE"]

                available_shapes = list(unlocked_shapes)
                for player in entity_manager.players.values():
                    if not available_shapes:
                        available_shapes = list(unlocked_shapes)
                    
                    # Weighted selection
                    weights = [SPAWN_CONFIG[shape][0] for shape in available_shapes]
                    shape = random.choices(available_shapes, weights=weights, k=1)[0]
                    available_shapes.remove(shape)
                    
                    self.spawn_formation_group(player, shape, entity_manager, broadcast_spawn_function)

        # Trigger event timers
        self.update_events(current_time, entity_manager, network_manager, broadcast_spawn_function)

    def spawn_formation_group(self, target_player, shape, entity_manager, broadcast_spawn_function):
        player_x = target_player["position_x"]
        player_y = target_player["position_y"]

        _, minimum_count, maximum_count, minimum_distance, maximum_distance, enemy_class, _ = SPAWN_CONFIG[shape]
        count = random.randint(minimum_count, maximum_count)
        distance = random.uniform(minimum_distance, maximum_distance)

        spawn_positions = []
        base_shape = "SINGLE"
        if "CIRCLE" in shape:
            base_shape = "CIRCLE"
        elif "WALL" in shape:
            base_shape = "WALL"
        elif "CLUSTER" in shape:
            base_shape = "CLUSTER"

        if base_shape == "SINGLE":
            spawn_positions.append(self.get_random_spawn_position(player_x, player_y, distance))
        elif base_shape == "CIRCLE":
            start_angle = random.uniform(0.0, 2.0 * math.pi)
            for index in range(count):
                angle = start_angle + (2.0 * math.pi * index / count)
                spawn_positions.append((player_x + distance * math.cos(angle), player_y + distance * math.sin(angle)))
        elif base_shape == "WALL":
            angle = random.uniform(0.0, 2.0 * math.pi)
            center_x = player_x + distance * math.cos(angle)
            center_y = player_y + distance * math.sin(angle)
            
            # Tangent direction vector
            tangent_x = -math.sin(angle)
            tangent_y = math.cos(angle)
            
            spacing = 40.0
            for index in range(count):
                offset = (index - count / 2.0) * spacing
                spawn_positions.append((center_x + tangent_x * offset, center_y + tangent_y * offset))
        elif base_shape == "CLUSTER":
            cluster_center_x, cluster_center_y = self.get_random_spawn_position(player_x, player_y, distance)
            for _ in range(count):
                offset_x = random.uniform(-50.0, 50.0)
                offset_y = random.uniform(-50.0, 50.0)
                spawn_positions.append((cluster_center_x + offset_x, cluster_center_y + offset_y))

        # Enemy base scaling stats
        health = ENEMY_HEALTH
        speed = 150.0
        experience_value = 20.0

        if enemy_class == ENEMY_CLASS_FAST:
            health = ENEMY_HEALTH * 0.6
            speed = 225.0
            experience_value = 40.0
        elif enemy_class == ENEMY_CLASS_TANK:
            health = ENEMY_HEALTH * 3.0
            speed = 45.0
            experience_value = 100.0

        # Apply progressive difficulty multipliers
        stat_multiplier = 1.0 + (self.difficulty / 20.0) * 1.25
        experience_multiplier = 1.0 + (self.difficulty / 30.0) * 1.25
        speed_multiplier = 1.0 + (self.difficulty / 20.0) * 1.05

        health *= stat_multiplier
        speed *= speed_multiplier
        experience_value *= experience_multiplier

        for spawn_x, spawn_y in spawn_positions:
            entity_index = entity_manager.get_next_entity_index()
            entity_manager.spawn_entity(
                entity_index,
                ENTITY_CHARACTER,
                CHARACTER_ENEMY,
                spawn_x,
                spawn_y,
                target_player_identification=target_player["identification"],
                health=health,
                maximum_health=health,
                enemy_class=enemy_class,
                speed=speed,
                experience_value=experience_value
            )
            broadcast_spawn_function(entity_index)

    def spawn_boss(self, entity_manager, broadcast_spawn_function):
        if not entity_manager.players:
            return
        
        target_player = random.choice(list(entity_manager.players.values()))
        player_x = target_player["position_x"]
        player_y = target_player["position_y"]
        spawn_x, spawn_y = self.get_random_spawn_position(player_x, player_y, 500.0)

        stat_multiplier = 1.0 + (self.difficulty / 20.0) * 1.25
        experience_multiplier = 1.0 + (self.difficulty / 30.0) * 1.25
        speed_multiplier = 1.0 + (self.difficulty / 20.0) * 1.05

        health = 3000.0 * stat_multiplier
        speed = 150.0 * speed_multiplier
        experience_value = 1000.0 * experience_multiplier

        entity_index = entity_manager.get_next_entity_index()
        entity_manager.spawn_entity(
            entity_index,
            ENTITY_CHARACTER,
            CHARACTER_ENEMY,
            spawn_x,
            spawn_y,
            target_player_identification=target_player["identification"],
            health=health,
            maximum_health=health,
            enemy_class=ENEMY_CLASS_BOSS,
            speed=speed,
            experience_value=experience_value
        )
        broadcast_spawn_function(entity_index)
        print(f"[BOSS] Boss spawned at ({spawn_x:.1f}, {spawn_y:.1f}) targeting player {target_player['identification']}!")

    def spawn_xp_crystal(self, position_x, position_y, value, entity_manager, broadcast_spawn_function):
        free_index = -1
        nearest_index = -1
        minimum_distance_squared = 999999.0
        
        # Look for existing crystals to merge or find free index
        for index in range(XP_START_INDEX, XP_END_INDEX):
            if index not in entity_manager.entities:
                if free_index == -1:
                    free_index = index
            else:
                crystal = entity_manager.entities[index]
                distance_squared = (crystal["position_x"] - position_x)**2 + (crystal["position_y"] - position_y)**2
                if distance_squared < minimum_distance_squared:
                    minimum_distance_squared = distance_squared
                    nearest_index = index

        # Merge crystals if nearby (40 units squared)
        if (free_index == -1 and nearest_index != -1) or (nearest_index != -1 and minimum_distance_squared < 40.0**2):
            entity_manager.entities[nearest_index]["health"] += value
            broadcast_spawn_function(nearest_index)
            return

        if free_index != -1:
            entity_manager.spawn_entity(
                free_index,
                ENTITY_XP_CRYSTAL,
                0,
                position_x,
                position_y,
                health=value  # Use health field for XP amount representation
            )
            broadcast_spawn_function(free_index)

    def update_events(self, current_time, entity_manager, network_manager, broadcast_spawn_function):
        if self.start_time is None:
            return
        
        elapsed_time = current_time - self.start_time

        # 1. Swarm Event (Every 120 seconds)
        swarm_cycle_index = int(elapsed_time // 120.0)
        swarm_time_in_cycle = elapsed_time % 120.0

        if swarm_time_in_cycle >= 115.0 and self.last_swarm_warning_cycle < swarm_cycle_index:
            self.last_swarm_warning_cycle = swarm_cycle_index
            self.broadcast_notification("SWARM INCOMING", 255, 255, 0, 5.0, 1.0, True, entity_manager, network_manager)

        if swarm_time_in_cycle < 20.0 and swarm_cycle_index > 0:
            self.is_swarm_active = True
        else:
            self.is_swarm_active = False

        # 2. Boss Event (Every 210 seconds)
        boss_cycle_index = int(elapsed_time // 210.0)
        boss_time_in_cycle = elapsed_time % 210.0

        if boss_time_in_cycle >= 205.0 and self.last_boss_warning_cycle < boss_cycle_index:
            self.last_boss_warning_cycle = boss_cycle_index
            self.broadcast_notification("BOSS INCOMING", 255, 0, 0, 5.0, 1.0, True, entity_manager, network_manager)

        if boss_time_in_cycle < 1.0 and boss_cycle_index > 0 and self.last_boss_spawn_cycle < boss_cycle_index:
            self.last_boss_spawn_cycle = boss_cycle_index
            self.spawn_boss(entity_manager, broadcast_spawn_function)

    def broadcast_notification(self, message, red, green, blue, duration, flash_duration, ignore_queue, entity_manager, network_manager):
        notification_packet = struct.pack(
            NOTIFICATION_FORMAT,
            PACKET_NOTIFICATION,
            0,
            time.time(),
            message.encode('utf-8')[:63].ljust(64, b'\0'),
            red,
            green,
            blue,
            duration,
            flash_duration,
            1 if ignore_queue else 0
        )
        network_manager.broadcast(notification_packet, entity_manager.players.keys())


class SimulationSystem:
    """Orchestrates physics, pursuits, projectiles, and O(N) spatial partitioning avoidance queries."""
    def __init__(self):
        self.spatial_grid = SpatialHashGrid(cell_size=100.0)

    def update(self, delta_time, entity_manager, team_lives, handle_player_death_function, broadcast_despawn_function, spawn_explosion_function):
        current_time = time.time()

        # Update players
        for player in entity_manager.players.values():
            if player.get("health", 100.0) <= 0.0:
                if team_lives > 0:
                    team_lives = handle_player_death_function(player)
                else:
                    player["health"] = 0.0
                    player["velocity_x"] = 0.0
                    player["velocity_y"] = 0.0
            else:
                player["position_x"] += player["velocity_x"] * delta_time
                player["position_y"] += player["velocity_y"] * delta_time
                
                # Cap positions to map boundaries
                map_limit = 5000.0 - 20.0
                player["position_x"] = max(-map_limit, min(map_limit, player["position_x"]))
                player["position_y"] = max(-map_limit, min(map_limit, player["position_y"]))

        # Re-populate spatial partitioning hash grid for enemies
        self.spatial_grid.clear()
        for entity_index, entity in entity_manager.entities.items():
            if entity["type"] == ENTITY_CHARACTER and entity["character_type"] == CHARACTER_ENEMY:
                self.spatial_grid.insert(entity_index, entity["position_x"], entity["position_y"])

        # Update enemies
        for entity_index, entity in list(entity_manager.entities.items()):
            if entity["type"] == ENTITY_CHARACTER and entity["character_type"] == CHARACTER_ENEMY:
                
                # O(N) Spatial Hashing Avoidance Query
                avoidance_x = 0.0
                avoidance_y = 0.0
                neighbors = self.spatial_grid.query_neighbors(entity["position_x"], entity["position_y"], ENEMY_AVOIDANCE_RADIUS)
                for neighbor_index in neighbors:
                    if entity_index == neighbor_index:
                        continue
                    
                    neighbor = entity_manager.entities.get(neighbor_index)
                    if neighbor and neighbor["type"] == ENTITY_CHARACTER and neighbor["character_type"] == CHARACTER_ENEMY:
                        difference_x = entity["position_x"] - neighbor["position_x"]
                        difference_y = entity["position_y"] - neighbor["position_y"]
                        distance = math.sqrt(difference_x * difference_x + difference_y * difference_y)
                        
                        if 0.0 < distance < ENEMY_AVOIDANCE_RADIUS:
                            force_magnitude = (1.0 - (distance / ENEMY_AVOIDANCE_RADIUS)) * ENEMY_AVOIDANCE_FORCE
                            avoidance_x += (difference_x / distance) * force_magnitude
                            avoidance_y += (difference_y / distance) * force_magnitude

                # Re-evaluate targets deterministically
                target_player_identification = entity.get("target_player_identification", 0)
                if target_player_identification == 0:
                    if entity_manager.players:
                        random_player = random.choice(list(entity_manager.players.values()))
                        entity["target_player_identification"] = random_player["identification"]
                        target_player_identification = random_player["identification"]
                
                if target_player_identification != 0:
                    target_player = None
                    for player in entity_manager.players.values():
                        if player["identification"] == target_player_identification:
                            target_player = player
                            break

                    # Re-target if current player is dead
                    if target_player and target_player.get("health", 100.0) <= 0.0:
                        alive_players = [player["identification"] for player in entity_manager.players.values() if player.get("health", 100.0) > 0.0]
                        if alive_players:
                            new_target_identification = alive_players[entity_index % len(alive_players)]
                            entity["target_player_identification"] = new_target_identification
                            for player in entity_manager.players.values():
                                if player["identification"] == new_target_identification:
                                    target_player = player
                                    break
                    
                    if not target_player:
                        entity["target_player_identification"] = 0
                        continue

                    target_x = target_player["position_x"]
                    target_y = target_player["position_y"]
                    difference_x = target_x - entity["position_x"]
                    difference_y = target_y - entity["position_y"]
                    distance = math.sqrt(difference_x * difference_x + difference_y * difference_y)
                    
                    steer_x = 0.0
                    steer_y = 0.0
                    if distance > 1.0:
                        steer_x = difference_x / distance
                        steer_y = difference_y / distance
                    
                    final_x = steer_x + avoidance_x
                    final_y = steer_y + avoidance_y
                    
                    final_magnitude = math.sqrt(final_x * final_x + final_y * final_y)
                    if final_magnitude > 0.001:
                        final_x /= final_magnitude
                        final_y /= final_magnitude
                    else:
                        final_x = 0.0
                        final_y = 0.0

                    speed = entity.get("speed", 150.0)
                    entity["position_x"] += final_x * speed * delta_time
                    entity["position_y"] += final_y * speed * delta_time

        # Update projectiles
        for entity_index, entity in list(entity_manager.entities.items()):
            if entity["type"] == ENTITY_PROJECTILE:
                entity["position_x"] += entity["velocity_x"] * delta_time
                entity["position_y"] += entity["velocity_y"] * delta_time
                
                spawn_duration = current_time - entity["spawn_time"]
                projectile_type = entity["character_type"]
                
                maximum_lifetime = PROJECTILE_LIFETIME
                if projectile_type == PROJECTILE_CRYSTAL:
                    maximum_lifetime = CRYSTAL_LIFETIME
                elif projectile_type == PROJECTILE_BOMB:
                    maximum_lifetime = BOMB_DELAY
                elif projectile_type == PROJECTILE_SPIKE:
                    maximum_lifetime = SPIKE_LIFETIME
                elif projectile_type == PROJECTILE_EXPLOSION:
                    maximum_lifetime = 0.5
                
                if spawn_duration > maximum_lifetime:
                    if projectile_type == PROJECTILE_BOMB:
                        spawn_explosion_function(entity["position_x"], entity["position_y"], entity["maximum_health"], entity.get("owner_identification", 0))
                    
                    entity_manager.despawn_entity(entity_index)
                    broadcast_despawn_function(entity_index)
        
        return team_lives


# ==============================================================================
# Central Server Coordinator
# ==============================================================================

class Server:
    """Central server orchestrator that coordinates networking, simulation, and updates."""
    def __init__(self):
        self.network_manager = NetworkManager()
        self.entity_manager = EntityManager()
        self.spawner_system = SpawnerSystem()
        self.simulation_system = SimulationSystem()
        
        self.last_broadcast_time = 0.0
        self.snapshot_enemy_index = 0
        self.broadcast_interval = 0.05
        self.team_lives = 3
        self.game_started = False
        
        self.log_buffer = []
        self.last_log_flush_time = time.time()
        print("Arcade Survivors Authoritative Multiplayer Server Initialized.")

    def log(self, message):
        self.log_buffer.append(f"[{time.strftime('%H:%M:%S')}] {message}")
        if len(self.log_buffer) > 500:
            self.flush_logs()

    def flush_logs(self):
        if self.log_buffer:
            print("\n".join(self.log_buffer))
            self.log_buffer.clear()
        self.last_log_flush_time = time.time()

    def handle_player_death(self, player):
        current_time = time.time()
        player_identification = player["identification"]
        
        if self.team_lives > 0:
            self.team_lives -= 1
            self.spawner_system.broadcast_notification(
                f"Player {player_identification} Died! {self.team_lives} Lives Left",
                255, 0, 0, 5.0, 1.0, False, self.entity_manager, self.network_manager
            )
            player_maximum_health = player.get("attributes", [100.0])[0] if player.get("attributes") else 100.0
            player["health"] = player_maximum_health
            player["position_x"] = 0.0
            player["position_y"] = 0.0
            player["velocity_x"] = 0.0
            player["velocity_y"] = 0.0
            player["invulnerability_frame_until"] = current_time + 2.0
            print(f"[DEATH] Player {player_identification} respawned at (0,0). Lives remaining: {self.team_lives}")
        else:
            player["health"] = 0.0
            player["velocity_x"] = 0.0
            player["velocity_y"] = 0.0
            self.spawner_system.broadcast_notification(
                f"Player {player_identification} Died! Game Over",
                255, 0, 0, 5.0, 1.0, False, self.entity_manager, self.network_manager
            )
            print(f"[DEATH] Player {player_identification} is dead. 0 lives left.")
        return self.team_lives

    def get_spawn_packet(self, entity_index):
        entity = self.entity_manager.entities[entity_index]
        extra_parameter = entity.get("extra_parameter", 0)
        
        if entity["type"] == ENTITY_CHARACTER and entity.get("character_type", 0) == CHARACTER_ENEMY:
            extra_parameter = entity.get("enemy_class", 0)
            
        target_or_owner_identification = entity.get("target_player_identification", 0)
        if entity["type"] != ENTITY_CHARACTER:
            target_or_owner_identification = entity.get("owner_identification", 0)

        return struct.pack(
            ENTITY_SPAWN_FORMAT,
            PACKET_ENTITY_SPAWN,
            0,
            time.time(), 
            entity_index,
            entity["type"],
            entity.get("character_type", 0), 
            entity["position_x"],
            entity["position_y"],
            target_or_owner_identification,
            entity.get("velocity_x", 0.0),
            entity.get("velocity_y", 0.0),
            entity.get("health", 0.0),
            entity.get("maximum_health", 0.0),
            extra_parameter
        )

    def broadcast_spawn(self, entity_index):
        packet = self.get_spawn_packet(entity_index)
        self.network_manager.broadcast(packet, self.entity_manager.players.keys())

    def broadcast_despawn(self, entity_index):
        packet = struct.pack(ENTITY_DESPAWN_FORMAT, PACKET_ENTITY_DESPAWN, 0, time.time(), entity_index)
        self.network_manager.broadcast(packet, self.entity_manager.players.keys())

    def broadcast_damage(self, entity_index, damage, owner_identification):
        packet = struct.pack(ENTITY_DAMAGE_FORMAT, PACKET_ENTITY_DAMAGE, owner_identification, time.time(), entity_index, damage)
        self.network_manager.broadcast(packet, self.entity_manager.players.keys())

    def broadcast_attribute_update(self, player_identification, attributes):
        packet = struct.pack(ATTRIBUTE_UPDATE_FORMAT, PACKET_ATTRIBUTE_UPDATE, player_identification, time.time(), *attributes)
        self.network_manager.broadcast(packet, self.entity_manager.players.keys())

    def broadcast_upgrade_update(self, player_identification, is_relic, upgrade_type, level):
        packet = struct.pack(UPGRADE_UPDATE_FORMAT, PACKET_UPGRADE_UPDATE, player_identification, time.time(), is_relic, upgrade_type, level)
        self.network_manager.broadcast(packet, self.entity_manager.players.keys())

    def send_upgrade_correction(self, address, player_identification, is_relic, upgrade_type, level):
        packet = struct.pack(UPGRADE_UPDATE_FORMAT, PACKET_UPGRADE_UPDATE, player_identification, time.time(), is_relic, upgrade_type, level)
        self.network_manager.send_to(packet, address)

    def spawn_projectile(self, entity_type, projectile_type, position_x, position_y, velocity_x, velocity_y, owner_identification, damage=0.0, radius=0.0, extra_parameter=0):
        entity_index = self.entity_manager.get_next_entity_index()
        self.entity_manager.spawn_entity(
            entity_index,
            entity_type,
            projectile_type,
            position_x,
            position_y,
            velocity_x=velocity_x,
            velocity_y=velocity_y,
            owner_identification=owner_identification,
            health=damage,
            maximum_health=radius,
            extra_parameter=extra_parameter
        )
        self.broadcast_spawn(entity_index)

    def spawn_explosion(self, position_x, position_y, radius, owner_identification):
        entity_index = self.entity_manager.get_next_entity_index()
        self.entity_manager.spawn_entity(
            entity_index,
            ENTITY_PROJECTILE,
            PROJECTILE_EXPLOSION,
            position_x,
            position_y,
            velocity_x=0.0,
            velocity_y=0.0,
            health=0.5,  # Using health field for explosion scale/duration representation
            maximum_health=radius,
            owner_identification=owner_identification
        )
        self.broadcast_spawn(entity_index)

    def recalculate_player_attributes(self, player):
        maximum_health = 100.0
        damage = 1.0
        attack_speed = 1.0
        movement_speed = 1.0
        size = 1.0
        xp_gained = 1.0
        lifesteal = 0.0

        relic_levels = player.get("relic_levels", [0, 0, 0, 0, 0, 0, 0])

        # Apply Relic updates authoritatively
        maximum_health += 100.0 * 0.12 * relic_levels[0]
        damage += 0.08 * relic_levels[1]
        attack_speed += 0.06 * relic_levels[2]
        size += 0.15 * relic_levels[3]
        movement_speed += 0.09 * relic_levels[4]
        xp_gained += 0.08 * relic_levels[5]
        lifesteal += 0.01 * relic_levels[6]

        old_attributes = player.get("attributes", (100.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.0))
        new_attributes = (maximum_health, damage, attack_speed, movement_speed, size, xp_gained, lifesteal)
        player["attributes"] = new_attributes

        # Apply difference scaling to health
        old_maximum = old_attributes[0]
        new_maximum = new_attributes[0]
        health_difference = new_maximum - old_maximum
        
        if health_difference != 0:
            player["health"] += health_difference
            player["health"] = max(0.0, min(new_maximum, player["health"]))

        self.broadcast_attribute_update(player["identification"], new_attributes)

    def process_packet(self, data, address):
        if len(data) < struct.calcsize(HEADER_FORMAT):
            return

        packet_header = struct.unpack(HEADER_FORMAT, data[:struct.calcsize(HEADER_FORMAT)])
        packet_type, player_identification, packet_timestamp = packet_header

        if packet_type == PACKET_ID_REQUEST:
            player, was_registered = self.entity_manager.register_player(address)
            if was_registered:
                print(f"[JOIN] Player {player['identification']} joined from {address}")
            
            identification_response = struct.pack(IDENTIFICATION_RESPONSE_FORMAT, PACKET_ID_RESPONSE, player["identification"], packet_timestamp)
            self.network_manager.send_to(identification_response, address)

            # Send names of other connected players
            for other_address, other_player in self.entity_manager.players.items():
                name_bytes = other_player["name"].encode('utf-8')[:31].ljust(32, b'\0')
                name_packet = struct.pack(PACKET_NAME_UPDATE_FORMAT, PACKET_NAME_UPDATE, other_player["identification"], time.time(), other_player["identification"], name_bytes)
                self.network_manager.send_to(name_packet, address)

            # Broadcast new player name
            name_bytes = player["name"].encode('utf-8')[:31].ljust(32, b'\0')
            name_packet = struct.pack(PACKET_NAME_UPDATE_FORMAT, PACKET_NAME_UPDATE, player["identification"], time.time(), player["identification"], name_bytes)
            self.network_manager.broadcast(name_packet, self.entity_manager.players.keys())

            # Spawn current world entities onto new client
            for index in self.entity_manager.entities:
                spawn_packet = self.get_spawn_packet(index)
                self.network_manager.send_to(spawn_packet, address)

        elif packet_type == PACKET_HEARTBEAT:
            if address in self.entity_manager.players:
                self.entity_manager.players[address]["last_heartbeat_received"] = time.time()
                heartbeat_ack = struct.pack(HEADER_FORMAT, PACKET_HEARTBEAT_ACK, self.entity_manager.players[address]["identification"], packet_timestamp)
                self.network_manager.send_to(heartbeat_ack, address)

        elif packet_type == PACKET_VELOCITY_UPDATE:
            if address in self.entity_manager.players:
                if len(data) >= struct.calcsize(VELOCITY_UPDATE_FORMAT):
                    _, _, _, position_x, position_y, velocity_x, velocity_y = struct.unpack(VELOCITY_UPDATE_FORMAT, data[:struct.calcsize(VELOCITY_UPDATE_FORMAT)])
                    player = self.entity_manager.players[address]
                    
                    if player.get("health", 100.0) <= 0.0:
                        player["velocity_x"] = 0.0
                        player["velocity_y"] = 0.0
                        return

                    map_limit = 5000.0 - 20.0
                    position_x = max(-map_limit, min(map_limit, position_x))
                    position_y = max(-map_limit, min(map_limit, position_y))

                    # Safety check: validate speed jumps
                    difference_x = position_x - player["position_x"]
                    difference_y = position_y - player["position_y"]
                    distance = math.sqrt(difference_x * difference_x + difference_y * difference_y)

                    if distance <= 100.0 or (player["position_x"] == 0.0 and player["position_y"] == 0.0):
                        player["position_x"] = position_x
                        player["position_y"] = position_y
                    
                    player["velocity_x"] = velocity_x
                    player["velocity_y"] = velocity_y
                    player["last_heartbeat_received"] = time.time()

        elif packet_type == PACKET_ENEMY_DEATH_REPORT:
            if address in self.entity_manager.players:
                player = self.entity_manager.players[address]
                if player.get("health", 100.0) <= 0.0:
                    return

                if len(data) >= struct.calcsize(ENEMY_DEATH_REPORT_HEADER_FORMAT):
                    _, _, _, count = struct.unpack(ENEMY_DEATH_REPORT_HEADER_FORMAT, data[:struct.calcsize(ENEMY_DEATH_REPORT_HEADER_FORMAT)])
                    identifications_format = "I" * count
                    entity_identifications = struct.unpack("<" + identifications_format, data[struct.calcsize(ENEMY_DEATH_REPORT_HEADER_FORMAT):struct.calcsize(ENEMY_DEATH_REPORT_HEADER_FORMAT) + (count * 4)])

                    for entity_index in entity_identifications:
                        if entity_index in self.entity_manager.entities:
                            entity = self.entity_manager.entities[entity_index]
                            if entity["type"] == ENTITY_CHARACTER and entity.get("character_type") == CHARACTER_ENEMY:
                                experience_value = entity.get("experience_value", 20.0)
                                spawn_x = entity["position_x"]
                                spawn_y = entity["position_y"]
                                self.entity_manager.despawn_entity(entity_index)
                                self.broadcast_despawn(entity_index)
                                self.spawner_system.spawn_xp_crystal(spawn_x, spawn_y, experience_value, self.entity_manager, self.broadcast_spawn)

        elif packet_type == PACKET_DAMAGE_BATCH:
            if address in self.entity_manager.players:
                player = self.entity_manager.players[address]
                if player.get("health", 100.0) <= 0.0:
                    return

                header_size = struct.calcsize(DAMAGE_BATCH_HEADER_FORMAT)
                if len(data) >= header_size:
                    _, _, _, count = struct.unpack(DAMAGE_BATCH_HEADER_FORMAT, data[:header_size])
                    entry_size = struct.calcsize(DAMAGE_ENTRY_FORMAT)
                    
                    for index in range(count):
                        start_offset = header_size + (index * entry_size)
                        if start_offset + entry_size > len(data):
                            break
                        
                        entity_index, damage, weapon_type = struct.unpack("<" + DAMAGE_ENTRY_FORMAT, data[start_offset:start_offset + entry_size])
                        
                        # Check if player took damage
                        target_player = None
                        for active_player in self.entity_manager.players.values():
                            if active_player["identification"] == entity_index:
                                target_player = active_player
                                break
                        
                        if target_player:
                            current_time = time.time()
                            if current_time >= target_player.get("invulnerability_frame_until", 0.0):
                                player_x = target_player["position_x"]
                                player_y = target_player["position_y"]
                                enemy_found = False
                                base_damage = 10.0
                                
                                for entity in self.entity_manager.entities.values():
                                    if entity["type"] == ENTITY_CHARACTER and entity["character_type"] == CHARACTER_ENEMY:
                                        difference_x = player_x - entity["position_x"]
                                        difference_y = player_y - entity["position_y"]
                                        distance = math.sqrt(difference_x * difference_x + difference_y * difference_y)
                                        
                                        if distance <= 120.0:
                                            enemy_found = True
                                            if entity.get("enemy_class") == ENEMY_CLASS_BOSS:
                                                base_damage = 40.0
                                            break
                                
                                if enemy_found:
                                    difficulty_multiplier = 1.0 + (self.spawner_system.difficulty / 20.0) * 1.25
                                    expected_damage = base_damage * difficulty_multiplier
                                    
                                    target_player["health"] -= expected_damage
                                    target_player["invulnerability_frame_until"] = current_time + 0.5
                                    
                                    if target_player["health"] <= 0.0:
                                        self.team_lives = self.handle_player_death(target_player)
                                    else:
                                        self.broadcast_damage(entity_index, expected_damage, 0)
                                        
                        elif entity_index in self.entity_manager.entities:
                            entity = self.entity_manager.entities[entity_index]
                            if entity["type"] == ENTITY_CHARACTER and entity["character_type"] == CHARACTER_ENEMY:
                                actual_damage = damage
                                enemy_health_before = entity["health"]
                                if actual_damage > enemy_health_before:
                                    actual_damage = enemy_health_before
                                
                                entity["health"] -= damage
                                
                                # Authoritative Lifesteal Calculation
                                player_lifesteal = player.get("attributes", (100.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.0))[6]
                                if player_lifesteal > 0.0 and actual_damage > 0.0:
                                    if 1 <= weapon_type <= 5:
                                        has_weapon = (player.get("weapons_mask", 0) & (1 << (weapon_type - 1))) != 0
                                        if player.get("weapon_levels", [0,0,0,0,0])[weapon_type - 1] > 0 or has_weapon:
                                            multipliers = {
                                                1: 1.0 * 0.40,  # Fireball Ring
                                                2: 1.0 * 1.00,  # Crystal Staff
                                                3: 0.5 * 0.40,  # Death Aura
                                                4: 0.5 * 0.40,  # Bomb Shoes
                                                5: 1.0 * 0.40   # Nature Spikes
                                            }
                                            healing = actual_damage * player_lifesteal * multipliers.get(weapon_type, 0.0)
                                            player_maximum_health = player.get("attributes", (100.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.0))[0]
                                            player["health"] = min(player_maximum_health, player["health"] + healing)
                                
                                if entity["health"] <= 0:
                                    experience_value = entity.get("experience_value", 20.0)
                                    spawn_x = entity["position_x"]
                                    spawn_y = entity["position_y"]
                                    self.entity_manager.despawn_entity(entity_index)
                                    self.broadcast_despawn(entity_index)
                                    self.spawner_system.spawn_xp_crystal(spawn_x, spawn_y, experience_value, self.entity_manager, self.broadcast_spawn)
                                else:
                                    self.broadcast_damage(entity_index, damage, player_identification)

        elif packet_type == PACKET_WEAPON_FIRE:
            if address in self.entity_manager.players:
                player = self.entity_manager.players[address]
                if player.get("health", 100.0) <= 0.0:
                    return

                if len(data) >= struct.calcsize(WEAPON_FIRE_FORMAT):
                    payload = data[struct.calcsize(HEADER_FORMAT):]
                    weapon_type, damage, radius, extra_parameter = struct.unpack("<Bffi", payload)
                    player_x = player["position_x"]
                    player_y = player["position_y"]
                    player_identification = player["identification"]

                    if weapon_type == WEAPON_FIREBALL_RING:
                        number_of_fireballs = 8
                        for index in range(number_of_fireballs):
                            angle = (index / number_of_fireballs) * 2.0 * math.pi
                            velocity_x = math.cos(angle) * 500.0
                            velocity_y = math.sin(angle) * 500.0
                            self.spawn_projectile(ENTITY_PROJECTILE, PROJECTILE_FIREBALL, player_x, player_y, velocity_x, velocity_y, player_identification, damage, radius)
                            
                    elif weapon_type == WEAPON_CRYSTAL_STAFF:
                        # Target closest enemy
                        closest_enemy = None
                        minimum_distance = float("inf")
                        for entity in self.entity_manager.entities.values():
                            if entity["type"] == ENTITY_CHARACTER and entity["character_type"] == CHARACTER_ENEMY and entity.get("health", 0.0) > 0.0:
                                difference_x = entity["position_x"] - player_x
                                difference_y = entity["position_y"] - player_y
                                distance = math.sqrt(difference_x * difference_x + difference_y * difference_y)
                                if distance < minimum_distance:
                                    minimum_distance = distance
                                    closest_enemy = entity
                                    
                        base_angle = 0.0
                        if closest_enemy:
                            base_angle = math.atan2(closest_enemy["position_y"] - player_y, closest_enemy["position_x"] - player_x)
                            
                        spawn_number = extra_parameter if extra_parameter > 0 else 1
                        for index in range(spawn_number):
                            angle = base_angle + (index - (spawn_number - 1) / 2.0) * 0.2
                            velocity_x = math.cos(angle) * 600.0
                            velocity_y = math.sin(angle) * 600.0
                            self.spawn_projectile(ENTITY_PROJECTILE, PROJECTILE_CRYSTAL, player_x, player_y, velocity_x, velocity_y, player_identification, damage, radius)
                            
                    elif weapon_type == WEAPON_DEATH_AURA:
                        player["weapons_mask"] |= (1 << (weapon_type - 1))
                        
                    elif weapon_type == WEAPON_BOMB_SHOES:
                        self.spawn_projectile(ENTITY_PROJECTILE, PROJECTILE_BOMB, player_x, player_y, 0.0, 0.0, player_identification, damage, radius)
                        
                    elif weapon_type == WEAPON_NATURE_SPIKES:
                        spawn_number = extra_parameter if extra_parameter > 0 else 3
                        for _ in range(spawn_number):
                            spawn_x = player_x + random.uniform(-200.0, 200.0)
                            spawn_y = player_y + random.uniform(-200.0, 200.0)
                            self.spawn_projectile(ENTITY_PROJECTILE, PROJECTILE_SPIKE, spawn_x, spawn_y, 0.0, 0.0, player_identification, damage, radius)

        elif packet_type == PACKET_PROJECTILE_EXPLODE:
            if address in self.entity_manager.players:
                if len(data) >= struct.calcsize(PACKET_PROJECTILE_EXPLODE_FORMAT):
                    _, _, _, projectile_index = struct.unpack(PACKET_PROJECTILE_EXPLODE_FORMAT, data[:struct.calcsize(PACKET_PROJECTILE_EXPLODE_FORMAT)])
                    if projectile_index in self.entity_manager.entities:
                        projectile = self.entity_manager.entities[projectile_index]
                        if projectile["type"] == ENTITY_PROJECTILE:
                            self.spawn_explosion(projectile["position_x"], projectile["position_y"], projectile["maximum_health"], projectile.get("owner_identification", 0))
                        self.entity_manager.despawn_entity(projectile_index)
                        self.broadcast_despawn(projectile_index)

        elif packet_type == PACKET_ATTRIBUTE_UPDATE:
            if address in self.entity_manager.players:
                if len(data) >= struct.calcsize(ATTRIBUTE_UPDATE_FORMAT):
                    unpacked_payload = struct.unpack(ATTRIBUTE_UPDATE_FORMAT, data[:struct.calcsize(ATTRIBUTE_UPDATE_FORMAT)])
                    attributes = unpacked_payload[3:]
                    
                    old_maximum_health = self.entity_manager.players[address]["attributes"][0]
                    new_maximum_health = attributes[0]
                    health_difference = new_maximum_health - old_maximum_health
                    
                    if health_difference != 0:
                        self.entity_manager.players[address]["health"] += health_difference
                        self.entity_manager.players[address]["health"] = min(new_maximum_health, self.entity_manager.players[address]["health"])
                    
                    self.entity_manager.players[address]["attributes"] = attributes
                    self.broadcast_attribute_update(player_identification, attributes)

        elif packet_type == PACKET_UPGRADE_UPDATE:
            if address in self.entity_manager.players:
                if len(data) >= struct.calcsize(UPGRADE_UPDATE_FORMAT):
                    _, _, _, is_relic, upgrade_type, level = struct.unpack(UPGRADE_UPDATE_FORMAT, data[:struct.calcsize(UPGRADE_UPDATE_FORMAT)])
                    player = self.entity_manager.players[address]
                    
                    is_valid = True
                    if is_relic:
                        if upgrade_type < 1 or upgrade_type > 7 or level < 0 or level > 15:
                            is_valid = False
                    else:
                        if upgrade_type < 1 or upgrade_type > 5 or level < 0 or level > 15:
                            is_valid = False
                            
                    current_level = 0
                    if is_valid:
                        current_level = player["relic_levels"][upgrade_type - 1] if is_relic else player["weapon_levels"][upgrade_type - 1]
                        if level != current_level + 1:
                            if not (current_level == 0 and level == 1):
                                is_valid = False
                                
                    if is_valid:
                        if is_relic:
                            player["relic_levels"][upgrade_type - 1] = level
                            self.recalculate_player_attributes(player)
                        else:
                            player["weapon_levels"][upgrade_type - 1] = level
                            
                        # Recalculate weapons mask
                        weapons_mask = 0
                        for index, weapon_level in enumerate(player["weapon_levels"]):
                            if weapon_level > 0:
                                weapons_mask |= (1 << index)
                        player["weapons_mask"] = weapons_mask
                        
                        self.broadcast_upgrade_update(player_identification, is_relic, upgrade_type, level)
                    else:
                        self.send_upgrade_correction(address, player_identification, is_relic, upgrade_type, current_level)

        elif packet_type == PACKET_XP_COLLECT:
            if address in self.entity_manager.players:
                player = self.entity_manager.players[address]
                if player.get("health", 100.0) <= 0.0:
                    return
                if len(data) >= struct.calcsize(XP_COLLECT_FORMAT):
                    _, _, _, crystal_index = struct.unpack(XP_COLLECT_FORMAT, data[:struct.calcsize(XP_COLLECT_FORMAT)])
                    if crystal_index in self.entity_manager.entities:
                        if self.entity_manager.entities[crystal_index]["type"] == ENTITY_XP_CRYSTAL:
                            self.entity_manager.despawn_entity(crystal_index)
                            self.broadcast_despawn(crystal_index)

        elif packet_type == PACKET_NAME_UPDATE:
            if address in self.entity_manager.players:
                if len(data) >= struct.calcsize(PACKET_NAME_UPDATE_FORMAT):
                    _, _, _, target_player_identification, name_bytes = struct.unpack(PACKET_NAME_UPDATE_FORMAT, data[:struct.calcsize(PACKET_NAME_UPDATE_FORMAT)])
                    name = name_bytes.decode('utf-8', errors='ignore').split('\x00')[0]
                    self.entity_manager.players[address]["name"] = name
                    print(f"[NAME] Player {player_identification} renamed to: {name}")
                    
                    name_bytes_padded = name.encode('utf-8')[:31].ljust(32, b'\0')
                    broadcast_packet = struct.pack(PACKET_NAME_UPDATE_FORMAT, PACKET_NAME_UPDATE, player_identification, time.time(), player_identification, name_bytes_padded)
                    self.network_manager.broadcast(broadcast_packet, self.entity_manager.players.keys())

        elif packet_type == PACKET_START_GAME:
            if address in self.entity_manager.players:
                player = self.entity_manager.players[address]
                if player["identification"] == 1:
                    print("[START] Game started authoritative trigger by host!")
                    self.game_started = True
                    self.spawner_system.start_time = time.time()
                    
                    broadcast_packet = struct.pack(PACKET_START_GAME_FORMAT, PACKET_START_GAME, player["identification"], time.time())
                    self.network_manager.broadcast(broadcast_packet, self.entity_manager.players.keys())

    def broadcast_world_state(self):
        current_time = time.time()
        if current_time - self.last_broadcast_time < self.broadcast_interval:
            return
        self.last_broadcast_time = current_time

        if not self.entity_manager.players:
            return

        players_list = list(self.entity_manager.players.values())
        players_count = len(players_list)
        
        elapsed_game_time = 0.0
        if self.spawner_system.start_time is not None:
            elapsed_game_time = current_time - self.spawner_system.start_time
            
        world_state_data = struct.pack(WORLD_STATE_HEADER_FORMAT, PACKET_WORLD_STATE, 0, current_time, elapsed_game_time, self.spawner_system.difficulty, self.team_lives, players_count)
        for player in players_list:
            world_state_data += struct.pack(
                PLAYER_STATE_FORMAT, 
                player["identification"], 
                player["position_x"], player["position_y"],
                player["velocity_x"], player["velocity_y"],
                player.get("weapons_mask", 0),
                player.get("health", 100.0)
            )

        self.network_manager.broadcast(world_state_data, self.entity_manager.players.keys())

    def broadcast_entity_snapshots(self):
        current_time = time.time()
        if not self.entity_manager.players:
            return

        # Get list of all active enemies
        active_enemies = [
            (index, entity) for index, entity in self.entity_manager.entities.items()
            if entity.get("type") == ENTITY_CHARACTER and entity.get("character_type") == CHARACTER_ENEMY
        ]
        
        if not active_enemies:
            return

        # Sweep round-robin through all active enemies in batches of up to 32
        batch_size = 32
        
        # Ensure our snapshot index is within bounds of current list length
        if self.snapshot_enemy_index >= len(active_enemies):
            self.snapshot_enemy_index = 0
            
        selected_slice = active_enemies[self.snapshot_enemy_index : self.snapshot_enemy_index + batch_size]
        self.snapshot_enemy_index += len(selected_slice)
        if self.snapshot_enemy_index >= len(active_enemies):
            self.snapshot_enemy_index = 0

        snapshot_data = struct.pack(ENTITY_SNAPSHOT_HEADER_FORMAT, PACKET_ENTITY_SNAPSHOT, 0, current_time, len(selected_slice))
        for index, entity in selected_slice:
            snapshot_data += struct.pack(SINGLE_SNAPSHOT_FORMAT, index, entity["position_x"], entity["position_y"])

        self.network_manager.broadcast(snapshot_data, self.entity_manager.players.keys())

    def run(self):
        last_tick_time = time.time()
        while True:
            current_time = time.time()
            delta_time = current_time - last_tick_time
            last_tick_time = current_time

            # 1. Process network packets
            incoming_packets = self.network_manager.poll_packets()
            for packet_data, client_address in incoming_packets:
                try:
                    self.process_packet(packet_data, client_address)
                except Exception as exception:
                    print(f"[PACKET ERROR] Failed processing packet from {client_address}: {exception}")

            # 2. Update Simulation and Spawner Systems
            self.team_lives = self.simulation_system.update(
                delta_time, 
                self.entity_manager, 
                self.team_lives, 
                self.handle_player_death, 
                self.broadcast_despawn, 
                self.spawn_explosion
            )
            self.spawner_system.update(
                current_time, 
                self.game_started, 
                self.entity_manager, 
                self.network_manager, 
                self.broadcast_spawn
            )

            # 3. Handle disconnected heartbeats
            self.entity_manager.cleanup_inactive_players(HEARTBEAT_TIMEOUT)

            # 4. Broadcast global sync state
            self.broadcast_world_state()
            self.broadcast_entity_snapshots()

            # 5. Flush logs periodically
            if current_time - self.last_log_flush_time > 1.0:
                self.flush_logs()

            time.sleep(0.01)


if __name__ == "__main__":
    server = Server()
    server.run()
