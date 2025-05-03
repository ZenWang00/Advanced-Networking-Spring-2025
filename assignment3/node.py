import socket
import threading
import time
from template_pb2 import Message
from sys import argv
from snowflake import folded_hash, derive_id

CONNECTED_PEERS = {}

def send_message(conn, message):
    serialized = message.SerializeToString()
    conn.sendall(len(serialized).to_bytes(4, byteorder="big"))
    conn.sendall(serialized)

def receive_message(conn, message_type):
    msg = message_type()
    size = int.from_bytes(conn.recv(4), byteorder="big")
    data = conn.recv(size)
    msg.ParseFromString(data)
    return msg

def handle_receive(peer_id):
    global CONNECTED_PEERS
    while True:
        for conn in CONNECTED_PEERS.values():
            try:
                msg = receive_message(conn, Message)
                print(f"Received message from {msg.fr} to {msg.to}: {msg.msg}")
                if msg.to == peer_id:
                    print(f"Message for this node: {msg.msg}")
                else:
                    print(f"Forwarding message to {msg.to}")
                    forward_message(msg)
            except:
                continue

def forward_message(message):
    global CONNECTED_PEERS
    if message.to in CONNECTED_PEERS:
        send_message(CONNECTED_PEERS[message.to], message)
    else:
        print(f"Peer {message.to} is not directly connected, message dropped.")

def broadcast_message(peer_id):
    while True:
        target_id = int(input("Enter target peer ID: "))
        content = input("Enter message content: ")
        message = Message(fr=peer_id, to=target_id, msg=content)
        if target_id in CONNECTED_PEERS:
            send_message(CONNECTED_PEERS[target_id], message)
        else:
            print(f"Target peer {target_id} is not connected.")

def start_node(peer_id, port, peers):
    global CONNECTED_PEERS
    server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_sock.bind(("0.0.0.0", port))
    server_sock.listen()
    print(f"Node {peer_id} listening on port {port}")

    for peer_addr, peer_port in peers:
        try:
            conn = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            conn.connect((peer_addr, peer_port))
            CONNECTED_PEERS[derive_id(peer_port)] = conn
            print(f"Connected to peer at {peer_addr}:{peer_port}")
        except:
            print(f"Could not connect to peer at {peer_addr}:{peer_port}")

    receive_thread = threading.Thread(target=handle_receive, args=(peer_id,))
    receive_thread.start()
    broadcast_thread = threading.Thread(target=broadcast_message, args=(peer_id,))
    broadcast_thread.start()

if __name__ == "__main__":
    if len(argv) < 3:
        print("Usage: python3 template-peer.py [port] [peer_ip:peer_port] ...")
        exit(1)

    port = int(argv[1])
    assigner_id = port
    peer_id = derive_id(assigner_id)
    print(f"Generated unique peer ID: {peer_id}")

    peers = [tuple(arg.split(":")) for arg in argv[2:]]
    peers = [(peer[0], int(peer[1])) for peer in peers]

    start_node(peer_id, port, peers)
