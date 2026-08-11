# PCIe 学习资源与路线图

本文档整合了 PCI Express 协议、驱动开发、Linux 内核实现、FPGA 实验等方向的
系统学习资源。每个阶段列出了关键概念、推荐书籍、博客文章和实践方式。

## 总体学习路径

```text
阶段 1: 协议基础（拓扑 / 配置空间布局 / BAR / 枚举 / Type 0&1 Header）
  → 阶段 2: TLP 与事务层（Header 逐字段 / Posted vs Non-Posted / 路由）
    → 阶段 2.5: 数据链路层（DLLP / ACK-NAK / Replay Buffer / 信用流控）
      → 阶段 3: 链路训练与物理层（LTSSM 11 状态 / 编码 / SerDes / CR_PARA）
        → 阶段 4: 中断机制（INTx → MSI → MSI-X / 内核 API / RISC-V PLIC 路径）
          → 阶段 5: Linux PCIe 子系统（host bridge / DWC 驱动 / 枚举核心源码）
            → 阶段 6: 动手实践（U-Boot/Linux 调试 / bare metal / FPGA）
              → 阶段 7: 高级特性（AER / ASPM / SR-IOV / ReBAR / DPC）
```

---

## 阶段 1：PCIe 协议基础 — 拓扑、配置空间、枚举

### 1.1 这个阶段要掌握什么

| 知识点 | 说明 |
|---|---|
| **PCIe 拓扑** | Root Complex → Switch → Endpoint 树形结构。理解 RC 是 CPU/内存侧的桥，EP 是设备 |
| **BDF 地址** | `Bus(8):Device(5):Function(3)` 16 位三元组。Bus 0 总是 RC 侧，Device 0 通常是 host bridge |
| **配置空间** | 每个 Function 4KB：前 256B 是 PCI 兼容头 (Vendor/Device ID, BAR, Command/Status)，后 4KB 是 PCIe 扩展能力 (Capability linked list) |
| **Type 0 vs Type 1** | Type 0 = Endpoint (有 6 个 BAR)，Type 1 = Bridge/Switch (有 2 个 BAR + 总线号寄存器) |
| **BAR 机制** | 设备声明所需 MMIO/IO 地址空间的寄存器。Bit 0 区分 Memory(0) vs I/O(1)；写入全 1 回读计算 size |
| **ECAM** | Enhanced Configuration Access Mechanism — 将配置空间映射到内存地址，每个 BDF 对应 4KB 偏移 |
| **枚举过程** | 深度优先遍历 BDF，读 Vendor ID。0xFFFF 表示设备不存在，否则读完整配置头 |
| **ATU/iATU** | DWC 控制器特有的地址转换单元，将 CPU 物理地址翻译为 PCIe 总线地址 |

### 1.2 配置空间完整布局

**Type 0 Header（Endpoint，偏移 0x00 ~ 0x3F）：**

```text
Offset  Register                        Bits
0x00    Vendor ID                      [15:0]
        Device ID                      [31:16]
0x04    Command                        [15:0]    ← Bus Master, Memory Space, I/O Space 等使能位
        Status                         [31:16]   ← Capability List, Interrupt Status 等
0x08    Revision ID                    [7:0]
        Class Code                     [31:8]    ← [23:16]=Base, [15:8]=Sub, [7:0]=Prog IF
0x0C    Cache Line Size               [7:0]
        Latency Timer                  [15:8]
        Header Type                    [23:16]   ← bit7=0(Multi-Func), bits[6:0]=0(Type 0)
        BIST                           [31:24]
0x10    BAR0                           [31:0]    ← bit0=0(MEM) / bit0=1(IO)
0x14    BAR1                           [31:0]    ← BAR0+BAR1 可组 64-bit BAR
0x18    BAR2                           [31:0]
0x1C    BAR3                           [31:0]
0x20    BAR4                           [31:0]
0x24    BAR5                           [31:0]
0x28    Cardbus CIS Pointer            [31:0]
0x2C    Subsystem Vendor ID           [15:0]
        Subsystem Device ID            [31:16]
0x30    Expansion ROM Base Address     [31:0]
0x34    Capabilities Pointer           [7:0]     ← 指向 Capability Linked List 链表头
        Reserved                       [31:8]
0x38    Reserved
0x3C    Max_Latency                   [15:8]
        Min_Grant                      [31:24]
        Interrupt Pin                  [15:8]    ← 0x01=INTA#, 0x02=INTB#...
        Interrupt Line                 [7:0]     ← 系统分配的 IRQ 号（x86 PIC 模式）
```

**Type 1 Header（Bridge/Switch — RC 的 Root Port 也是 Type 1）：**

与 Type 0 的差异在 0x10 ~ 0x24 区域：

```text
0x10    BAR0                           [31:0]    ← Bridge 只有 2 个 BAR
0x14    BAR1                           [31:0]
0x18    Primary Bus Number            [7:0]      ← ⬅ 上游 Bus 号
        Secondary Bus Number           [15:8]    ← ⬅ 下游第一个 Bus 号
        Subordinate Bus Number         [23:16]   ← ⬅ 下游最大 Bus 号
        Secondary Latency Timer       [31:24]
0x1C    IO Base                       [7:0]      ← ⬅ 下游 IO 窗口 base
        IO Limit                       [15:8]    ← ⬅ 下游 IO 窗口 limit
        Secondary Status              [31:16]
0x20    Memory Base                    [15:0]    ← ⬅ 下游 MMIO 窗口 base
        Memory Limit                   [31:16]   ← ⬅ 下游 MMIO 窗口 limit
0x24    Prefetchable Memory Base      [15:0]    ← ⬅ 下游 Prefetchable MMIO 窗口
        Prefetchable Memory Limit      [31:16]
0x28    Prefetchable Base (Upper)     [31:0]    ← 64-bit 扩展
0x2C    Prefetchable Limit (Upper)    [31:0]    ← 64-bit 扩展
0x30    IO Base (Upper)               [15:0]    ← 64-bit 扩展
        IO Limit (Upper)              [31:16]
0x34    Capabilities Pointer           [7:0]
        Reserved                       [31:8]
0x38    Expansion ROM Base Address     [31:0]
0x3C    Bridge Control                [15:0]    ← Secondary Bus Reset 等控制位
        Interrupt Pin                  [15:8]
        Interrupt Line                 [7:0]
```

**三个总线号的作用：**

操作系统在枚举过程中设置桥的总线号寄存器——这正是枚举的核心输出：

```text
枚举步骤:
1. 发现 Bridge 设备 (Header Type bit[6:0] = 1)
2. 分配新 bus 号: secondary = next_bus++
3. 递归扫描 secondary bus
4. 递归返回后: subordinate = max_bus_found
5. 写 Type 1 Header: Primary / Secondary / Subordinate

之后，桥接器会将以下 TLP 转发到 secondary bus:
  - Type 1 Config TLP: 目标 Bus 号在 [Secondary, Subordinate] 范围内
  - Memory TLP: 地址落在 Bridge 的 Memory Base/Limit 窗口内
```

**Capability Linked List：**

```text
Capabilities Pointer (0x34) = 0x40
  → offset 0x40: Capability ID = 0x01 (PCI Power Management)
                 Next Pointer  = 0x50
    → offset 0x50: Capability ID = 0x05 (MSI)
                   Next Pointer = 0x70
      → offset 0x70: Capability ID = 0x10 (PCI Express Capability)
                     Next Pointer = 0xB0
        → offset 0xB0: Capability ID = 0x11 (MSI-X)
                       Next Pointer = 0x00 (链表结束)

常见 Capability ID:
  0x01 = PCI Power Management
  0x05 = MSI
  0x10 = PCI Express Capability (含 Link Cap/Status、Device Cap/Status 等)
  0x11 = MSI-X
```

### 1.3 书籍推荐

这本书有一个社区驱动的开源中文翻译项目，由 Michael ZZY、LJGibbs 等人协作翻译，
Chapter 1-8 已完成并发布在知乎和腾讯云：

| 平台 | 链接 | 说明 |
|---|---|---|
| **GitHub 源码仓库** | https://github.com/ljgibbslf/Chinese-Translation-of-PCI-Express-Technology | Markdown 源文件，MIT 协议 |
| **Gitee 镜像** | https://gitee.com/sdlhk/chinese-translation-of-pci-express-technology | 国内访问更快 |
| **知乎连载** | https://zhuanlan.zhihu.com/p/449184680 | Chapter 1 Background（以此为入口找后续章节） |
| **知乎 Ch.2** | https://zhuanlan.zhihu.com/p/452450617 | Chapter 2 PCIe Architecture Overview |
| **腾讯云社区** | https://cloud.tencent.com.cn/developer/article/1925726 | 第一章①（多篇连载） |
| **微信公众号** | 搜索 "PCIe 系列博文更新计划" | 完整翻译章节规划 + 定期更新 |

**翻译进度：**
- Chapter 1-8: ✅ Michael ZZY 翻译完成（含背景、架构概述、配置、地址路由、TLP 等）
- Chapter 9, 12: ✅ 完成
- Chapter 10, 11, 13, 14, 15, 17: 🔄 进行中
- 校对：LJGibbs、XTtang、Karl_DGR
- 联系作者: lf_gibbs@163.com

**书籍阅读顺序：**
1. 马鸣锦《PCI、PCI-X 和 PCI Express 的原理及体系结构》（1-2 天快速过一遍建立直觉）
2. 王齐《PCI Express 体系结构导读》第 1-6 章（精读，2 周）
3. MindShare *Technology 3.0* Ch.3-4（对照加深，1 周）
4. MindShare *System Architecture* 枚举章节（补充伪代码细节）

### 1.3 博客与在线教程

**入门系列：**

| 博客 | 链接 | 说明 |
|---|---|---|
| **博客** | **链接** | **说明** |
|---|---|---|
| **CSDN: 21天学会PCIe专栏** | https://blog.csdn.net/xiaoheshang_123/article/details/143440989 | 21天系统学习计划：从架构→术语→配置空间→事务→中断→DMA→驱动 |
| **知乎：[持续演进] 可以学习 1W 小时的 PCIe** | https://zhuanlan.zhihu.com/p/447134701 | 系统整理了 PCIe 各层学习资料、MindShare 翻译合集、学习笔记。**必读索引** |
| **GitHub: Learning-PCIe-from-scratch** | https://github.com/linuslau/Learning-PCIe-from-scratch | 从零学 PCIe：图解、笔记、协议分析仪 trace 日志、Q&A 格式。**稀缺的实战资料** |
| **GitHub: PCIe-CXL-study** | https://github.com/toaneliyan/PCIe-CXL-study | PCIe 和 CXL 学习笔记仓库 |
| **CSDN: PCIe 学习重点提纲** | https://blog.csdn.net/yao00037/article/details/139606967 | 知识体系总览：基础知识→分层模型→物理层→数据链路层→事务层→配置→性能 |
| **CSDN: PCIe 学习计划** | https://blog.csdn.net/yao00037/article/details/139607036 | 附带评论区的学习路线讨论 |

**配置空间与枚举：**

