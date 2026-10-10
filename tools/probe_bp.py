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

# which call hit: 0x08007cda = OscConfig error, 0x08007d08 = ClockConfig error
OSC_BP=0x08007cda; CLK_BP=0x08007d08

sock=socket.create_connection((HOST,PORT),timeout=5); time.sleep(0.5); sock.settimeout(2.0)
try: sock.recv(4096)
except socket.timeout: pass

print("=== reset halt ===")
print(send(sock,"reset halt"))
print(send(sock,"bp "+hex(OSC_BP)+" 2 hw"))
print(send(sock,"bp "+hex(CLK_BP)+" 2 hw"))
print("=== resume, wait up to 8s ===")
print(send(sock,"resume"))
hit=None
for _ in range(16):
    time.sleep(0.5)
    out=send(sock,"halt")
    if "halted due to" in out:
        print("=== HALTED ===")
        pcout=send(sock,"reg pc")
        print(pcout)
        pc = pcout.split("0x")[1][:8] if "0x" in pcout else ""
        if pc.lower()==hex(OSC_BP)[2:]:
            hit="OSC_CONFIG(0x%08x)"%OSC_BP
        elif pc.lower()==hex(CLK_BP)[2:]:
            hit="CLK_CONFIG(0x%08x)"%CLK_BP
        else:
            hit="other(0x"+pc+")"
        print("HIT:", hit)
        break
print("RESULT:", hit)
sock.close()
