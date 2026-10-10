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
# App Reset_Handler = 0x080205e5; App main around there
APP_RESET=0x080205e5
sock=socket.create_connection((HOST,PORT),timeout=5); time.sleep(0.5); sock.settimeout(2.0)
try: sock.recv(4096)
except socket.timeout: pass
print("=== reset halt ===")
print(send(sock,"reset halt"))
print("=== bp at App Reset_Handler ===")
print(send(sock,"bp "+hex(APP_RESET)+" 2 hw"))
print("=== resume 7s ===")
print(send(sock,"resume"))
hit=False
for _ in range(16):
    time.sleep(0.5)
    out=send(sock,"halt")
    if "halted due to" in out:
        pcout=send(sock,"reg pc")
        print(pcout)
        pc=pcout.split("0x")[1][:8] if "0x" in pcout else ""
        print("PC=",pc)
        if pc.lower()==hex(APP_RESET)[2:]:
            print(">>> REACHED APP RESET HANDLER - JUMP SUCCESSFUL")
            hit=True
        else:
            print(">>> PC in other region")
        # read App state
        print("VTOR:", send(sock,"mdw 0xE000ED08 1"))
        print("CFGR:", send(sock,"mdw 0x40023808 1"))
        break
print("reached_app_reset:", hit)
sock.close()
