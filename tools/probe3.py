import socket, time, sys, io, subprocess

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
HOST = "127.0.0.1"
PORT = 4444
ELF = "c:/ST_project/DP83848/build/Debug/Bootloader/DP83848_BOOT.elf"

def send(sock, cmd):
    sock.sendall((cmd + "\n").encode())
    time.sleep(0.3)
    data = b""
    sock.settimeout(2.0)
    try:
        while True:
            chunk = sock.recv(4096)
            if not chunk: break
            data += chunk
            if data.rstrip().endswith(b">"): break
    except socket.timeout:
        pass
    return data.decode(errors="replace")

def a2l(addr):
    r = subprocess.run(["arm-none-eabi-addr2line","-e",ELF,"-f","-C",addr],capture_output=True,text=True)
    return r.stdout.strip().replace("\n"," | ")

sock = socket.create_connection((HOST,PORT),timeout=5)
time.sleep(0.5); sock.settimeout(2.0)
try: sock.recv(4096)
except socket.timeout: pass

print("pc:", send(sock,"reg pc"))
print("lr:", send(sock,"reg lr"))
print("msp:", send(sock,"reg msp"))
# read 12 words from MSP to find return address chain
msp_out = send(sock,"reg msp")
msp_hex = "0x" + msp_out.split("0x")[1][:8]
print("MSP hex:", msp_hex)
print("stack:", send(sock,"mdw "+msp_hex+" 12"))
# caller return address is at msp+0 (top of stack when in Error_Handler, LR was pushed)
# Actually Error_Handler was called by bl, so [MSP+0] holds return addr
print("=== RCC CR/CFGR/PLLCFGR/CIR ===")
print(send(sock,"mdw 0x40023800 4"))
print("=== read SystemCoreClock var: find via symbol ===")
# locate SystemCoreClock address
r = subprocess.run(["arm-none-eabi-nm","-C",ELF],capture_output=True,text=True)
for line in r.stdout.splitlines():
    if "SystemCoreClock" in line:
        addr = line.split()[0]
        print("SystemCoreClock @", addr)
        print(send(sock,"mdw 0x"+addr+" 1"))
        break
sock.close()
