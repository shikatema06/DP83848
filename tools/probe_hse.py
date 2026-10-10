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
# immediately read HSE/PLL (fresh reset, HSE cold)
def rcr():
    o=send(sock,"mdw 0x40023800 1")
    v=o.split("0x40023800:")[1].split()[0]
    return int(v,16)
print("T=0    RCC CR =", hex(rcr()))
for i in range(10):
    time.sleep(0.5)
    v=rcr()
    hse=(v>>17)&1; pll=(v>>25)&1; hseon=(v>>16)&1; pllon=(v>>24)&1
    print("T=%.1f HSEON=%d HSERDY=%d PLLON=%d PLLRDY=%d"%(0.5*(i+1),hseon,hse,pllon,pll))
sock.close()
