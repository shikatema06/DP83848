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
print("=== reset halt, read meta ===")
print(send(sock,"reset halt"))
print("meta@0x0800FFD0:", send(sock,"mdw 0x0800FFD0 16"))
print("=== resume, wait 7s for jump ===")
print(send(sock,"resume"))
time.sleep(7)
print("=== halt: check if jumped to App ===")
print(send(sock,"halt"))
print(send(sock,"reg pc"))
print("VTOR:", send(sock,"mdw 0xE000ED08 1"))
print("CFGR:", send(sock,"mdw 0x40023808 1"))
sock.close()