| 博客 | 链接 | 说明 |
|---|---|---|
| **CSDN: 大话PCIe — BAR空间和TLP** | https://blog.csdn.net/mshgocn/article/details/78004130 | 中文，BAR 机制通俗讲解 |
| **CSDN: PCIe实践之路 — BAR空间和TLP** | https://blog.csdn.net/abcamus/article/details/74157026 | BAR 空间分配 + TLP 路由实战 |
| **与非网: PCIe原理 — BAR0/1是如何配置的** | https://fpga.eetrend.com/blog/2019/100046257.html | 从 FPGA 视角看 BAR 配置的底层流程 |
| **Stack Overflow: PCIe device discovery pseudo code** | https://stackoverflow.com/questions/41727146 | 枚举算法伪代码讨论 |
| **IIT Bombay: PCI Express 课件** | https://www.cse.iitb.ac.in/~cs330/pci_exp.pdf | 学术课件，协议层视图清晰 |

**三层架构入门：**

| 博客 | 链接 | 说明 |
|---|---|---|
| **CSDN: 别再死记硬背了！彻底搞懂PCIe的'三层楼'** | https://blog.csdn.net/weixin_29266679/article/details/159634794 | 用快递站与高速公路类比，**最适合零基础** |
| **CSDN: PCIe笔记3 — Device Layers** | https://blog.csdn.net/asfgj123/article/details/148918742 | 三层分工作用、数据"穿衣服脱衣服"比喻 |
| **ProgrammerSought: old boy read PCIe — 分层结构** | https://www.programmersought.com/article/7172682289/ | 老男孩读 PCIe 系列第三篇 |
| **CSDN: 深入浅出PCIe技术原理与应用入门** | https://wenku.csdn.net/doc/6h6i42bizr | CSDN 文库的汇总式入门文档 |

### 1.4 本项目实践

```bash
# 在 Unmatched 物理板或 QEMU 上操作

# U-Boot: 观察配置空间
pci enum                       # 枚举 PCIe 总线
pci 0                          # 显示所有设备的 BDF、Vendor/Device ID、BAR
pci header 0.1.0               # 显示指定设备的配置头 raw dump
md 0xd00000000 64              # 读 Bus 0 Dev 0 Func 0 的 256B 配置空间 (ECAM)

# Linux: 观察配置空间
lspci -vvv -xxx                # 超级详细 + 完整 4KB 配置空间 hex dump
lspci -vvv -t                  # 树形拓扑
lspci -nn                      # 数字 Vendor/Device ID
hexdump -C /sys/bus/pci/devices/0000:00:00.0/config  # 读 raw 配置空间
setpci -s 00:00.0 0x04.L      # 读 Command/Status 寄存器 (offset 0x04, 32-bit)
setpci -s 01:00.0 0x10.L      # 读 BAR0
```

**动手验证 BAR sizing 算法：**

```bash
# 在 Linux 上观察内核如何探测 BAR 大小
dmesg | grep -A 5 "BAR"
cat /sys/bus/pci/devices/0000:01:00.0/resource
# resource 文件显示每个 BAR 的 start-end-flags
```

---

## 阶段 2：TLP 与事务层

### 2.1 这个阶段要掌握什么

| 知识点 | 说明 |
|---|---|
| **TLP 结构** | Header (3/4 DW) + Data Payload (0~1024 DW) + 可选的 TLP Digest (1 DW ECRC) |
| **Fmt + Type 字段** | Byte 0 的 Fmt[1:0](Header 大小 + 是否有数据) + Type[4:0](事务类型) 联合编码 TLP 类型 |
| **Posted vs Non-Posted** | 理解 PCIe 性能的核心：Posted (MWr/Msg) 发射后不管；Non-Posted (MRd/Cfg) 必须等 Completion |
| **路由方式** | 地址路由 (Memory/I/O)、ID 路由 (Config)、隐式路由 (Message) |
| **Transaction Descriptor** | Requester ID + Tag 组成全局唯一的 Transaction ID，Completion 靠它找到请求者 |
| **Byte Enables** | 首尾 DW 可以有非连续 byte enable，中间 DW 必须全部 enable |
| **TC/VC** | Traffic Class (8 级) + Virtual Channel 实现 QoS |
| **流量控制** | Data Link Layer 的 credit-based 流控，与 Transaction Layer 协同 |

### 2.2 Fmt+Type 编码速查表

| TLP 类型 | Fmt[1:0] | Type[4:0] | Posted? | 说明 |
|---|---|---|---|---|
| MRd (Memory Read) | 00/01 | 0 0000 | No | 3DW=32bit addr, 4DW=64bit addr |
| MRdLk (Memory Read Locked) | 00/01 | 0 0001 | No | 原子读 |
| MWr (Memory Write) | 10/11 | 0 0000 | **Yes** | Posted，无 Completion |
| IORd (I/O Read) | 00 | 0 0010 | No | 仅 32-bit 地址 |
| IOWr (I/O Write) | 10 | 0 0010 | No | Non-Posted |
| CfgRd0 (Config Read Type 0) | 00 | 0 0100 | No | Endpoint 配置读 |
| CfgWr0 (Config Write Type 0) | 10 | 0 0100 | No | Endpoint 配置写 |
| CfgRd1 (Config Read Type 1) | 00 | 0 0101 | No | Bridge 配置读 |
| CfgWr1 (Config Write Type 1) | 10 | 0 0101 | No | Bridge 配置写 |
| Msg (Message) | 01 | 10 rrr* | **Yes** | 无数据消息 |
| MsgD (Message w/ Data) | 11 | 10 rrr* | **Yes** | 带数据消息 (MSI 中断等) |
| Cpl (Completion) | 00 | 0 1010 | — | 无数据完成 |
| CplD (Completion w/ Data) | 10 | 0 1010 | — | 带数据完成 |
| CplLk (Completion Locked) | 00 | 0 1011 | — | 锁定完成 |

### 2.3 Header 字段详解

以 Memory Write TLP (3DW Header) 为例：

```text
DW0 [31:0]:
  [31:29] Fmt      = 10b (3DW header + data)
  [28:24] Type     = 0_0000b (MWr)
  [23]    Reserved
  [22:20] TC       = Traffic Class
  [19:18] Attr     = Attributes (Relaxed Ordering / No Snoop)
  [17]    TH       = TPH hint
  [16]    TD       = TLP Digest present
  [15]    EP       = Poisoned data
  [14:13] Attr     = Attributes (续)
  [12:11] AT       = Address Type (00 = untranslated)
  [10:0]  Length   = Data payload in DWs

DW1 [31:0]:
  [31:16] Requester ID = {Bus[7:0], Device[4:0], Function[2:0]}
  [15:8]  Tag          = 每 Function 内唯一，Completion 靠它匹配
  [7:4]   Last DW BE   = 最后一个 DW 的 byte enable
  [3:0]   1st DW BE    = 第一个 DW 的 byte enable

DW2 [31:0]:
  [31:2]  Address[31:2] = 目标内存地址 (低 2 bit 忽略)
  [1:0]   PH            = 保留
```

### 2.4 推荐博客

| 博客 | 链接 | 说明 |
|---|---|---|
| **腾讯云: PCIe 系列第二讲 — OSI模型与事务层分析(上)** | https://cloud.tencent.cn/developer/article/1652995 | 腾讯云 PCIe 系列第二篇，TLP 包格式 + 事务类型详解 |
| **腾讯云: TLP 学习经验分享** | https://cloud.tencent.cn/developer/article/1766484 | TLP 格式详解 + 抓包示例（中文，必读） |
| **CSDN: PCIe 地址空间 — 内存、IO与配置** | https://blog.csdn.net/z534437748/article/details/160493165 | 三种地址空间的区别和 TLP 对应关系 |
| **CSDN: PCIe IO读写事务 — 从一次诡异的设备失联说起** | https://blog.csdn.net/z534437748/article/details/160767392 | **实战案例** — IO 读写导致的 bug 排查，理解 Posted/Non-Posted |
| **ProgrammerSought: PCIe Transaction Layer** | https://www.programmersought.com/article/49537100959/ | 事务层协议详解，配合代码 |
| **Rambus: TLP (Transaction Layer Packet) Glossary** | https://www.rambus.com/chip-interface-ip-glossary/transaction-layer-packet/ | TLP 术语官方定义 |
| **rtlp-lib (Rust TLP 解析库)** | https://docs.rs/crate/rtlp-lib/ | 从代码级理解 TLP 结构 |
| **Tencent: 访问PCIe BAR空间** | https://www.programmersought.com/article/77645274807/ | 如何通过 /sys 和 mmap 访问 BAR 空间 |
| **与非网: 浅析PCIe地址空间 — BAR与ATU** | https://www.e-com-net.com/article/1297163741355384832.htm | 三种地址 (CPU 物理 → PCIe 总线 → 设备 BAR) 的关系 |

**理解三种地址转换关系是 PCIe 学习的分水岭：**

```text
CPU 物理地址 (0x60090000)
      │
      ▼  iATU outbound translation
PCIe 总线地址 (Memory Write TLP 中的地址字段)
      │
      ▼  BAR 地址路由 (TLP 地址落在哪个 BAR 的窗口内?)
设备内部地址 (NVMe 控制器的 MMIO 寄存器)
```

### 2.5 本项目实践

```bash
# ftrace 观察 PCIe 层间调用
echo function > /sys/kernel/debug/tracing/current_tracer
echo 'pci*' > /sys/kernel/debug/tracing/set_ftrace_filter
echo 1 > /sys/kernel/debug/tracing/tracing_on
# 执行 PCIe 操作后查看
cat /sys/kernel/debug/tracing/trace

# dynamic debug 观察 DWC 层寄存器访问
echo 'file pcie-designware*.c +p' > /sys/kernel/debug/dynamic_debug/control
echo 'file pcie-fu740.c +pflmt' > /sys/kernel/debug/dynamic_debug/control
dmesg -w | grep -E 'atu|iATU|outbound|inbound|tlp|rd|wr'

# 观察 BAR 空间
cat /proc/iomem | grep pci
cat /sys/bus/pci/devices/0000:01:00.0/resource
```

---

## 阶段 2.5：数据链路层 — DLLP 与 ACK/NAK

### 这个阶段要掌握什么

数据链路层（Data Link Layer）在事务层和物理层之间，**不创建新的数据内容**，
而是为 TLP 传输提供可靠性保障。

| 职责 | 说明 |
|---|---|
| **TLP 保序传输** | 确保 TLP 按发送顺序到达对端，不丢不乱序 |
| **错误检测** | 为每个 TLP 计算 LCRC (Link CRC)，对端验证失败触发重传 |
| **ACK/NAK 协议** | 收到正确 TLP → 回 ACK DLLP；LCRC 校验失败 → 回 NAK DLLP → 发送端从 Replay Buffer 重传 |
| **信用流控** | 接收端定期广播 Credit (FC DLLP)，告知发送端自己还有多少 buffer 空间 |
| **电源管理** | DLLP 承载 PM_Enter_L1、PM_Request_Ack 等链路电源管理握手 |

### DLLP 格式

DLLP (Data Link Layer Packet) 只有 **8 字节**（1 DW 数据 + 2 字节 CRC），
只在本 Link 段的两端之间交换，**不跨越 Switch**：

```text
Byte 0: DLLP Type                   ← 区分 ACK/NAK/FC 等
Byte 1-3: Attributes                ← 随 Type 不同而变化 (ACK: SeqNum, FC: Credits)
Byte 4-5: CRC (16-bit)
```

DLLP 不需要路由，物理层收到后立即在数据链路层消费，不向上传递。

### ACK/NAK 协议与 Replay Buffer

