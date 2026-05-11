import socket
import struct
import time
import random
import math

# Constants
IP = "0.0.0.0"
PORT = 12345
HEARTBEAT_TIMEOUT = 30.0

# Packet Types
PACKET_ID_REQUEST = 0
PACKET_ID_RESPONSE = 1
PACKET_HEARTBEAT = 2
PACKET_HEARTBEAT_ACK = 3
PACKET_VELOCITY_UPDATE = 4
PACKET_WORLD_STATE = 5

# Struct Formats (Little Endian, Packed)
# Header: type (B), player_id (I), timestamp (d)
HEADER_FMT = "<BId"
# ID Response: Header only
ID_RES_FMT = HEADER_FMT
# Velocity Update: Header + vx (f), vy (f)
VEL_UPD_FMT = HEADER_FMT + "ff"
# World State: Header + count (I)
WORLD_STATE_HEADER_FMT = HEADER_FMT + "I"
# Player State in World State: id (I), vx (f), vy (f)
PLAYER_STATE_FMT = "Iff"

class Server:
    def __init__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind((IP, PORT))
        self.sock.setblocking(False)
        self.players = {} # (addr): {id, x, y, vx, vy, last_heartbeat}
        self.next_player_id = 1
        self.last_broadcast = 0
        self.broadcast_interval = 0.05 # 20Hz
        print(f"Server started on {IP}:{PORT}")

    def get_random_spawn(self):
        angle = random.uniform(0, 2 * math.pi)
        radius = random.uniform(0, 500)
        x = radius * math.cos(angle)
        y = radius * math.sin(angle)
        return x, y

    def run(self):
        while True:
            try:
                data, addr = self.sock.recvfrom(2048)
                self.handle_packet(data, addr)
            except BlockingIOError:
                pass
            
            self.cleanup_players()
            self.broadcast_world_state()
            time.sleep(0.01)

    def handle_packet(self, data, addr):
        if len(data) < struct.calcsize(HEADER_FMT):
            return

        header = struct.unpack(HEADER_FMT, data[:struct.calcsize(HEADER_FMT)])
        p_type, p_id, p_timestamp = header

        if p_type == PACKET_ID_REQUEST:
            if addr not in self.players:
                new_id = self.next_player_id
                self.next_player_id += 1
                spawn_x, spawn_y = self.get_random_spawn()
                self.players[addr] = {
                    "id": new_id,
                    "x": spawn_x,
                    "y": spawn_y,
                    "vx": 0.0,
                    "vy": 0.0,
                    "last_heartbeat": time.time()
                }
                print(f"New player {new_id} connected from {addr}")
            
            p = self.players[addr]
            res = struct.pack(ID_RES_FMT, PACKET_ID_RESPONSE, p["id"], p_timestamp)
            self.sock.sendto(res, addr)

        elif p_type == PACKET_HEARTBEAT:
            if addr in self.players:
                self.players[addr]["last_heartbeat"] = time.time()
                ack = struct.pack(HEADER_FMT, PACKET_HEARTBEAT_ACK, self.players[addr]["id"], p_timestamp)
                self.sock.sendto(ack, addr)

        elif p_type == PACKET_VELOCITY_UPDATE:
            if addr in self.players:
                fmt = VEL_UPD_FMT
                if len(data) >= struct.calcsize(fmt):
                    _, _, _, vx, vy = struct.unpack(fmt, data[:struct.calcsize(fmt)])
                    self.players[addr]["vx"] = vx
                    self.players[addr]["vy"] = vy
                    self.players[addr]["last_heartbeat"] = time.time()

    def broadcast_world_state(self):
        now = time.time()
        if now - self.last_broadcast < self.broadcast_interval:
            return
        self.last_broadcast = now

        if not self.players:
            return

        # Prepare packet
        player_list = list(self.players.values())
        count = len(player_list)
        
        # Header: type, id (0 for server), timestamp, count
        data = struct.pack(WORLD_STATE_HEADER_FMT, PACKET_WORLD_STATE, 0, now, count)
        
        for p in player_list:
            data += struct.pack(PLAYER_STATE_FMT, p["id"], p["vx"], p["vy"])

        # Broadcast to all
        for addr in self.players:
            self.sock.sendto(data, addr)

    def cleanup_players(self):
        now = time.time()
        to_remove = []
        for addr, p in self.players.items():
            if now - p["last_heartbeat"] > HEARTBEAT_TIMEOUT:
                print(f"Player {p['id']} timed out")
                to_remove.append(addr)
        
        for addr in to_remove:
            del self.players[addr]

if __name__ == "__main__":
    server = Server()
    server.run()
