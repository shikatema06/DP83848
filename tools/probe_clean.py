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

print("=== reset halt ===")
print(send(sock,"reset halt"))
print(send(sock,"reg pc"))
print(send(sock,"mdw 0xE000ED08 1"))  # VTOR
print("=== resume 7s ===")
print(send(sock,"resume"))
time.sleep(7)
print("=== halt ===")
print(send(sock,"halt"))
print(send(sock,"reg pc"))
print(send(sock,"reg sp"))
print(send(sock,"mdw 0xE000ED08 1"))  # VTOR
print(send(sock,"mdw 0x40023808 1"))  # CFGR SW/SWS
print(send(sock,"mdw 0x40023800 1"))  # CR HSE/PLL
print(send(sock,"mdw 0xE000E010 1"))  # SysTick CTRL
sock.close()
