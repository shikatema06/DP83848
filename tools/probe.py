import socket, time, sys, io

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
            if not chunk:
                break
            data += chunk
            if data.rstrip().endswith(b">"):
                break
    except socket.timeout:
        pass
    return data.decode(errors="replace")

def addr2line(addr):
    import subprocess
    r = subprocess.run(["arm-none-eabi-addr2line", "-e", ELF, "-f", "-C", addr],
                       capture_output=True, text=True)
    return r.stdout.strip().replace("\n", " | ")

def main():
    sock = socket.create_connection((HOST, PORT), timeout=5)
    time.sleep(0.5)
    sock.settimeout(2.0)
    try:
        sock.recv(4096)
    except socket.timeout:
        pass

    print("=== reset halt ===")
    print(send(sock, "reset halt"))
    print(send(sock, "reg pc"))

    # Set HW breakpoint at Error_Handler (0x08007bf0)
    print("=== set bp at Error_Handler 0x08007bf0 ===")
    print(send(sock, "bp 0x08007bf0 2 hw"))

    print("=== resume, wait up to 8s ===")
    print(send(sock, "resume"))

    hit = False
    for _ in range(16):
        time.sleep(0.5)
        out = send(sock, "halt")
        if "halted due to" in out:
            hit = True
            print("=== HALTED ===")
            print(out)
            break

    if hit:
        print("=== reg lr / pc ===")
        print(send(sock, "reg lr"))
        print(send(sock, "reg pc"))
        # read stack to find return address: MSP at halt; look at [MSP+0] = return addr (LR saved) ...
        # Actually we are IN Error_Handler, so stack frame top = return address into caller
        msp = send(sock, "reg msp")
        print("MSP:", msp)
        # read 8 words from MSP
        print(send(sock, "mdw " + msp.split("0x")[1][:8] + " 8" if "0x" in msp else "mdw 0x2001ff00 8"))
        # also read RCC registers
        print("=== RCC CR/CFGR ===")
        print(send(sock, "mdw 0x40023800 4"))
        print("=== RCC_PLLCFGR 0x40023804 ===")
        print(send(sock, "mdw 0x40023804 1"))

    sock.close()

if __name__ == "__main__":
    main()
