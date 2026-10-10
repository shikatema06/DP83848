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

print("=== FLASH_ACR 0x40023C00 ===")
print(send(sock,"mdw 0x40023C00 1"))
print("=== PWR_CR 0x40007000 ===")
print(send(sock,"mdw 0x40007000 1"))
print("=== RCC_CFGR (0x40023808) SW bits ===")
print(send(sock,"mdw 0x40023808 1"))
print("=== check SCB_VTOR ===")
print(send(sock,"mdw 0xE000ED08 1"))
sock.close()
