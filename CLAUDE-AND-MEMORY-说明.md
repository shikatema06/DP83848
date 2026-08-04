# CLAUDE.md 与 Memory 说明文档

## CLAUDE.md

**文件位置:** 项目根目录 `/CLAUDE.md`

**是什么:** 一个**项目说明书**，由我（Claude）根据项目代码结构、构建系统、硬件配置等信息主动编写。它类似于新开发者的"入职文档"——让任何进入此项目的 AI 或开发者快速了解全局。

**包含内容:**
- 项目概述（MCU、PHY、软件栈）
- 内存布局（Flash 分区、链接脚本）
- 构建与烧录命令
- 外设配置总表
- 应用层架构（TCP 协议、ADC 流、网络配置、引导加载程序）
- 关键文件索引
- 代码规范与调试指南

**什么时候更新:** 当项目架构发生变化（新增外设、修改内存布局、更换工具链等）时，可以要求我更新此文件。

---

## Memory（记忆系统）

**文件位置:** `.claude/memory/` 目录下，每个文件一个独立事实

**是什么:** Claude 的**持久化记忆**，用于跨会话记住关键信息。每次新会话启动时，Memory 索引 (`MEMORY.md`) 会被加载到上下文中，让我"记得"以前了解过的内容。

**已创建的 Memory 文件:**

| 文件 | 内容摘要 |
|---|---|
| `project-overview.md` | 项目总体描述、核心功能清单 |
| `hardware-config.md` | MCU 引脚、时钟树、外设参数 |
| `build-and-flash.md` | 构建命令、烧录方式、链接脚本 |
| `architecture.md` | Flash 分区设计、应用层架构、IAP 引导、PHY 驱动模式 |
| `flash-layout.md` | 详细 Flash 扇区分配和保护约束 |

**何时添加/更新 Memory:**
- 发现重要的设计决策（为什么用 TIM4 而非 SysTick？为什么 PE7 而非 PD3？）
- 关于你的偏好（编码风格、命名约定、测试方法）
- 关于工具链或环境配置的特殊说明

**如何触发:** 当讨论到重要的、非显而易见的信息时，可以说"记住这个"或"把这个记下来"，我会写入新的 Memory 文件。

---

## 二者关系

```
CLAUDE.md               ← 项目说明书，放在根目录，人人可读
  └── 项目范围、构建命令、外设总表、关键文件索引

.claude/memory/          ← Claude 的内部记忆，关注细节和原因
  ├── MEMORY.md          ← 索引文件（加载到上下文中）
  ├── project-overview.md
  ├── hardware-config.md
  ├── build-and-flash.md
  ├── architecture.md    ← flash-layout.md 等深层细节
  └── ...
```

**简要理解:**
- **CLAUDE.md** = README 的增强版，给 AI 助手看的项目手册
- **Memory** = Claude 的私人笔记本，记住你说过的偏好和项目中的关键决策原因

两者配合：CLAUDE.md 提供静态的项目全貌，Memory 记录动态积累的项目知识和你的个人偏好。
