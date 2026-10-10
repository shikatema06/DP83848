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
time.sleep(1)
print("g_osc_err@0x20000030:", send(sock,"mdw 0x20000030 1"))
print(send(sock,"resume"))
time.sleep(7)
print("=== halt after 7s ===")
print(send(sock,"halt"))
print("PC:", send(sock,"reg pc"))
print("VTOR:", send(sock,"mdw 0xE000ED08 1"))
print("CFGR:", send(sock,"mdw 0x40023808 1"))
print("CR:", send(sock,"mdw 0x40023800 1"))
sock.close()
