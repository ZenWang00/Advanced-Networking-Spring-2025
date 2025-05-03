import socket
from sys import argv
from template_pb2 import Message, FastHandshake

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

def main():
    host = None
    port = None
    requested_id = None
    try:
        if len(argv) > 3:
            host = argv[1]
            port = int(argv[2])
            requested_id = int(argv[3])
        elif len(argv) > 2:
            port = int(argv[1])
        else:
            raise ValueError
    except:
        host = host or "127.0.0.1"
        port = 8080

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((host, port))
        print("Connected to the server")

        handshake = FastHandshake(id=requested_id, error=False)
        send_message(s, handshake)
        response = receive_message(s, FastHandshake)
        if response.error:
            print(f"Requested ID #{requested_id} is taken. Using assigned ID #{response.id}")
        else:
            print(f"Successfully connected with ID #{response.id}")

        id = response.id

        while True:
            try:
                data = input("Enter a message (format '[to_id] [message]'): ")
                if data == "end":
                    break
                to_id, message = data.split(" ", 1)
                to_id = int(to_id)
            except ValueError:
                print("Invalid format. Use '[to_id] [message]'")
                continue

            msg = Message(fr=id, to=to_id, msg=message)
            send_message(s, msg)
            if message == "end":
                break
            msg = receive_message(s, Message)
            print(f"Received: {msg.msg} from {msg.fr} to {msg.to}")
            if msg.msg == "end":
                break
        print("Closing connection")


if __name__ == "__main__":
    main()
