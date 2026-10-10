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

# We are halted in Error_Handler. Manually try to switch SYSCLK to PLL.
# RCC_CFGR @0x40023808, SW=bits[1:0], SWS=bits[3:2]. PLL=0b10.
print("CFGR before:", send(sock,"mdw 0x40023808 1"))
# write SW=PLL (keep other bits). current 0x0000fc00 -> set bit1
print(send(sock,"mww 0x40023808 0x0000fc02"))
time.sleep(0.5)
print("CFGR after write:", send(sock,"mdw 0x40023808 1"))
print("SWS bits(3:2):", send(sock,"mdw 0x40023808 1"))
# also read RCC CR again to confirm PLLRDY
print("RCC CR:", send(sock,"mdw 0x40023800 1"))
sock.close()
