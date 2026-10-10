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
print(send(sock,"reset halt"))
time.sleep(1)
print("g_osc_err@0x20000030:", send(sock,"mdw 0x20000030 1"))
print("g_clk_err@0x2000002c:", send(sock,"mdw 0x2000002c 1"))
print("=== RCC CR (HSE/PLL) ===")
print(send(sock,"mdw 0x40023800 1"))
print("=== RCC CFGR ===")
print(send(sock,"mdw 0x40023808 1"))
# let it run a bit (it will hit the deadloop again) then re-read
print(send(sock,"resume"))
time.sleep(3)
print(send(sock,"halt"))
print("g_osc_err:", send(sock,"mdw 0x20000030 1"))
sock.close()