```text
发送端                                   接收端
  │                                        │
  ├─ 发送 TLP(Seq#=5) ──────────────────→  │
  │   └─ TLP 副本存入 Replay Buffer         ├─ LCRC 校验 ✓
  │                                        ├─ 向上传递 TLP
  │   ←──────────── ACK(Seq#=5) ──────────┤
  ├─ 收到 ACK → 从 Replay Buffer 删除       │
  │                                        │
  ├─ 发送 TLP(Seq#=6) ──────────────────→  │
  │   └─ 副本存入 Replay Buffer              ├─ LCRC 校验 ✗ (bit error)
  │                                        │
  │   ←──────────── NAK(Seq#=6) ──────────┤
  ├─ 收到 NAK → 从 Replay Buffer 取出       │
  ├─ 重传 TLP(Seq#=6) ──────────────────→  │
  │                                        ├─ LCRC 校验 ✓ → 向上传递
  │   ←──────────── ACK(Seq#=6) ──────────┤
  ├─ 清除 Replay Buffer                     │
```

**关键点：**
- ACK 采用累加确认：ACK(Seq#=N) 表示 Seq# <= N 的 TLP 全部正确接收
- NAK 不会导致链路断开，只是性能下降；连续 NAK 触发链路重训练 (Recovery)
- Replay Buffer 大小决定了有多少未确认 TLP 可以 "在飞"

### 信用流控 (Credit-Based Flow Control)

PCIe 使用基于信用的流控而非简单的 Ready/Busy 握手。接收端通过 FC DLLP
定期广播各类型 buffer 的可接收量：

| Credit 类型 | 对应 TLP | 说明 |
|---|---|---|
| **PH (Posted Header)** | MWr Header | Posted 事务的 Header Credit |
| **PD (Posted Data)** | MWr Data | Posted 事务的 Data Credit |
| **NPH (Non-Posted Header)** | MRd/CfgRd Header | Non-Posted 请求的 Header Credit |
| **NPD (Non-Posted Data)** | Data in NP | Non-Posted 请求的 Data Credit |
| **CPLH (Completion Header)** | Cpl/CplD Header | Completion 的 Header Credit |
| **CPLD (Completion Data)** | CplD Data | Completion 的 Data Credit |

流控规则：发送端每发一个 TLP 对应 Credit 减 1；接收端释放 buffer 后广播新增
Credit；Credit ≤ 0 时禁止发送。这保证了接收端 buffer 永不溢出。

### 推荐博客

| 博客 | 链接 | 说明 |
|---|---|---|
| **CSDN: PCIe笔记3 — Data Link Layer** | https://blog.csdn.net/asfgj123/article/details/148918742 | DLLP 类型、ACK/NAK 协议、Replay Buffer |
| **CSDN: PCIe体系结构导读随记 — 数据链路层** | https://blog.csdn.net/phmatthaus/article/details/141828029 | DLCMSM 状态机 (DL_Inactive/Init/Active) |
| **与非网: PCIe DLLP 与 Flow Control** | 搜索 "PCIe DLLP ACK NAK flow control" | 流控信用机制详解 |

### 本项目实践

```bash
# 观察链路可靠性 — 若有 bit error 会在 dmesg 看到 AER 报错
dmesg | grep -iE "AER|corrected|uncorrected|replay|retry"

# lspci 查看 AER 能力
lspci -vvv -s 00:00.0 | grep -A 20 "Advanced Error Reporting"

# 查看 Max Payload Size 和 Max Read Request Size
lspci -vvv -s 01:00.0 | grep -E "DevCap|DevCtl" | head -4
```

---

## 阶段 3：链路训练 (LTSSM) 与物理层

### 3.1 这个阶段要掌握什么

| 知识点 | 说明 |
|---|---|
| **LTSSM 11 状态** | Detect → Polling → Configuration → L0 (主线) + Recovery (重训练) + L0s/L1/L2 (省电) + Hot Reset/Loopback/Disable |
| **训练目标** | Bit Lock (CDR)、Symbol/Block Lock (COM 字符)、Link Width、Lane Reversal、Polarity Inversion、De-skew、速率协商 |
| **Gen1 启动原则** | **所有链路训练从 Gen1 (2.5 GT/s) 开始**，成功后再切换到更高速度 |
| **TS1/TS2 Ordered Set** | 16 符号 (160 bit)：COM (K28.5) + Link/Lane Number + N_FTS + Rate ID + Training Control |
| **编码演进** | 8b/10b (Gen1/2, 20% 开销) → 128b/130b (Gen3+, ~1.54% 开销) |
| **CR_PARA PHY 接口** | FU740 使用的 Synopsys PHY 参数配置接口 (CR_PARA_ADDR/DATA/WR_EN/ACK) |
| **FU740 Gen1 quirk** | `fu740_pcie_start_link()` 先强制 Gen1 训练，link up 后再写到 LNKCAP 恢复原生速度 |

### 3.2 LTSSM 状态转换图（主线）

```text
复位 (Cold/Warm/Hot)
  → Detect.Quiet (12ms)
    → Detect.Active (检查对端 50Ω 终端)
      → Polling.Active (发送 TS1, bit lock + symbol lock)
        → Polling.Configuration (发送 TS2)
          → Configuration.LinkWidth.Start (协商 Lane 数)
            → Configuration.LinkWidth.Accept
              → Configuration.LaneNum.Wait (分配 Lane 号)
                → Configuration.LaneNum.Accept
                  → Configuration.Complete (≥2ms guard)
                    → Configuration.Idle (发送 Logical Idle)
                      → L0 (正常操作, Gen1)
                        → Recovery (速度切换)
                          → L0 (Gen3)
```

### 3.3 Detect 状态的关键细节

Detect 是 PCIe 最有巧思的设计之一——不需要任何协议协商就能检测对端存在：

```text
发送端: 在差分对上发送缓慢电压 ramp
接收端: 如果有 50Ω 终端 → RC 时间常数大 → 充电慢 → 接收端存在
        如果开路       → RC 时间常数小 → 充电快 → 无接收端

这是纯模拟电路行为，不依赖数字协议。
```

### 3.4 编码对比

| 特性 | 8b/10b (Gen1/2) | 128b/130b (Gen3+) |
|---|---|---|
| 有效带宽利用率 | 80% | ~98.46% |
| Block 大小 | 10 bit | 130 bit |
| 同步方式 | COM (K28.5) 字符 | Sync Header (2 bit: 01=Ordered Set, 10=Data) |
| Scrambler | 每 Lane 独立 LFSR | 每 Lane 独立 LFSR，种子在 TS 中交换 |
| Equalization | 不需要 (速率低) | **必需要** (CTLE + DFE + 协商 preset) |

### 3.5 推荐博客

| 博客 | 链接 | 说明 |
|---|---|---|
| **dev.to: Understanding PCIe Link Training** | https://dev.to/ripan030/understanding-pcie-link-training-165i | **英文最佳入门** — LTSSM 全状态讲解，配图清晰 |
| **CSDN: LTSSM 过程详解** | https://blog.csdn.net/m0_61208341/article/details/132454143 | 中文，状态转换图 + 时序图 |
| **ProgrammerSought: PCIe Literacy — LTSSM Basics (3 篇)** | https://www.programmersought.com/article/74483813969/ | LTSSM 基础系列三篇 |
| **与非网: 浅析PCIe链路LTSSM状态机** | https://www.e-com-net.com/article/1280009396251803648.htm | 中文企业级教程 |
| **CSDN: PCIe状态机-LTSSM** | https://www.cnblogs.com/yuanqiangfei/p/18195764 | 博客园，中文总结 |
| **CSDN: 《PCI Express体系结构导读》随记 — 数据链路层与物理层** | https://blog.csdn.net/phmatthaus/article/details/141828029 | 王齐书的读书笔记，DLCMSM + LTSSM |
| **CSDN: PCIe硬件设计核心概念解析** | https://blog.csdn.net/tianxiaer359/article/details/148553694 | SerDes 电路、PCB 差分 100Ω、背钻 |

### 3.6 本项目实践

FU740 的 LTSSM 可通过 mgmt 寄存器直接观察：

```bash
# U-Boot 观察 mgmt 寄存器
md 0x100d0000 1       # PERST_N 控制 (bit0=PERST#)
md 0x100d0010 1       # APP_LTSSM_ENABLE (bit0=1 使能)
md 0x100d001C 1       # APP_HOLD_PHY_RST
md 0x100d0708 1       # DEVICE_TYPE (4=RC, 0=EP)

# 观察 PHY debug 寄存器
# PCIE_PORT_DEBUG1 (DBI offset 0x80):
#   bit 4  = link up
#   bit 29 = link in training
md 0xe0000080 1       # DBI + 0x80

# 观察 link 协商结果
md 0xe000007C 1       # LNKCAP — link capability
md 0xe0000082 1       # LNKSTA — link status (speed + width)

# Linux: 通过 dmesg 观察 link 训练日志
dmesg | grep -E "PCIE-|link up|link down|LTSSM|GEN|speed|width"
# FU740 典型输出: "PCIE-0: Link up (Gen1-x8, Bus0)"

# Gen1 quirk 实验:
# 编辑 src/linux/drivers/pci/controller/dwc/pcie-fu740.c
# 在 fu740_pcie_start_link() 中注释强制 Gen1 部分 (line 186-200)
# 观察通过 ASM1042A Switch 连接的设备还能否正常 link up
```

---

## 阶段 4：中断机制 — INTx → MSI → MSI-X

### 4.1 这个阶段要掌握什么

| 知识点 | 说明 |
|---|---|
| **Legacy INTx** | 物理引脚 INTA#~INTD#，电平触发，多设备可共享一根线。已过时但 PCIe 仍兼容 |
| **MSI 原理** | 设备发 Memory Write TLP (Posted) 到 APIC 地址 → CPU 收到中断。**本质是一次 PCIe 写事务** |
| **MSI-X 增强** | 每 Entry 独立 Address+Data、最多 2048 向量、Per-vector mask。Table 在 BAR 空间 |
| **地址/数据格式** | x86: Address=0xFEExx000 + Data=向量的 APIC 格式；ARM64 GICv3 ITS: Address=GITS_TRANSLATER |
| **中断重映射** | IOMMU (VT-d/SMMU) 拦截 MSI 写 → 查 IRTE 表 → 转投到正确 CPU |
| **内核 API** | `pci_alloc_irq_vectors()` 统一接口，自动 MSI-X → MSI → INTx 回退 |

### 4.2 MSI/MSI-X 的完整硬件流程

```text
1. 操作系统枚举设备时:
   - 读取 MSI-X Capability → Table Size (N 个 Entry)
   - 为每个 Entry 分配 Address (APIC/GIC 地址) 和 Data (向量号)
   - 写入 MSI-X Table (在设备 BAR 空间中)
   - 设置 MSI-X Enable bit

2. 设备需要中断时:
   - 从 MSI-X Table 读取 entry[N] 的 (msg_addr, msg_data)
   - 发起 Memory Write TLP (Posted) → 目标地址 = msg_addr, 数据 = msg_data
   - 这是一个标准 PCIe MWr TLP!

3. 平台层接收:
   x86: MWr 到 0xFEEXXXXX → LAPIC 识别 → 中断注入 CPU
   ARM64: MWr 到 GITS_TRANSLATER → GICv3 ITS 解析 DeviceID+EventID → LPI 注入

4. 若启用 IOMMU 中断重映射:
   IOMMU 拦截 MWr → 计算 interrupt_index → 查 IRTE → 获取真实 (dest_cpu, vector)

5. CPU 收到中断 → IDT/GIC 查表 → 调用驱动 ISR
   驱动 ISR 读设备寄存器清中断状态 → 处理数据 → 返回
```

### RISC-V 平台的 MSI 中断路径

HiFive Unmatched 使用 RISC-V PLIC 处理 MSI（**注意：** RISC-V PLIC 不原生支持
MSI，FU740 的 DWC 控制器内部集成了一个 MSI 控制器作为桥接）：

```text
设备发 MWr TLP (带 MSI Data)
  → DWC 内部 MSI 控制器接收
    → 解析 MSI Data 低 5 bit → MSI 向量号 (0~31)
    → 将向量号映射到 PLIC 中断线
      → PLIC 中断源 IRQ 56 (PCIe MSI 专用)
        → PLIC 将中断路由到目标 hart
          → RISC-V 外部中断 → S-mode trap → Linux IRQ handler
```

**FU740 PLIC 中断分配：**
- **IRQ 42**: UART0 串口
- **IRQ 56**: PCIe MSI 汇总中断线
- PLIC 共 69 个中断源 (DTS: `riscv,ndev = <69>`)

**与 x86/ARM 的关键区别：** RISC-V PLIC 没有 ITS (Interrupt Translation Service)
这样的 MSI 地址拦截机制。DWC 的 MSI 控制器在 RC 内部捕获 MSI TLP，
转换为传统的中断线信号，再送入 PLIC。

### 三种机制对比

| 特性 | Legacy INTx | MSI | MSI-X |
|---|---|---|---|
| 信号方式 | 物理引脚电平 | Memory Write TLP | Memory Write TLP |
| 最多向量 | 4 (共享) | 32 (必须 2 的幂) | 2048 (独立) |
| Table 位置 | N/A | Capability 寄存器内 | **BAR 空间中** |
| Per-vector mask | 不支持 | 可选 (Mask/Pending bits) | **原生支持** |
| CPU 亲和性 | 共享，难定向 | 同设备共 CPU 组 | **每向量独立绑定** |
| 数据一致性 | 可能与数据乱序 | PCIe 写不能被数据超越 | 同左 |

### 4.4 推荐博客与资料

| 博客 | 链接 | 说明 |
|---|---|---|
| **Linux Kernel MSI-HOWTO (官方)** | https://origin.kernel.org/doc/html/v6.19/PCI/msi-howto.html | **权威文档** — 用哪个 API、何时申请、怎样回退 |
| **Stony Brook: PCIe Interrupt Delivery 课件** | https://www3.cs.stonybrook.edu/~live3/files/pcie-interrupt-delivery.pdf | MSI/MSI-X 硬件投递流程的学术级讲解 |
| **AWS FPGA: MSI-X Implementation Guide** | https://awsdocs-fpga-f2.readthedocs-hosted.com/latest/sdk/apps/msix-interrupts/README.html | 工业级 MSI-X 实现参考：Table 布局、ConfigFS 配置 |
| **CSDN: PCIe MSI-X 机制实例** | https://blog.csdn.net/u011011827/article/details/130450767 | 中文，MSI-X Table Entry 结构 + 内核配置代码 |
| **ProgrammerSought: MSI/MSI-X code analysis** | https://www.programmersought.com/article/78017399476/ | 内核代码级分析：`pci_enable_msix()` → `msix_capability_init()` |

### 4.5 本项目实践

```bash
# 观察 Unmatched 上的中断分配
cat /proc/interrupts | grep -iE 'pci|nvme|msi'

# 查看 MSI/MSI-X 能力
lspci -v -s 01:00.0 | grep -A 5 "MSI"
# 输出示例:
#   Capabilities: [60] MSI-X: Enable+ Count=33 Masked-
#   Capabilities: [70] Express Endpoint, MSI 00

# 查看分配的 MSI-X 向量
ls /sys/bus/pci/devices/0000:01:00.0/msi_irqs/
cat /sys/bus/pci/devices/0000:01:00.0/msi_irqs/*

# NVMe 驱动使用 MSI-X 的每个 IO 队列有独立中断向量
nvme list
cat /proc/interrupts | grep nvme
```

---

## 阶段 5：Linux PCIe 子系统与 DWC 驱动

### 5.1 这个阶段要掌握什么

| 知识点 | 说明 |
|---|---|
| **PCI 子系统分层** | host bridge 驱动 → PCI 核心 (probe/setup-bus) → endpoint 驱动 |
| **DWC 控制器架构** | 三层：SiFive shim (pcie-fu740.c) → DWC 核心 (pcie-designware.c) → Linux PCI 核心 |
| **枚举代码路径** | `pci_host_probe()` → `pci_scan_child_bus()` → `pci_scan_slot()` → `pci_scan_device()` |
| **BAR 分配流程** | `__pci_read_base()` 读写全1 → `pci_assign_unassigned_resources()` → bridge window 编程 |
| **iATU 编程** | DWC 特有：outbound window (CPU→PCIe)、inbound window (PCIe→CPU) |
| **驱动模型** | `pci_register_driver()` → probe → enable_device → set_master → request BAR → request_irq |
| **设备树绑定** | `snps,dw-pcie.yaml` 定义 reg-names (dbi/config/atu/dma) 的标准含义 |

### 5.2 内核 PCI 枚举核心源码导读

以下是 `drivers/pci/probe.c` 中的关键函数链：

```text
pci_host_probe(bridge)                           ← DWC 驱动调用
  └─ pci_scan_root_bus_bridge(bridge)
       ├─ pci_register_host_bridge()              ← 注册 host bridge 结构
       └─ pci_scan_child_bus(bus)                 ← ⬅ 枚举核心
            └─ for devfn = 0 to 0xFF step 8:
                 pci_scan_slot(bus, devfn)
                   └─ pci_scan_single_device()
                        └─ pci_scan_device()
                             ├─ pci_bus_read_dev_vendor_id()
                             │    读 Vendor ID — 0xFFFF = 设备不存在
                             ├─ pci_alloc_dev()           ← 分配 pci_dev
                             └─ pci_setup_device()        ← ⬅ 读完整配置头
                                  ├─ 读 Header Type       → Type 0 vs Type 1
                                  ├─ __pci_read_base()     ← ⬅ BAR sizing
                                  │    ┌ 写 0xFFFFFFFF 到 BAR
                                  │    ├ 读回 → 低位 0 的个数 = BAR size
                                  │    └ 恢复原始值
                                  ├─ pci_read_irq()        ← 读 IRQ 引脚
                                  └─ pci_read_capabilities() ← 遍历 Capability List

       └─ pci_assign_unassigned_bus_resources(bus)       ← 资源分配
            ├─ __pci_bus_size_bridges()    ← 计算需求
            ├─ __pci_bus_assign_resources() ← 分配基址
            └─ pci_setup_bridge()          ← 写 Type 1 头的 Base/Limit 寄存器

       └─ pci_bus_add_devices(bus)                        ← 注册到设备模型
            └─ device_attach() → nvme_probe() 等 EP 驱动绑定
```

**BAR sizing 算法（`__pci_read_base` 的核心逻辑）：**

```c
// 简化伪代码
pci_read_config_dword(dev, bar_offset, &orig);  // 保存原始值
pci_write_config_dword(dev, bar_offset, ~0);     // 写全 1
pci_read_config_dword(dev, bar_offset, &mask);   // 读回

// mask 中: bit 0=MEM/IO 类型, bits[3:1]=prefetchable/type
// size = 1 << __builtin_ctz(~(mask & ~0xf))
// 例: mask=0xFFF00004 → size = 1<<12 = 4KB

pci_write_config_dword(dev, bar_offset, orig);   // 恢复
```

### 5.3 DWC 控制器驱动架构

```text
drivers/pci/controller/dwc/
  pcie-designware.h          ← 967 行: 所有寄存器定义 + struct dw_pcie/dw_pcie_rp
  pcie-designware.c          ← ~800 行: dw_pcie_setup_rc(), dw_pcie_wait_for_link()
  pcie-designware-host.c     ← 1225 行: dw_pcie_host_init(), MSI 控制器
  pcie-fu740.c               ← 357 行: FU740 shim (本项目的核心)

调用链:
  fu740_pcie_probe()                              ← platform_driver probe
    └─ dw_pcie_host_init(&pci->pp)                ← DWC 通用入口
         ├─ dw_pcie_host_get_resources()          ← 从 DT 获取 dbi/config/mgmt 基址
         ├─ fu740_pcie_host_init()                ← .host_init 回调
         │    ├─ GPIO: PERST# assert → 100ms delay → deassert
         │    ├─ 使能 pcie_aux 时钟
         │    ├─ fu740_pcie_init_phy()            ← CR_PARA AC 终端配置（8 lanes）
         │    └─ 释放 PRCI 复位
         ├─ dw_pcie_msi_host_init()               ← MSI 控制器初始化
         ├─ dw_pcie_setup_rc()                    ← 设置 Root Complex
         │    ├─ 设置 device type = RC (DBI offset)
         │    └─ 配置 class code
         ├─ fu740_pcie_start_link()               ← .start_link 回调
         │    ├─ 强制 Gen1 (写 LNKCAP 的 max link speed = 1)
         │    ├─ 使能 LTSSM (mgmt_base + 0x10 |= 1)
         │    ├─ dw_pcie_wait_for_link()          ← 轮询 PCIT_PORT_DEBUG1
         │    └─ 恢复原生速度 → 发起 speed change → 再等 link
         └─ pci_host_probe(bridge)                ← 触发枚举
              → pci_scan_child_bus() → NVMe/AHCI/GPU 驱动绑定
```

### 5.4 本项目的独特价值

本项目包含 U-Boot 和 Linux **两侧**的 FU740 DWC PCIe 驱动，对比阅读是理解 DWC 控制器的最佳方式：

| 层面 | 文件 | 行数 | 学习重点 |
|---|---|---|---|
| U-Boot | `src/u-boot/drivers/pci/pcie_dw_sifive.c` | 506 | bare-metal 风格：PHY 直接寄存器编程、Gen1 永久强制、单 ATU window |
| U-Boot | `src/u-boot/drivers/pci/pcie_dw_common.c` | ~200 | iATU outbound 编程、config space read/write |
| Linux | `src/linux/drivers/pci/controller/dwc/pcie-fu740.c` | 357 | 平台驱动 model：.host_init/.start_link 回调、二阶速度协商 |
| Linux | `src/linux/drivers/pci/controller/dwc/pcie-designware.h` | 967 | 完整寄存器定义 + 核心数据结构 (`struct dw_pcie`, `dw_pcie_rp`) |
| Linux | `src/linux/drivers/pci/controller/dwc/pcie-designware.c` | ~800 | `dw_pcie_setup_rc()` + link-up 轮询逻辑 |
| Linux | `src/linux/drivers/pci/controller/dwc/pcie-designware-host.c` | 1225 | `dw_pcie_host_init()` 总控 + MSI 中断控制器 |

**U-Boot vs Linux 对比阅读要点：**

| 方面 | U-Boot | Linux |
|---|---|---|
| PHY 初始化 | `pcie_sifive_init_phy()` 直接写 CR_PARA | `fu740_pcie_init_phy()` 同逻辑，通过 mgmt reg ops |
| Gen1 策略 | 永久强制 Gen1 | Gen1 启动 → 恢复原生速度 → 请求 speed change |
| iATU | 单个 outbound MEM window (window 0) | 多个动态管理 window |
| MSI | 最小支持 | 完整 MSI + MSI-X IRQ domain |
| 枚举 | 独立实现 (`pci_auto.c`) | Linux PCI 核心标准枚举 |
| DT 结构 | `pcie_sifive_of_to_plat()` 直接读 ofnode | `dw_pcie_host_get_resources()` 读 DT reg-names |

### 5.5 推荐博客与资料

**内核文档与源码：**

| 资源 | 链接 | 说明 |
|---|---|---|
| **Linux PCI Subsystem Docs (v6.19)** | https://origin.kernel.org/doc/html/v6.19/PCI/index.html | 内核官方 PCI 文档入口 |
| **How To Write Linux PCI Drivers** | https://www.kernel.org/doc/Documentation/PCI/pci.rst | probe/remove/enable 标准模板 |
| **LDD3 Chapter 12: PCI Drivers** | https://lwn.net/Kernel/LDD3/ | 经典教材，虽然旧但框架没变 |
| **DWC Device Tree Binding** | https://android.git.googlesource.com/kernel/common/+/refs/tags/android15-6.6-2025-04_r3/Documentation/devicetree/bindings/pci/snps,dw-pcie.yaml | reg-names (dbi/dbi2/config/atu/dma) 的标准定义 |
| **DeepWiki: PCI/PCIe Core Enumeration** | https://deepwiki.com/torvalds/linux/11.2-pcipcie:-core-enumeration-resource-assignment-and-quirks | `pci_scan_child_bus` → `__pci_read_base()` → Quirks 的 AI 生成摘要 |

**深度分析博客：**

| 博客 | 链接 | 说明 |
|---|---|---|
| **CSDN: PCI/PCIe子系统万字总结** | https://blog.csdn.net/qq_63718344/article/details/155729400 | 驱动框架 + 配置空间 + 设备树 + 枚举流程，新手友好 |
| **腾讯云: Linux 与 PCIe — 深入理解 PCIe 在内核中的实现** | https://cloud.tencent.cn/developer/article/2669020 | 配置空间解析 → 资源分配 → MSI-X → DMA 的全生命周期 |
| **ProgrammerSought: PCIe 初始化枚举与资源分配过程分析** | https://www.programmersought.com/article/25197399484/ | ACPI 命名空间 → `acpi_pci_root_add()` → `pci_scan_child_bus()` 的源码追踪 |
| **ProgrammerSought: PCIe device discovery process** | https://www.programmersought.com/article/26138889443/ | `pci_scan_device()` 的逐行源码分析 |
| **ProgrammerSought: PCIe study notes (1)** | https://www.programmersought.com/article/80523786433/ | `pci_scan_root_bus()` 三步：create → scan → assign |

---

## 阶段 6：动手实践 — 全部在 HiFive Unmatched 上

所有实验都使用本项目的构建体系和 Unmatched 物理板（或 QEMU 虚拟机）。

### 6.1 必经实验（按学习阶段顺序做）

**实验 1：用 U-Boot 直接观察配置空间（配合阶段 1）**

```bash
# 物理板或 QEMU — U-Boot 命令行
pci enum
pci 0                          # 显示 BDF + Vendor/Device ID + BAR
pci header 0.0.0               # RC Root Port 的 Type 1 Header
pci header 1.0.0               # 下游第一个设备的 Type 0/1 Header

# 直接读 ECAM 验证 Type 0/1 Header 各字段
md 0xd00000000 64              # BDF=0:0.0 的 256B 配置空间
# 对比本文档 §1.2 的 Type 1 Header 布局:
#   +0x00  Vendor/Device ID
#   +0x18  Primary/Secondary/Subordinate Bus Number (只有 Bridge 有)
#   +0x1C  IO Base/Limit
#   +0x20  Memory Base/Limit

# 读 Capabilities Pointer (offset 0x34)
md 0xd00000034 1               # bit[7:0] = Capability 链表头
```

**实验 2：Linux 侧完整 dump 配置空间（配合阶段 1）**

```bash
# === 设备树：理解 reg-names 对应的物理地址 ===
ls -la /sys/firmware/fdt        # 运行时 DTB
dtc -I dtb -O dts /sys/firmware/fdt | grep -A 30 'pcie@e00000000'
# 观察 dbi = 0xe00000000, config = 0xdf0000000, mgmt = 0x100d0000

# === 每个设备的完整配置空间 ===
lspci -vvv -xxx | head -100      # 前 4KB 十六进制 dump
for dev in /sys/bus/pci/devices/*/config; do
  echo "=== $dev ===" && hexdump -C "$dev" | head -4
done

# === 手动 BAR sizing（对照内核 __pci_read_base 逻辑） ===
# 读 BAR0 原始值
setpci -s 01:00.0 0x10.L        # 保存这个值
# 写全 1 → 读回 → 计算 size
setpci -s 01:00.0 0x10.L=0xFFFFFFFF
setpci -s 01:00.0 0x10.L        # 记录：bit0=MEM/IO, 低位连续0的个数=2^size
# 恢复原始值
setpci -s 01:00.0 0x10.L=<原始值>

# 对比内核帮我们算好的结果
cat /sys/bus/pci/devices/0000:01:00.0/resource
```

**实验 3：TLP 观察 — ftrace + dynamic debug（配合阶段 2）**

```bash
# 启用 ftrace 追踪所有 pci_ 开头的内核函数
mount -t tracefs none /sys/kernel/debug/tracing 2>/dev/null || true
echo function > /sys/kernel/debug/tracing/current_tracer
echo 'pci*' > /sys/kernel/debug/tracing/set_ftrace_filter
echo 1 > /sys/kernel/debug/tracing/tracing_on

# 触发一次 PCIe 活动（读 NVMe 设备配置空间）
cat /sys/bus/pci/devices/0000:01:00.0/vendor > /dev/null

# 查看 TLP 级调用
cat /sys/kernel/debug/tracing/trace | tail -50
echo 0 > /sys/kernel/debug/tracing/tracing_on

# dynamic debug：追踪 DWC 层的 iATU 编程和 TLP 路由
echo 'file pcie-designware*.c +p' > /sys/kernel/debug/dynamic_debug/control
echo 'file pcie-fu740.c +pflmt' > /sys/kernel/debug/dynamic_debug/control
dmesg -wH &
# 然后插拔设备或重新枚举
echo 1 > /sys/bus/pci/rescan
# 观察 dmesg 中的 iATU outbound/inbound 编程日志
```

**实验 4：LTSSM 链路训练追踪（配合阶段 3）**

```bash
# === U-Boot 侧：DWC 内部寄存器 ===
md 0x100d0000 1       # PERST_N
md 0x100d0010 1       # APP_LTSSM_ENABLE
md 0x100d001C 1       # APP_HOLD_PHY_RST
md 0x100d0708 1       # DEVICE_TYPE (期望: 0x4 = RC)
md 0xe0000080 1       # PCIE_PORT_DEBUG1: [4]=link_up, [29]=training
md 0xe000007C 1       # LNKCAP: 查看支持的最高速率
md 0xe0000082 1       # LNKSTA: 当前协商的速率和宽度

# === Linux 侧：启动后确认链路 ===
dmesg | grep -E "PCIE-|link up|link down|speed|width|GEN|LTSSM"
# FU740 典型输出: "PCIE-0: Link up (Gen1-x8, Bus0)"

lspci -vvv -s 00:00.0 | grep -E "LnkCap|LnkSta|LnkCtl"
# LnkCap: 支持的速率和宽度
# LnkSta: 当前协商结果 (如 Speed 2.5GT/s, Width x8)
```

**实验 5：MSI-X 中断路径分析（配合阶段 4）**

```bash
# === 观察 NVMe 驱动的 MSI-X 分配 ===
cat /proc/interrupts | grep nvme
# 每一行：IO 队列的中断向量 + 在哪个 CPU 上处理 + 累积计数

ls /sys/bus/pci/devices/0000:01:00.0/msi_irqs/
cat /sys/bus/pci/devices/0000:01:00.0/msi_irqs/*

# === 触发中断后观察计数变化 ===
dd if=/dev/nvme0n1 of=/dev/null bs=4K count=1000 iflag=direct 2>&1 &
cat /proc/interrupts | grep nvme
# 重复 cat 几次，观察中断计数增长

# === 查看 MSI-X Table 的 BAR 映射 ===
lspci -vvv -s 01:00.0 | grep -A 2 "MSI-X"
# 输出示例: MSI-X: Enable+ Count=33 Masked-
#           Vector table: BAR=0 offset=00002000
#           PBA: BAR=0 offset=00003000
cat /sys/bus/pci/devices/0000:01:00.0/resource
# BAR0 的 start-end 对应 MSI-X Table 所在的物理内存范围
```

**实验 6：对比 U-Boot 和 Linux 的 PCIe 初始化（配合阶段 5）**

```bash
# 在 U-Boot 中观察 bare-metal 风格的初始化
pci regions                    # iATU outbound window 配置
md 0xe000070C 1                # iATU region 0: lower target address
md 0xe0000710 1                # iATU region 0: upper target address
md 0xe0000700 1                # iATU region 0: control (type=cfg0/mem)

# 在 Linux 中对比 DWC 驱动做了哪些不同的事
grep -n "atu\|ATU\|outbound\|inbound\|iATU" \
  src/linux/drivers/pci/controller/dwc/pcie-designware.c
# 对照 U-Boot 的 pcie_dw_common.c: 两者 iATU 编程的差异在哪?
```

### 6.2 深入实验（Unmatched 物理板 / QEMU）

| 序号 | 实验 | 操作步骤 | 学习点 |
|---|---|---|---|
| **7** | **Gen1 quirk 实验** | `./build.sh dev-linux` → 编辑 `src/linux/drivers/pci/controller/dwc/pcie-fu740.c` 中 `fu740_pcie_start_link()` 的 Gen1 强制代码 → 重建 → 观察 | 理解 Gen1 初始训练的必要性；观察 ASM1042A Switch 下设备还能否 link up |
| **8** | **手动触发 PCIe 重枚举** | `echo 1 > /sys/bus/pci/devices/0000:00:00.0/remove` → `echo 1 > /sys/bus/pci/rescan` | 理解热插拔流程；对比前后 `lspci` 和 `iomem` |
| **9** | **NVMe 全流程** | `nvme id-ctrl /dev/nvme0` → `nvme smart-log /dev/nvme0` → `dd` 测速 | 验证从 MSI-X 中断到块设备 IO 的完整数据通路 |
| **10** | **追踪一次 MRd/CplD 往返** | 用 `dd` 读 NVMe → 同时 `dmesg -wH` 或 ftrace | 理解 Non-Posted Read 请求和 Completion TLP 的匹配 |
| **11** | **lspci 深入解析** | `lspci -vvv -s 01:00.0` 逐行解释输出 | 对照本文档 §1.2 的 Header 布局，验证每个 Capability |
| **12** | **QEMU 对比实验** | `./build.sh qemu && ./qemu.sh` → 对比物理板和 QEMU 的 `lspci -t` 和 `iomem` | 理解 DWC vs GPEX host bridge 的差异 |
| **13** | **DTB 反编译分析** | `dtc -I dtb -O dts deploy/hifive-unmatched-a00.dtb | grep -A 50 'pcie@e00000000'` | 理解 `ranges` 地址转换、`interrupt-map` MSI 路由 |
| **14** | **内核配置实验** | `cd out/linux && make -C ../../src/linux O=$PWD ARCH=riscv menuconfig` → 禁用/启用 PCIe 选项 | 理解 `CONFIG_PCIE_FU740`/`CONFIG_PCIE_DW` 等选项的作用 |
| **15** | **SPL 阶段 PCIe 观察**（仅物理板） | 在 `spl_board_init_f()` 后设断点（JTAG）或加 early print | 观察 PCIe Switch 的 GPIO 复位时序 |
| **16** | **AER 日志分析** | `dmesg \| grep -iE "AER\|correct\|uncorrect"` → `lspci -vvv \| grep -A 20 AER` | 观察链路错误记录 |

**每个实验后导出观察结果：**

```bash
# 保存快照，方便对照学习
{ echo "=== $(date) ==="; lspci -vvv -t; echo; lspci -vvv -xxx -s 01:00.0; \
  echo; cat /proc/iomem | grep pci; echo; cat /proc/interrupts | grep -iE 'pci|nvme'; \
} > pcie-snapshot-$(date +%Y%m%d-%H%M%S).log

# dev-linux 实验后导出 patch
git -C src/linux diff -- drivers/pci/controller/dwc/ > patches/linux/0002-pcie-experiment.patch
```

### 6.3 QEMU 作为安全实验平台

物理板上的某些实验（移除 Root Port、破坏性配置写入）有风险。
QEMU 是安全的替代：

```bash
# QEMU 上做 BAR sizing 实验（写全 1 无风险）
./build.sh qemu
./qemu.sh --build
# 在 QEMU 的 Linux 里:
setpci -s 00:01.0 0x10.L=0xFFFFFFFF
setpci -s 00:01.0 0x10.L    # 观察 size 回读结果

# QEMU 带 NVMe 设备 — 做中断/IO 实验
qemu-system-riscv64 -M virt -smp 8 -m 2G \
  -bios deploy/qemu/fw_dynamic.elf \
  -kernel deploy/qemu/u-boot.bin \
  -drive file=deploy/qemu/qemu-lite.img,if=none,format=raw,id=rootdisk \
  -device virtio-blk-device,drive=rootdisk \
  -drive file=nvme-test.img,if=none,id=nvme0 \
  -device nvme,serial=test,drive=nvme0
```

**QEMU virt 与物理板的 PCIe 对比：**

| 方面 | Unmatched 物理板 | QEMU virt |
|---|---|---|
| PCIe 控制器 | FU740 DWC | GPEX (pci-host-generic) |
| Host bridge 驱动 | `pcie-fu740.c` | `pcie-host-generic.c` |
| iATU | 需要手动编程 | 不需要 (纯 ECAM) |
| PHY/LTSSM | 可观察 mgmt 寄存器 | 无 (虚拟化) |
| 枚举结果 | RC → ASM1042A Switch → EP | virtio-blk + nvme 直连 |
| 适合实验 | 配置空间/BAR/中断/Link | 配置空间/BAR/中断 (协议层一致) |

### 6.4 12 步最小 PCIe RC 初始化序列（从本项目驱动提炼，供源码阅读对照）

对照 `pcie-fu740.c` 和 `pcie_dw_sifive.c` 阅读：

```text
1.  PCIe 电源使能 (GPIO 5 = PCIE_PWREN)
    对应: fu740_pcie_host_init() → gpiod_set_value(pwren, 1)
2.  使能 pcie_aux 时钟 (PRCI)
    对应: clk_prepare_enable(pcie_aux)
3.  复位 ASM1042A Switch (GPIO 7 = UBRDG_RSTN)
    对应: U-Boot SPL 的 spl_usb_pcie_bridge_init()
    注意: 物理板独有，QEMU 无此步骤
4.  释放 PCIe 控制器复位 (reset_control_deassert)
    对应: fu740_pcie_host_init() → reset_control_deassert(rst)
5.  设置 DBI RoW enable (MISC_CONTROL_1_OFF bit 0)
    对应: pcie_sifive_force_gen1() 和 Linux 侧的 DBI 操作
6.  PHY lane 初始化 (CR_PARA: AC 终端)
    对应: fu740_pcie_init_phy() → 8 个 lane 依次 CR_PARA_WR
7.  设置 device type = RC
    对应: writel(0x4, mgmt_base + PCIEX8MGMT_DEVICE_TYPE)
8.  编程 iATU outbound window (cfg0 type → ECAM)
    对应: dw_pcie_prog_outbound_atu() / pcie_dw_prog_outbound_atu_unroll()
9.  强制 Gen1 (LNKCAP max speed = 1)
    对应: fu740_pcie_start_link() 中的 Gen1 强制逻辑
10. 使能 LTSSM
    对应: writel(0x1, mgmt_base + PCIEX8MGMT_APP_LTSSM_ENABLE)
11. 等待 link up (轮询 PORT_DEBUG1 bit4=1, bit29=0)
    对应: dw_pcie_wait_for_link() → 循环读 dw_pcie_link_up()
12. ECAM 枚举: for B=0..255, D=0..31, F=0..7: 读 Vendor ID
    对应: pci_scan_child_bus() → pci_scan_device() → pci_bus_read_dev_vendor_id()
```

**练习：** 在源码中找到每一步对应的精确行号，写出调用栈。

### 6.5 修改源码追踪每一步 — dev-linux / dirty-src 开发模式

本项目的构建系统支持两种源码修改模式，让你可以在 PCIe 初始化的每一步
添加调试输出、修改逻辑、观察行为变化。

#### 6.5.1 两种开发模式对比

| | `./build.sh dev-linux` | 直接改 `src/linux/` |
|---|---|---|
| **触发方式** | `./build.sh dev-linux` | 手动编辑 + 手动 make |
| **源码 reset?** | 首次 fetch+patch+defconfig，之后不 reset/clean | 需要自己管理 |
| **.config 覆盖?** | 不覆盖 | 自己管理 |
| **增量编译?** | 是 | 是 |
| **适合场景** | 临时实验、加 printk、改逻辑后快速验证 | 深度开发、多个 patch 叠加 |
| **导出结果** | `git -C src/linux diff` → patch | `git -C src/linux format-patch` |

**推荐策略：** 先用 `dev-linux` 做临时实验，验证想法后导出为 patch 放到 `patches/linux/`，
然后用 `./build.sh linux` 验证 patch 的可复现性。

#### 6.5.2 dev-linux 工作流：追踪 Linux PCIe probe 每一步

以下是一个完整的实验流程，在 `pcie-fu740.c` 的每个关键函数入口添加 `pr_info()`：

**步骤 1：首次构建 dev-linux**

```bash
# 首次调用会 fetch Linux 源码 + 应用已有 patches + 生成 .config
# 与普通 linux target 的区别：之后不会 git reset --hard / git clean
./build.sh dev-linux
```

**步骤 2：在源码中添加追踪点**

```bash
# 编辑 pcie-fu740.c，在关键函数入口添加 pr_info()
vim src/linux/drivers/pci/controller/dwc/pcie-fu740.c
```

以下是可以添加的追踪点——每个对应 PCIe 初始化的一步：

```c
// ===== fu740_pcie_probe() — 入口 =====
static int fu740_pcie_probe(struct platform_device *pdev)
{
    pr_info("=== [PCIe Step 1] fu740_pcie_probe: platform driver matched ===\n");

    // ... 原有代码: 获取 dbi_base, mgmt_base, gpios, clocks, reset ...

    pr_info("=== [PCIe Step 2] dbi=0x%px, mgmt=0x%px ===\n", pci->dbi_base, mgmt_base);
    pr_info("=== [PCIe Step 3] GPIOs: reset=%d, pwren=%d ===\n",
            gpiod_get_value(pcie_reset), gpiod_get_value(pcie_pwren));
    // ... 继续原有代码 ...
}

// ===== fu740_pcie_host_init() — host 初始化回调 =====
static int fu740_pcie_host_init(struct dw_pcie_rp *pp)
{
    pr_info("=== [PCIe Step 4] fu740_pcie_host_init: deassert PERST ===\n");

    // ... 原有代码: GPIO PERST assert → delay 100ms → deassert ...

    pr_info("=== [PCIe Step 5] pcie_aux clock enabled ===\n");
    // ... clk_prepare_enable(pcie_aux) 之后 ...

    pr_info("=== [PCIe Step 6] fu740_pcie_init_phy: programming 8 lanes ===\n");
    // ... fu740_pcie_init_phy() 调用前后 ...
}

// ===== fu740_pcie_init_phy() — PHY 初始化 =====
static void fu740_pcie_init_phy(struct dw_pcie *pci)
{
    pr_info("=== [PCIe Step 6a] PHY lane AC termination config start ===\n");
    for (int i = 0; i < 8; i++) {
        pr_info("=== [PCIe Step 6b] programming lane %d ===\n", i);
        // ... CR_PARA_ADDR, CR_PARA_WR_DATA, CR_PARA_WR_EN ...
    }
    pr_info("=== [PCIe Step 6c] PHY init done ===\n");
}

// ===== fu740_pcie_start_link() — 链路训练启动 =====
static int fu740_pcie_start_link(struct dw_pcie *pci)
{
    pr_info("=== [PCIe Step 7] set device type = RC (0x4) ===\n");
    // ... writel(0x4, mgmt + PCIEX8MGMT_DEVICE_TYPE) ...

    pr_info("=== [PCIe Step 8] iATU outbound window programming ===\n");
    // ... dw_pcie_prog_outbound_atu() 调用 ...

    pr_info("=== [PCIe Step 9] force Gen1: writing LNKCAP max speed = 1 ===\n");
    // ... 读 LNKCAP → 改 max speed → 写回 ...

    pr_info("=== [PCIe Step 10] enabling LTSSM ===\n");
    // ... writel(0x1, mgmt + PCIEX8MGMT_APP_LTSSM_ENABLE) ...

    pr_info("=== [PCIe Step 11] waiting for link up (polling PORT_DEBUG1) ===\n");
    // ... dw_pcie_wait_for_link() ...
    // 可以在循环中加 pr_info 打印每次轮询的 PORT_DEBUG1 值:
    //   val = readl(pci->dbi_base + PCIE_PORT_DEBUG1);
    //   pr_info("  PORT_DEBUG1=0x%08x [link_up=%d training=%d]\n",
    //           val, !!(val & BIT(4)), !!(val & BIT(29)));

    pr_info("=== [PCIe Step 12] link up! ===\n");
}

// ===== dw_pcie_host_init() 完成后，触发枚举 =====
// 在 fu740_pcie_probe() 最后:
    pr_info("=== [PCIe Step 13] calling dw_pcie_host_init() → pci_host_probe() → enum start ===\n");
```

**步骤 3：增量重建**

```bash
# dev-linux 模式下，只重编译修改过的文件，不 reset 源码
./build.sh dev-linux
```

**步骤 4：部署到 SD 卡**

```bash
# 物理板：替换 boot 分区中的 Image.gz 和 DTB
# （假设 SD 卡 mount 在 /mnt/sdcard）
sudo mount /dev/sdX3 /mnt/sdcard
sudo cp deploy/Image.gz /mnt/sdcard/
sudo cp deploy/hifive-unmatched-a00.dtb /mnt/sdcard/
sudo umount /mnt/sdcard

# 或者直接用新构建的整个镜像刷写 SD 卡 (注意选择正确设备!)
# sudo dd if=deploy/unmatched-lite.img of=/dev/sdX bs=4M status=progress
```

**步骤 5：在板上观察输出**

```bash
# 物理板串口控制台
# 启动后在 dmesg 中查看所有 [PCIe Step N] 标记
dmesg | grep "\[PCIe Step"
# 输出示例:
# [PCIe Step 1] fu740_pcie_probe: platform driver matched
# [PCIe Step 2] dbi=0x..., mgmt=0x...
# [PCIe Step 6a] PHY lane AC termination config start
# [PCIe Step 6b] programming lane 0
# ...
# [PCIe Step 12] link up!
# [PCIe Step 13] calling pci_host_probe() → enum start
```

#### 6.5.3 U-Boot 侧源码修改：追踪 bare-metal PCIe 初始化

U-Boot 侧的改法与 Linux 不同——U-Boot 没有 `dev-linux` 模式，但你可以
直接在 `src/u-boot/` 中修改源码然后重编：

```bash
# U-Boot 构建
./build.sh u-boot
# 或直接调 litebuild.py（不改 meson 配置）
```

**U-Boot 中添加追踪点：**

```c
// src/u-boot/drivers/pci/pcie_dw_sifive.c

static int pcie_sifive_probe(struct udevice *dev)
{
    printf("=== [U-Boot PCIe Step 1] pcie_sifive_probe ===\n");

    // 打印 DT 获取的资源
    printf("  dbi_base  = 0x%llx\n", (u64)pci->dbi_base);
    printf("  mgmt_base = 0x%llx\n", (u64)sifive->mgmt_base);
    printf("  config    = 0x%llx\n", (u64)pci->cfg_base);
}

static void pcie_sifive_init_phy(struct sifive_pcie *priv)
{
    printf("=== [U-Boot PCIe Step 6] init_phy: programming CR_PARA ===\n");
    // 对每个 lane 打印 CR_PARA 配置:
    for (int lane = 0; lane < 8; lane++) {
        printf("  lane %d: addr=0x%x data=0x%x\n", lane, addr, data);
        // ... CR_PARA 写操作 ...
    }
}

static int pcie_sifive_init_port(struct udevice *dev)
{
    printf("=== [U-Boot PCIe Step 4-5] assert PERST + power on ===\n");
    // ... GPIO assert ...
    printf("=== [U-Boot PCIe Step 6] calling init_phy ===\n");
    // ... pcie_sifive_init_phy() ...
    printf("=== [U-Boot PCIe Step 7] device type = RC ===\n");
    // ...
    printf("=== [U-Boot PCIe Step 9-10] force Gen1 + enable LTSSM ===\n");
    // ...
    printf("=== [U-Boot PCIe Step 11] waiting for link... ===\n");
    // 在等待循环内:
    while (timeout--) {
        u32 val = readl(dbi + PCIE_PORT_DEBUG1);
        printf("  retry %d: PORT_DEBUG1=0x%08x [up=%d train=%d]\n",
               100 - timeout, val, !!(val & BIT(4)), !!(val & BIT(29)));
        if ((val & BIT(4)) && !(val & BIT(29))) break;
        mdelay(10);
    }
    printf("=== [U-Boot PCIe Step 12] link up! speed=%s, width=x%d ===\n",
           speed_str, width);
}
```

**U-Boot 重建与部署：**

```bash
# 重建
./build.sh u-boot

# 部署 u-boot.itb 到 SD 卡 (替换 raw 分区 p2 的内容)
# 注意: SPL+U-Boot ITB 位于 SD 卡的 raw GPT 分区，不是 FAT 文件系统中的文件
sudo dd if=deploy/u-boot-spl.bin of=/dev/sdX1 bs=4M
sudo dd if=deploy/u-boot.itb of=/dev/sdX2 bs=4M
sync

# 也可以重建整个 GPT 镜像后完整烧写
./build.sh                         # 构建完整的 unmatched-lite.img
sudo dd if=deploy/unmatched-lite.img of=/dev/sdX bs=4M status=progress
```

#### 6.5.4 实验想法：在每个步骤做破坏性修改

| 修改内容 | 预期现象 | 学习点 |
|---|---|---|
| 在 `fu740_pcie_host_init()` 中注释掉 `reset_control_deassert()` | 控制器不复位，后续 LTSSM 可能卡住 | 理解 PRCI 复位的必要性 |
| 在 `fu740_pcie_init_phy()` 中跳过某几个 lane 的 CR_PARA 写 | 部分 lane 无法 link up，link width 变小 | 理解 lane 协商 |
| 注释掉 Gen1 强制代码 | ASM1042A 可能无法 link up | 理解 Gen1 初始训练为什么必要 |
| 注释掉 `dw_pcie_wait_for_link()` 后面的速度恢复代码 | 链路永久停留在 Gen1 | 观察 `lspci -vvv` 的 LnkSta 速度 |
| 将 `PCIEX8MGMT_APP_LTSSM_ENABLE` 写为 0x0（不使能 LTSSM） | link 永远不 up，dmesg 报 timeout | 理解 LTSSM 是硬件自动机 |
| 在 `dw_pcie_prog_outbound_atu()` 前加 `return` | ECAM 读返回全 F，枚举不到设备 | 理解 iATU window 的必要性 |
| 在 `pci_scan_child_bus()` 返回前打印 bus/device 树 | 看到实际枚举到的 BDF 列表 | 理解深度优先枚举顺序 |

**每次实验的记录格式：**

```bash
# 实验前保存基线
{ echo "=== BASELINE $(date) ==="; lspci -vvv -t; lspci -vvv -xxx -s 00:00.0; \
  cat /proc/iomem | grep pci; } > exp-baseline.log

# 改代码 → ./build.sh dev-linux → 部署 → 启动 → 收集日志
{ echo "=== EXPERIMENT $(date) ==="; echo "Change: removed Gen1 force"; \
  dmesg | grep "\[PCIe Step\]"; lspci -vvv -s 00:00.0 | grep LnkSta; \
  cat /proc/iomem | grep pci; } > exp-no-gen1-force.log

# diff 对比
diff exp-baseline.log exp-no-gen1-force.log
```

#### 6.5.5 用 dynamic debug 替代重编译（不修改源码也能追踪）

如果不想重新编译，可以用内核 dynamic debug 机制启用已有的 `dev_dbg()` 调用点：

```bash
# 查看 pcie-fu740.c 中已有的 dev_dbg 调用
grep -n 'dev_dbg\|dev_info\|dev_warn' src/linux/drivers/pci/controller/dwc/pcie-fu740.c

# 启用所有 FU740 驱动中的调试输出
echo 'file pcie-fu740.c +pflmt' > /sys/kernel/debug/dynamic_debug/control

# 启用 DWC 核心层调试
echo 'file pcie-designware*.c +p' > /sys/kernel/debug/dynamic_debug/control
echo 'file pcie-designware-host.c +pflmt' > /sys/kernel/debug/dynamic_debug/control

# 要追踪枚举过程：
echo 'file probe.c +p' > /sys/kernel/debug/dynamic_debug/control
echo 'file setup-bus.c +p' > /sys/kernel/debug/dynamic_debug/control

# 实时查看
dmesg -wH

# 触发重新枚举使所有日志输出
echo 1 > /sys/bus/pci/rescan
```

**dev-linux vs dynamic debug 选型：**

| 场景 | 用 dev-linux | 用 dynamic debug |
|---|---|---|
| 添加新的 pr_info/printk | ✅ | ❌ (需要重编译) |
| 改逻辑（注释代码、改寄存器值） | ✅ | ❌ |
| 启用已有的 dev_dbg | ❌ (不需要) | ✅ (免重编译) |
| 追踪枚举流程 | ❌ (太底层) | ✅ (ftrace 更好) |
| 在 U-Boot 侧改代码 | ✅ (直接用 ./build.sh u-boot) | ❌ (U-Boot 无 dynamic debug) |

#### 6.5.6 实验完成后导出 patch

```bash
# === Linux 侧 ===
# dev-linux 模式下修改了 src/linux/
git -C src/linux status                    # 确认改动
git -C src/linux diff > /tmp/my-pcie-debug.patch

# 只导出特定文件的改动
git -C src/linux diff -- drivers/pci/controller/dwc/pcie-fu740.c \
  > patches/linux/0002-pcie-trace-probe.patch

# 如果多个文件都有改动，逐个 diff
git -C src/linux diff -- drivers/pci/controller/dwc/ \
  > patches/linux/0002-pcie-dwc-trace.patch

# 检查 patch 内容，确认不包含已有 0001 patch 的改动
grep '^---\|^+++' patches/linux/0002-pcie-trace-probe.patch

# === U-Boot 侧 ===
git -C src/u-boot diff -- drivers/pci/pcie_dw_sifive.c \
  > patches/u-boot/2026.01/0006-pcie-trace-init.patch
```

### 6.6 参考仓库（全部不需要 FPGA）

| 仓库 | 链接 | 用途 |
|---|---|---|
| **Learning-PCIe-from-scratch** | https://github.com/linuslau/Learning-PCIe-from-scratch | 学习笔记 + 真机协议分析仪 trace 日志 |
| **PCIe-CXL-study** | https://github.com/toaneliyan/PCIe-CXL-study | PCIe + CXL 学习笔记仓库 |
| **lkml PCI subsystem** | https://patchwork.ozlabs.org/project/linux-pci/list/ | 跟踪内核 PCI 子系统的最新改动和讨论 |
| **内核源码 (本项目)** | `src/linux/drivers/pci/` + `src/u-boot/drivers/pci/` | **最好的参考就是本项目已经下载好的源码** |

### 6.7 速查命令（全部在 Unmatched / QEMU 可用）

```bash
# === 拓扑与设备 ===
lspci -vvv -t                        # 树形拓扑
lspci -vvv -xxx                      # 完整 4KB 配置空间 hex dump
lspci -nn -d ::0108                  # 按 class code 过滤 (0108=NVMe)

```bash
# === 拓扑与设备 ===
lspci -vvv -t                        # 树形拓扑
lspci -vvv -xxx                      # 完整 4KB 配置空间 hex dump
lspci -nn -d ::0108                  # 按 class code 过滤 (0108=NVMe)
lspci -vvv -s 00:00.0 | grep -E "Capabilities|Status|Control"

# === 配置空间手动读写 ===
setpci -s 01:00.0 0x04.L             # 读 Command/Status
setpci -s 01:00.0 0x10.L=0xFFFFFFFF # 写全1到 BAR0 (BAR sizing)
setpci -s 01:00.0 0x04.L=0x6        # 写 Command: Bus Master + Mem Space

# === BAR 与资源 ===
cat /proc/iomem | grep -B1 -A5 pci   # PCIe MMIO 窗口分配
cat /sys/bus/pci/devices/*/resource  # 所有设备的 BAR
lspci -vvv | grep -E "Region|Memory at|I/O at"

# === 中断 ===
cat /proc/interrupts | grep -iE 'pci|nvme|msi'
cat /sys/bus/pci/devices/*/msi_irqs/* 2>/dev/null  # MSI 向量列表

# === ASPM / 功耗 ===
lspci -vvv | grep ASPM
cat /sys/bus/pci/devices/*/power/runtime_status

# === Dynamic Debug ===
echo 'file pcie-fu740.c +pflmt' > /sys/kernel/debug/dynamic_debug/control
echo 'file pcie-designware*.c +p' > /sys/kernel/debug/dynamic_debug/control
dmesg -wH                            # 实时带时间戳追踪

# === U-Boot 侧 ===
pci enum
pci 0
pci regions                          # 查看 iATU outbound window 配置
md 0xd00000000 64                    # ECAM dump
md 0xe000007C 2                      # LNKCAP
md 0xe0000080 2                      # PORT_DEBUG1: bit4=link_up, bit29=training
```

---

## 阶段 7：高级特性速览 — AER / ASPM / SR-IOV

以下特性在学习阶段 1-6 完成后可以按需选读，每个都可以展开成深入专题。

### AER — Advanced Error Reporting

PCIe 的高级错误报告，是 PCI 兼容 Parity Error 的重大升级：

| 错误类型 | 说明 | 处理方式 |
|---|---|---|
| **Correctable Error** | 链路层面可纠正（LCRC 校验失败后重传成功） | 硬件自动重传，驱动计统计 |
| **Uncorrectable Non-Fatal** | 不可纠正但可恢复（如 TLP Poison、Completion Timeout） | 驱动可重置受影响操作 |
| **Uncorrectable Fatal** | 致命错误，链路不可用（如 Link Training Error） | 需链路复位或设备重置 |

AER 寄存器位于 PCIe Extended Capability 空间（0x100 偏移之后），
通过 `pci_find_ext_capability(dev, PCI_EXT_CAP_ID_ERR)` 访问。

### ASPM — Active State Power Management

| 状态 | 说明 | 退出延迟 |
|---|---|---|
| L0 | 正常工作 | — |
| L0s | 单向浅度睡眠，无需对端配合 | < 4 µs |
| L1 | 双向协商深度睡眠，时钟可关闭 | < 64 µs |
| L1.1/L1.2 | 进一步关闭参考时钟和 PLL (Gen3+) | 数十 µs |

ASM1042A 桥可能只支持有限 ASPM 状态。`lspci -vvv | grep ASPM` 可确认。

### SR-IOV — Single Root I/O Virtualization

将一个物理设备虚拟化为多个轻量级 VF (Virtual Function)：

```text
PF (Physical Function):  BDF 01:00.0   ← 管理 + 数据
  ├─ VF 0:                BDF 01:00.1  ← 轻量级数据通路
  ├─ VF 1:                BDF 01:00.2
  └─ VF 2:                BDF 01:00.3
```

PF 负责全局配置，VF 只提供数据通路。NVMe 和网卡普遍支持。

### 其他可探索的特性

| 特性 | 缩写 | 说明 |
|---|---|---|
| Resizable BAR | ReBAR | 运行时调整 BAR 大小（GPU 关键特性） |
| Alternative Routing-ID | ARI | 突破 BDF 每个 Bus 最多 32 Device 的限制 |
| Address Translation Services | ATS | 设备主动请求 IOMMU 地址翻译缓存 |
| Page Request Interface | PRI | 设备缺页时主动向 OS 请求换入内存 |
| Downstream Port Containment | DPC | 硬件自动隔离故障设备防止扩散 |
| Precision Time Measurement | PTM | 链路上精确时间同步 |

### 推荐博客

| 博客 | 链接 | 说明 |
|---|---|---|
| **LKML: AER 枚举后清理讨论** | https://lkml.rescloud.iu.edu/2311.0/06189.html | Bjorn Helgaas 的 AER 邮件讨论 |
| **Kernel Doc: DWC PCIe PMU 中文** | https://docs.linuxkernel.org.cn/admin-guide/perf/dwc_pcie_pmu.html | DWC 性能监控单元文档 |
| **DeepWiki: PCI and IOMMU** | https://deepwiki.com/torvalds/linux/8.1-pci-and-iommu | PCIe + IOMMU 集成 |

---

## 书籍总表

| 优先级 | 书名 | 作者/出版社 | 语言 | 适合阶段 | 获取方式 |
|---|---|---|---|---|---|
| ⭐⭐⭐ | **《PCI Express 体系结构导读》** | 王齐 | 中文 | 入门 | 书店/图书馆 |
| ⭐⭐⭐ | **PCI Express Technology 3.0** | MindShare | 英文 | 进阶 | O'Reilly/Safari |
| ⭐⭐⭐ | **《PCI、PCI-X 和 PCI Express 的原理及体系结构》** | 马鸣锦 | 中文 | 零基础入门 | 图书馆 |
| ⭐⭐☆ | **PCI Express System Architecture** | MindShare | 英文 | 进阶 | O'Reilly/Safari |
| ⭐⭐☆ | **Linux Device Drivers 3rd Ed.** Ch.12 | O'Reilly | 英文 | 驱动开发 | https://lwn.net/Kernel/LDD3/ |
| ⭐⭐☆ | **PCI Express Base Specification** | PCI-SIG | 英文 | 工程查阅 | pcisig.com (会员) |

## 博客速查索引

| 主题 | 关键博客 | 特色 |
|---|---|---|
| **学习资料索引** | [知乎: 可以学习1W小时的PCIe](https://zhuanlan.zhihu.com/p/447134701) | 资源汇总最全 |
| **21天系统入门** | [CSDN: 21天学会PCIe专栏](https://blog.csdn.net/xiaoheshang_123/article/details/143440989) | 结构化每日学习计划 |
| **MindShare 中文翻译** | [GitHub: Chinese-Translation-of-PCI-Express-Technology](https://github.com/ljgibbslf/Chinese-Translation-of-PCI-Express-Technology) | 英文书的社区中译 |
| **配置空间布局** | [Type 0/1 Header 完整字节布局](#12-配置空间完整布局) | 本文档 §1.2 |
| **三层架构入门** | [CSDN: 彻底搞懂PCIe三层楼](https://blog.csdn.net/weixin_29266679/article/details/159634794) | 零基础友好 |
| **数据链路层** | [CSDN: PCIe笔记3 — DLLP/ACK/NAK](https://blog.csdn.net/asfgj123/article/details/148918742) | 链路可靠性机制 |
| **配置空间与BAR** | [CSDN: 大话PCIe-BAR空间和TLP](https://blog.csdn.net/mshgocn/article/details/78004130) | BAR 机制通俗讲解 |
| **TLP 格式** | [腾讯云: TLP学习经验分享](https://cloud.tencent.cn/developer/article/1766484) | 有抓包示例 |
| **地址空间关系** | [与非网: 访问PCIe BAR空间](https://www.e-com-net.com/article/1297163741355384832.htm) | 三种地址关系 |
| **LTSSM** | [dev.to: Understanding PCIe Link Training](https://dev.to/ripan030/understanding-pcie-link-training-165i) | 英文最佳 |
| **RISC-V MSI** | [本文档 §4 的 RISC-V 中断路径](#risc-v-平台的-msi-中断路径) | PLIC + DWC MSI 控制器 |
| **MSI-X** | [AWS MSI-X Guide](https://awsdocs-fpga-f2.readthedocs-hosted.com/latest/sdk/apps/msix-interrupts/README.html) | 工业级参考 |
| **Linux 枚举** | [腾讯云: Linux与PCIe深入理解](https://cloud.tencent.cn/developer/article/2669020) | 全生命周期 |
| **内核枚举源码** | [ProgrammerSought: PCIe device discovery](https://www.programmersought.com/article/26138889443/) | 逐行源码分析 |
| **DWC 驱动** | 本项目 `pcie-fu740.c` + `pcie-designware.h` | 一手源码 |
| **AER/ASPM 等** | [DeepWiki: PCI and IOMMU](https://deepwiki.com/torvalds/linux/8.1-pci-and-iommu) | 高级特性参考 |
| **Bare-metal RC** | 本项目 `pcie_dw_sifive.c` (U-Boot) | U-Boot 侧就是 bare-metal 风格实现 |

## 与本项目文档对应

| 外部学习阶段 | 本项目实践文档 | 实践环境 |
|---|---|---|
| 阶段 1 (协议基础) | [pcie-learning.md](pcie-learning.md) §1, [pcie-study.md](pcie-study.md) §1 | 物理板 / QEMU |
| 阶段 2 (TLP) | [spl-analysis.md](../boot/spl-analysis.md) §4 (FIT=TLP 载体) | U-Boot |
| 阶段 3 (LTSSM) | [pcie-study.md](pcie-study.md) §PCIe SerDes | U-Boot mgmt 寄存器 |
| 阶段 4 (中断) | [linux-on-unmatched.md](../linux/linux-on-unmatched.md) §PCIe | Linux /proc/interrupts |
| 阶段 5 (Linux 驱动) | [pcie-learning.md](pcie-learning.md) §3, [pcie-study.md](pcie-study.md) §7 | 物理板 / 源码 |
| 阶段 6 (实践) | [pcie-learning.md](pcie-learning.md) §4, [boot-chain-overview.md](../boot/boot-chain-overview.md) | 综合 |

## 建议学习节奏

```text
Week 1-2:   阶段 1  协议基础 — 拓扑/配置空间布局/Type 0&1 Header/BAR/枚举
            先看马鸣锦书(1-2天建立直觉) → 王齐书 §1-6(精读)
            → 本文档 §1.2 配置空间字节布局对照查阅
            → 在 U-Boot/Linux 上用 md/lspci/setpci 验证所学

Week 3:     阶段 2  TLP 与事务层 — Header 逐字段/Posted vs Non-Posted/路由/三种地址
            读腾讯云 TLP 博客 + MindShare Ch.5-7
            → 用 lspci -xxx 观察真实配置 TLP

Week 4:     阶段 2.5 数据链路层 — DLLP 类型/ACK-NAK/Replay Buffer/Credit Flow Control
            读 CSDN PCIe笔记3 + MindShare Ch.9-11
            → 用 dmesg + AER 日志理解链路可靠性

Week 5:     阶段 3  链路训练 — LTSSM 11状态/编码演进/Gen1 quirk/Detect 模拟原理
            读 dev.to 教程 → 在 U-Boot 中 dump mgmt 寄存器
            → 做 Gen1 quirk 实验

Week 6:     阶段 4  中断 — INTx/MSI/MSI-X/RISC-V PLIC 路径
            读 MSI-HOWTO + RISC-V 中断路径
            → 在 Unmatched 上观察 /proc/interrupts 的 MSI-X 分配

Week 7-8:   阶段 5  Linux PCIe 子系统 — 枚举源码/DWC 驱动架构/对比 U-Boot
            读 probe.c 枚举代码 → 读 pcie-fu740.c + pcie-designware.h
            → 对比 U-Boot 和 Linux 两侧的 PHY/Link/ATU 实现

Week 9+:    阶段 6+7 动手实践 + 高级特性
            物理板: 插入 NVMe/GPU → dynamic debug 追踪 → dev-linux 导出 patch
            QEMU: BAR sizing 实验 → 带 NVMe 的 QEMU 中断实验
            高级: AER/ASPM 日志分析 → SR-IOV → ReBAR
```

**如果你更喜欢按天规划的学习节奏：** 参考 CSDN 上的
[21天学会PCIe专栏](https://blog.csdn.net/xiaoheshang_123/article/details/143440989)，
每天聚焦一个子主题，21 天覆盖从架构到驱动开发的完整知识面。
