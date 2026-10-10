import socket, time, sys, io

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
HOST="127.0.0.1"; PORT=4444
RCC_CR=0x40023800; RCC_PLLCFGR=0x40023804; RCC_CFGR=0x40023808
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

# reset and halt at Reset_Handler (RCC in reset state)
print("=== reset halt ===")
print(send(sock,"reset halt"))

def cr(): return send(sock,"mdw 0x40023800 1").split("0x40023800:")[1].split()[0] if "0x40023800" in send(sock,"mdw 0x40023800 1") else "?"

# 1) enable HSE
print("=== step1: HSEON ===")
print(send(sock,"mww 0x40023800 0x00010000"))  # HSEON=bit16
for i in range(10):
    time.sleep(0.2)
    out=send(sock,"mdw 0x40023800 1")
    if "HSERDY" in out or "0x40023800" in out:
        val=out.split("0x40023800:")[1].split()[0]
        hsrdy = (int(val,16)>>17)&1
        print(f"  try{i}: CR={val} HSERDY={hsrdy}")
        if hsrdy: break

# 2) configure PLL: PLLM=25, PLLN=336, PLLP=DIV2, PLLSRC=HSE, PLLQ=4
print("=== step2: PLLCFG ===")
# PLLCFGR reset default 0x24003010; set PLLSRC=HSE(1<<22), PLLM=25, PLLN=336, PLLP=0, PLLQ=4
# base = (4<<24)|(2<<16) = 0x04020000; need M=25 => 25<<0, N=336<<6, P=0, SRC=1<<22, Q=4<<24
pll = (25)|(336<<6)|(0<<16)|(1<<22)|(4<<24)
print("PLLCFGR write:", hex(pll))
print(send(sock,"mww 0x40023804 "+hex(pll)))
print(send(sock,"mdw 0x40023804 1"))

# 3) enable PLL
print("=== step3: PLLON ===")
print(send(sock,"mww 0x40023800 0x01010000"))  # PLLON=bit24 | HSEON=bit16
for i in range(10):
    time.sleep(0.2)
    out=send(sock,"mdw 0x40023800 1")
    val=out.split("0x40023800:")[1].split()[0]
    pllrdy=(int(val,16)>>25)&1
    print(f"  try{i}: CR={val} PLLRDY={pllrdy}")
    if pllrdy: break

# 4) switch SYSCLK to PLL
print("=== step4: SW=PLL ===")
print(send(sock,"mww 0x40023808 0x00000002"))
time.sleep(0.5)
out=send(sock,"mdw 0x40023808 1")
print("CFGR:", out)
val=out.split("0x40023808:")[1].split()[0]
sws=(int(val,16)>>2)&3
print("SWS =", sws, "(2=PLL,0=HSI)")

sock.close()
