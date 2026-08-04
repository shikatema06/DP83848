---
name: build-and-flash
description: 构建系统配置、构建命令和烧录方式
metadata:
  type: reference
---

# 构建与烧录

**工具链:**
- GCC ARM Embedded (GNU Arm Embedded Toolchain)
- CMake 3.22+, Ninja
- STM32CubeProgrammer (STM32_Programmer_CLI)

**构建命令:**
```bash
cmake -B build/Debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/Debug -j 8                        # 构建应用
cmake --build build/Debug --target DP83848_BOOT -j 8  # 构建引导加载程序
```

**烧录命令:**
```bash
# 烧录应用 (会自动复位运行)
STM32_Programmer_CLI -c port=SWD -w "build/Debug/DP83848.elf" -v -rst

# 烧录引导加载程序 (需要先烧录再复位)
STM32_Programmer_CLI -c port=SWD -w "build/Debug/DP83848_BOOT.elf" -v
```

**链接脚本:**
- `STM32F407XX_APP.ld` — 应用在 0x08010000 (832 KB)
- `STM32F407XX_BOOT.ld` — 引导加载程序在 0x08000000 (64 KB)
- CMakeLists.txt 中通过 `target_link_options` 传递 `-T` 参数

**CMakePresets.json:** 包含 `Debug` 预设配置

**如何应用:** 对应用层代码修改只需构建 DP83848 目标。修改引导加载程序才需要构建 DP83848_BOOT。烧录引导加载程序会覆盖整个 Flash，需先保存 net_config 数据。