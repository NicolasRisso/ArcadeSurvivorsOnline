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
PACKET_POSITION_UPDATE = 4

# Struct Formats (Little Endian, Packed)
# Header: type (B), player_id (I), timestamp (d)
HEADER_FMT = "<BId"
# ID Response: Header + x (f), y (f)
ID_RES_FMT = HEADER_FMT + "ff"
# Position Update: Header + x (f), y (f)
POS_UPD_FMT = HEADER_FMT + "ff"

class Server:
    def __init__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind((IP, PORT))
        self.sock.setblocking(False)
        self.players = {} # (addr): {id, x, y, last_heartbeat}
        self.next_player_id = 1
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
                data, addr = self.sock.recvfrom(1024)
                self.handle_packet(data, addr)
            except BlockingIOError:
                pass
            
            self.cleanup_players()
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
                    "last_heartbeat": time.time()
                }
                print(f"New player {new_id} connected from {addr}")
            
            p = self.players[addr]
            res = struct.pack(ID_RES_FMT, PACKET_ID_RESPONSE, p["id"], p_timestamp, p["x"], p["y"])
            self.sock.sendto(res, addr)

        elif p_type == PACKET_HEARTBEAT:
            if addr in self.players:
                self.players[addr]["last_heartbeat"] = time.time()
                ack = struct.pack(HEADER_FMT, PACKET_HEARTBEAT_ACK, self.players[addr]["id"], p_timestamp)
                self.sock.sendto(ack, addr)

        elif p_type == PACKET_POSITION_UPDATE:
            if addr in self.players:
                # Update sender's position
                fmt = POS_UPD_FMT
                if len(data) >= struct.calcsize(fmt):
                    _, _, _, x, y = struct.unpack(fmt, data[:struct.calcsize(fmt)])
                    self.players[addr]["x"] = x
                    self.players[addr]["y"] = y
                    self.players[addr]["last_heartbeat"] = time.time()
                    
                    # Broadcast this movement to all OTHER players
                    for other_addr, other_p in self.players.items():
                        if other_addr != addr:
                            self.sock.sendto(data, other_addr)

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
