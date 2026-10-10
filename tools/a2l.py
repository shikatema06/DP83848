import subprocess, sys
ELF="c:/ST_project/DP83848/build/Debug/DP83848_BOOT.elf"
for a in sys.argv[1:]:
    r=subprocess.run(["C:/ST/STM32CubeCLT_1.18.0/GNU-tools-for-STM32/bin/arm-none-eabi-addr2line.exe","-e",ELF,"-f","-C",a],capture_output=True,text=True)
    print(a, "->", r.stdout.strip().replace("\n"," | "))
