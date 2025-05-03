import socket
from sys import argv
from threading import Thread
from template_pb2 import Message, FastHandshake

CLIENTS = {}
MESSAGE_BUFFER = {}  # 缓存离线消息
LAST_ID = 0

def send_message(conn, m):
    serialized = m.SerializeToString()
    conn.sendall(len(serialized).to_bytes(4, byteorder="big"))
    conn.sendall(serialized)

def receive_message(conn, m):
    msg = m()
    size = int.from_bytes(conn.recv(4), byteorder="big")
    data = conn.recv(size)
    msg.ParseFromString(data)
    return msg

def handle_client(conn: socket.socket, addr, requested_id=None):
    global LAST_ID

    try:
        if requested_id is not None and requested_id not in CLIENTS:
            id = requested_id
        else:
            id = LAST_ID
            LAST_ID += 1

        CLIENTS[id] = (conn, addr)
        with conn:
            handshake = FastHandshake(id=id, error=(id != requested_id))
            print(f"Debug: Preparing to send handshake to client #{id} at {addr}")
            send_message(conn, handshake)
            print(f"Debug: Handshake sent to client #{id}")
            print(f"Connected by #{id} {addr}")

            if id in MESSAGE_BUFFER:
                for buffered_msg in MESSAGE_BUFFER[id]:
                    send_message(conn, buffered_msg)
                del MESSAGE_BUFFER[id]

            while True:
                msg = receive_message(conn, Message)
                print(f"Received: {msg.msg} from {msg.fr} to {msg.to}")
                if msg.msg == "end":
                    break
                if msg.to in CLIENTS:
                    target_conn, _ = CLIENTS[msg.to]
                    send_message(target_conn, msg)
                else:
                    MESSAGE_BUFFER.setdefault(msg.to, []).append(msg)
                    print(f"Client #{msg.to} is offline. Message buffered.")
            print(f"Closing connection to #{id} {addr}")
            CLIENTS.pop(id)
    except Exception as e:
        print(f"Error handling client #{id}: {e}")
        CLIENTS.pop(id, None)


def loop_main(port):
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.bind(("0.0.0.0", port))
            print(f"Server started on port {port}")
            print("Waiting for a client...")
            s.listen()
            while True:
                try:
                    conn, addr = s.accept()
                    requested_id = int(input("Enter requested ID (or leave blank for auto-assignment): ").strip()) if argv else None
                    Thread(target=handle_client, args=(conn, addr, requested_id)).start()
                except KeyboardInterrupt:
                    break
    except Exception as e:
        print(f"Server error: {e}")

def main():
    global CLIENTS

    try:
        port = int(argv[1])
    except:
        port = 8080

    loop = Thread(target=loop_main, args=(port,))
    loop.daemon = True
    loop.start()

    while True:
        try:
            command = input("op> ").strip().lower()
        except:
            break

        if command == "num_users":
            print(f"Number of users: {len(CLIENTS)}")
        else:
            print("Invalid command")
            print("Available commands:")
            print("- num_users: Get the number of connected users")

if __name__ == "__main__":
    main()
