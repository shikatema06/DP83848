import socket, time, sys, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
HOST="127.0.0.1"; PORT=4444
def send(sock,cmd):
    sock.sendall((cmd+"\n").encode()); time.sleep(0.3)
    data=b""; sock.settimeout(2.0)
    try:
        while True:
            c=sock.recv(4096)
            if not c: break
            data+=c
            if data.rstrip().endswith(b">"): break
    except socket.timeout: pass
    return data.decode(errors="replace")
sock=socket.create_connection((HOST,PORT),timeout=5); time.sleep(0.5); sock.settimeout(2.0)
try: sock.recv(4096)
except socket.timeout: pass
# App vector table at 0x08010000: [0]=MSP, [1]=ResetHandler
print("=== App vector @0x08010000 ===")
print(send(sock,"mdw 0x08010000 2"))
print("=== Active addr check: read boot meta at 0x0800FFD0 ===")
print(send(sock,"mdw 0x0800FFD0 16"))
# slot A start addr from linker: SLOT_A = 0x08010000 typically
sock.close()
