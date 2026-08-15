# HiFive Unmatched 启动链与 PCIe 学习计划

本文档基于 `docs/` 中的全部项目文档，按依赖关系由浅入深组织，共五个阶段。

## 阶段 1：硬件基础与环境搭建（第 1-2 周）

**目标：** 理解板级硬件拓扑，搭建可重复构建环境，在板上启动到 Linux。

### 1.1 硬件概览

| 顺序 | 文档 | 重点 |
|---|---|---|
| 1 | `reference/unmatched/hardware/hifive-unmatched-datasheet.pdf` | 板载器件总览：FU740、DDR4、QSPI、microSD、PCIe x16、FTDI USB-JTAG/UART、PMIC |
| 2 | `reference/unmatched/hardware/hifive-unmatched-schematics-v3.pdf` | 重点阅读：FU740 - Misc、Boot Select、JTAG MUX、Reset、Clock Select、PCIe power/reset GPIOs |
| 3 | `reference/unmatched/hardware/freedom-u740-c000-manual-v1p7.pdf` | FU740 SoC 子系统概览 |
| 4 | `pcie/pcie-study.md` §1 Board overview | 归纳板级器件和调试拓扑 |

**验收标准：** 能画出 Unmatched 板级框图，解释关键器件和 GPIO 连接。

### 1.2 构建环境搭建

| 顺序 | 文档 | 重点 |
|---|---|---|
| 1 | `build/build.md` §初始化 | 安装宿主工具、KAS、QEMU |
| 2 | `build/build.md` §Unmatched | `./build.sh toolchain && ./build.sh` 构建 SD 镜像 |
| 3 | `build/build.md` §QEMU | `./build.sh qemu && ./qemu.sh` 验证 QEMU 启动 |
| 4 | `build/build.md` §QEMU GDB | `./qemu-gdb.sh --build` 熟悉 GDB 调试流程 |

**验收标准：**

- `./build.sh` 产出 `deploy/unmatched-lite.img`
- QEMU 启动到 BusyBox shell
- GDB 可 attach 到 QEMU 并设置断点

### 1.3 框架架构理解

| 顺序 | 文档 | 重点 |
|---|---|---|
| 1 | `architecture/DESIGN.md` | 框架设计目标、分层架构、组件契约 |
| 2 | `architecture/framework-spec.md` | 配置模型 (YAML)、命令模型、后端接口 |
| 3 | `architecture/repo-framework.md` | 目标仓库布局、目录职责、迁移计划 |
| 4 | `uml/README.md` + 各 `.puml` | 可视化理解 build-sequence、component-state、dependency-graph |

**验收标准：** 能解释 Meson/Ninja → litebuild.py → Make/Kbuild 三层架构。

---

## 阶段 2：启动链深度理解（第 3-4 周）

**目标：** 从 ZSBL 到 Linux 完整追踪启动链，理解每一阶段的职责和交接。

### 2.1 启动全貌

| 顺序 | 文档 | 重点 |
|---|---|---|
| 1 | `boot/boot-chain-overview.md` | **先读这个：** 五阶段启动链总览，建立全局概念 |
| 2 | `pcie/pcie-study.md` §2 Boot mode and MSEL | MSEL DIP 开关、ZSBL 启动源选择 |
| 3 | `pcie/pcie-study.md` §3 Image layout | SPI Flash / SD 卡分区布局、raw GPT 分区 vs 文件系统 |
| 4 | `pcie/pcie-study.md` §8 Ownership summary | 各阶段职责总结表 |

### 2.2 U-Boot SPL 深度分析

| 顺序 | 文档 | 重点 |
|---|---|---|
| 1 | `reference/memory-map.md` | **配合阅读：** 全部物理地址速查，理解 SPL 内存布局 |
| 2 | `boot/spl-analysis.md` §1-2 | 总体架构、内存地址布局、汇编入口 `_start` |
| 2 | `boot/spl-analysis.md` §2.3-2.5 | FU740 早期初始化、板级 GPIO 复位序列、MSEL 设备选择 |
| 3 | `boot/spl-analysis.md` §3 | `board_init_r` 通用 SPL 框架 |
| 4 | `boot/spl-analysis.md` §4-5 | FIT 镜像加载、跳转 OpenSBI 协议 (fw_dynamic_info) |
| 5 | `boot/spl-analysis.md` §6 | SPL 编译过程：三层构建系统、Kconfig 配置、链接脚本 |
| 6 | `boot/spl-analysis.md` §7 | 设计要点：多核同步、PCB 版本兼容、DM 驱动模型 |

**验收标准：** 能画出 SPL 从 `_start` 到 `spl_invoke_opensbi()` 的完整调用链。

### 2.3 OpenSBI 运行时

| 顺序 | 文档 | 重点 |
|---|---|---|
| 1 | `opensbi/README.md` | 官方资料、RISC-V 前置、SBI 契约、firmware 类型和学习路线 |
| 2 | `opensbi/getting-started.md` | 第一条命令、第一周阅读顺序、第一次 GDB 和入门验收 |
| 3 | `opensbi/architecture.md` | 特权级、SBI ABI、trap、scratch、domain、PMP 和初始化生命周期 |
| 4 | `opensbi/firmware-boot.md` | SPL 构造 fw_dynamic_info、relocation、cold/warmboot 和 `mret` |
| 5 | `opensbi/ecall-extensions.md` | ecall 注册/分发、现代与 legacy ABI、核心扩展调用链 |
| 6 | `opensbi/sbi-abi-reference.md` | 常用扩展参数、返回值、错误码和 RV32/RV64 差异 |
| 7 | `opensbi/platform-fu740.md` | generic FDT、FU740 PMIC/TLB hook 和各阶段 ownership |
| 8 | `opensbi/debug-testing.md` | ELF/GDB/SBIUnit/QEMU/上板测试和回归矩阵 |
| 9 | `opensbi/labs.md` | 按风险递进完成 9 个源码和硬件实验 |
| 10 | `opensbi/source-annotations.md` | 按函数和共享状态复盘完整调用图 |
| 11 | `opensbi/porting-checklist.md` | 把方法迁移到另一台 RISC-V 机器 |
| 12 | `opensbi/security-model.md` | 理解 domain/PMP、trap delegation 和共享内存安全边界 |
| 13 | `boot/opensbi-on-unmatched.md` | 当前板子的启动位置、镜像布局和边界总结 |
| 14 | `boot/spl-analysis.md` §5 | 对照 SPL 如何构造 fw_dynamic_info 并跳转 OpenSBI |

**验收标准：** 能解释 FW_DYNAMIC 模式的三个字段 (next_addr, next_mode, boot_hart) 的含义。

### 2.4 U-Boot Proper 到 Linux

| 顺序 | 文档 | 重点 |
|---|---|---|
| 1 | `boot/uboot-boot-log.md` §1-2 | PCIe 枚举 → ASM1042A xHCI USB 桥 (Gen1)；USB 初始化 |
| 2 | `boot/uboot-boot-log.md` §3-5 | MMC/SD 分区扫描、distro boot、extlinux 引导协议 |
| 3 | `boot/uboot-boot-log.md` §6 | **关键：** 内核解压与重定位三段式流程 (gzip → Header → memmove) |
| 4 | `boot/uboot-boot-log.md` §7 | FDT fixup 与内核启动约定 (a0=hart, a1=dtb) |

**验收标准：** 能解释为什么 `Image.gz` 必须经过 "解压 → 读 Header → memmove" 三步才能启动。

### 2.5 Linux 内核总览

| 顺序 | 文档 | 重点 |
|---|---|---|
| 1 | `linux/linux-on-unmatched.md` | 内核配置、设备树、驱动结构、BusyBox rootfs、开发工作流 |

**验收标准：** 能解释 FU740 Linux PCIe 驱动的三层架构和 probe 调用链。

---

## 阶段 3：PCIe 子系统（第 5-7 周）

**目标：** 理解 FU740 PCIe 控制器的硬件拓扑、U-Boot 和 Linux 两侧的初始化流程。

### 3.1 协议与硬件

| 顺序 | 文档 | 重点 |
|---|---|---|
| 1 | `pcie/pcie-learning.md` §1.1 | PCIe 核心概念表：BDF、ECAM、BAR、ATU、MSI、LTSSM |
| 2 | `pcie/pcie-learning.md` §1.2-1.4 | FU740 硬件拓扑 (RC → ASM1042A Switch → x16/M.2)、寄存器映射、DWC 三层架构 |
| 3 | `pcie/pcie-study.md` §PCIe SerDes and link-up path | CR_PARA PHY 编程接口、LTSSM 使能、link-up 轮询 |
| 4 | `pcie/pcie-study.md` §PCIe PHY IP inference | 推断 DWC PHY 类型 (CR_PARA 命名) |
| 5 | `reference/unmatched/hardware/freedom-u740-c000-manual-v1p7.pdf` PCIe 章节 | FU740 手册中的 PCIe X8 AXI4 Subsystem |

### 3.2 U-Boot PCIe 枚举

| 顺序 | 文档 | 重点 |
|---|---|---|
| 1 | `pcie/pcie-learning.md` §2.1 | SPL 中 `spl_usb_pcie_bridge_init()` GPIO 复位 |
| 2 | `pcie/pcie-learning.md` §2.2 | U-Boot proper probe: PHY init → Gen1 force → LTSSM → link wait |
| 3 | `pcie/pcie-learning.md` §2.3 | 阅读顺序：`pcie_dw_sifive.c` → `pcie_dw_common.h` → `pci-uclass.c` |
| 4 | `pcie/pcie-study.md` §5-6 | U-Boot PCIe bring-up 完整流程、平台数据结构 |

**动手实验：**

```bash
# U-Boot 命令行
pci enum              # 枚举 PCIe 总线
pci 0                 # 显示设备详情 (BDF, Vendor/Device ID, BAR)
nvme scan             # 扫描 NVMe 设备
```

- 修改 `pcie_dw_sifive.c` 添加调试输出（使用 `./build.sh dev-linux` 保留源码修改）
- Gen1 quirk 实验：注释强制 Gen1 逻辑观察行为变化

### 3.3 Linux PCIe 驱动

| 顺序 | 文档 | 重点 |
|---|---|---|
| 1 | `pcie/pcie-learning.md` §3.1 | Linux probe 完整调用链 `fu740_pcie_probe()` → `dw_pcie_host_init()` |
| 2 | `pcie/pcie-learning.md` §3.2 | 文件阅读顺序：`pcie-fu740.c` (357行) → `pcie-designware.h` (967行) → host.c/probe.c |
| 3 | `pcie/pcie-study.md` §7 | Linux PCIe flow: host_init → init_phy → start_link → pci_host_probe |
| 4 | `pcie/pcie-study.md` §Linux checkpoints | `lspci -vvv`、`/proc/iomem`、`dmesg` 验证命令 |

**动手实验：**

```bash
lspci -vvv -t                          # 树形拓扑
cat /sys/bus/pci/devices/0000:01:00.0/config  # 原始 256-byte 配置空间
hexdump -C /sys/bus/pci/devices/0000:01:00.0/config
cat /proc/iomem | grep -A 20 pci        # PCIe MMIO 窗口

# Dynamic debug 追踪 PCIe 日志 (无需重编译)
echo 'file pcie-fu740.c +p' > /sys/kernel/debug/dynamic_debug/control
echo 'file pcie-designware*.c +p' > /sys/kernel/debug/dynamic_debug/control
```

- 插入 NVMe SSD 观察 U-Boot `nvme scan` 和 Linux `nvme list` 行为

### 3.4 对比与总结

| 顺序 | 文档 | 重点 |
|---|---|---|
| 1 | `pcie/pcie-learning.md` §5.1 | U-Boot vs Linux PCIe 枚举对比表 |
| 2 | `pcie/pcie-learning.md` §5.2 | QEMU virt GPEX vs FU740 DWC 差异 |
| 3 | `pcie/pcie-study.md` §Failure triage | PCIe 常见故障排查 (无输出、设备不可见、NVMe 无块设备) |

---

## 阶段 4：动手实验与高级话题（第 8-9 周）

**目标：** 独立调试、动手修改代码、记录实验数据。

### 4.1 调试工具链

| 实验 | 参考文档 | 说明 |
|---|---|---|
| Linux 开发模式 | `build/build.md` §Linux 开发模式 | `./build.sh dev-linux` 保留源码修改，导出 patch |
| GDB 调试 | `pcie/pcie-study.md` §JTAG | FTDI OpenOCD, `MSEL=0000` debugger wait, `gdb-multiarch` attach |
| 串口日志收集 | `pcie/pcie-learning.md` §4 | `dmesg` / `lspci` / `iomem` 三份日志保存 |

**导出实验 patch：**

```bash
git -C src/linux diff -- drivers/pci/ > patches/linux/0002-pcie-learning.patch
```

### 4.2 PCIe 实验清单

| 实验 | 参考文档 | 说明 |
|---|---|---|
| NVMe 全流程 | `pcie/pcie-learning.md` §4.2 | `nvme id-ctrl` → `smart-log` → `dd` 带宽测试 |
| 插入外设 | `pcie/pcie-learning.md` §4.3 | PCIe 网卡/WiFi/GPU，`echo 1 > /sys/bus/pci/rescan` 触发重新枚举 |
| Bare metal 最小初始化 | `pcie/pcie-learning.md` §4.4 | 12 步 PCIe 初始化序列 |
| 性能测试 | `pcie/pcie-learning.md` §5.4 | NVMe Gen3 x4 带宽理论值 vs 实测 |

### 4.3 DDR 深入（可选）

| 顺序 | 文档 | 重点 |
|---|---|---|
| 1 | `pcie/pcie-study.md` §DDR bring-up path | DDR 控制器/PHY 内存映射、`sifive,ddr-params` 参数表 |
| 2 | `pcie/pcie-study.md` §DDR checkpoints | `bdinfo`、`md.l` 寄存器检查 |
| 3 | `pcie/pcie-study.md` §DDR failure triage | "DDR invalid size" 等故障排查 |

---

## 阶段 5：框架贡献与架构演进（第 10 周+）

**目标：** 理解框架的未来方向，可以贡献 patches 或适配新板。

| 顺序 | 文档 | 重点 |
|---|---|---|
| 1 | `architecture/framework-spec.md` §19-20 | 实施计划 (Phase 1-4)，扩展点（新板/新组件） |
| 2 | `architecture/repo-framework.md` §14 | 迁移计划：YAML manifest、schema 验证、部署元数据 |
| 3 | `architecture/DESIGN.md` §21-22 | 实施阶段与开放问题 |
| 4 | `uml/` 全部 `.puml` | 理解 artifact-flow、sd-update-sequence 等未来工具链 |

**验收标准：**

- 能将实验修改导出为格式正确的 patch
- 能解释 YAML manifest → Meson → script 的数据流
- 知道在何处为新板添加 board.yml / manifest / cross file

---

## 学习节奏建议

```text
Week  1-2:  阶段 1 (硬件基础 + 环境搭建 + 框架架构)
Week  3-4:  阶段 2 (SPL 深度分析 + U-Boot → Linux 重定位)
Week  5-7:  阶段 3 (PCIe 协议 → U-Boot 枚举 → Linux 驱动 → 对比)
Week  8-9:  阶段 4 (动手实验、JTAG 调试、bare metal 尝试)
Week 10+:   阶段 5 (框架演进、贡献 patch)
```

每个阶段内部按上表顺序阅读，先读英文文档建立概念框架，再读中文文档
（`build.md`、`spl-analysis.md`、`uboot-boot-log.md`）深入代码细节。遇到知识点
交叉时（如 PCIe 学习计划中引用了 SPL 的 bridge reset），可向前跳转确认前置知识。

## 关键文件速查

### 项目文档

| 文件 | 说明 |
|---|---|
| `docs/build/build.md` | 构建说明 (SD 镜像、QEMU、GDB、开发模式) |
| `docs/boot/boot-chain-overview.md` | **启动链总览** — 五阶段完整流程 |
| `docs/boot/spl-analysis.md` | U-Boot SPL 代码解析 (启动流程、编译、FIT) |
| `docs/boot/opensbi-on-unmatched.md` | **OpenSBI 深度分析** — fw_dynamic、SBI 服务、平台集成 |
| `docs/opensbi/README.md` | OpenSBI 官方资料索引、版本固定和学习路线 |
| `docs/opensbi/getting-started.md` | OpenSBI 从零开始和第一周学习路径 |
| `docs/opensbi/source-guide.md` | OpenSBI firmware/trap/ecall/platform 源码导读与实验 |
| `docs/opensbi/architecture.md` | OpenSBI 特权级、ABI、scratch、domain 和生命周期 |
| `docs/opensbi/firmware-boot.md` | OpenSBI firmware 启动、relocation、cold/warmboot 和 mret |
| `docs/opensbi/ecall-extensions.md` | SBI ecall/扩展分发与核心 handler 源码导读 |
| `docs/opensbi/sbi-abi-reference.md` | SBI ABI、错误码和常用 extension 参数速查 |
| `docs/opensbi/platform-fu740.md` | generic/FU740 平台适配、FDT、PMIC 和 errata |
| `docs/opensbi/debug-testing.md` | OpenSBI 构建、GDB、SBIUnit、QEMU 和上板验证 |
| `docs/opensbi/labs.md` | OpenSBI 源码、ABI、SBIUnit 和 Unmatched 动手实验 |
| `docs/opensbi/source-annotations.md` | OpenSBI 函数级源码索引和调用图 |
| `docs/opensbi/porting-checklist.md` | 新平台移植、FDT、Kconfig、设备和验收清单 |
| `docs/opensbi/security-model.md` | OpenSBI 安全模型、domain/PMP 和共享内存审查 |
| `docs/boot/uboot-boot-log.md` | U-Boot proper 启动日志分析 (内核重定位) |
| `docs/linux/linux-on-unmatched.md` | **Linux 内核视角** — config、DTS、驱动、rootfs |
| `docs/pcie/pcie-learning.md` | PCIe 学习路线与实验计划 |
| `docs/pcie/pcie-study.md` | PCIe 学习笔记 (硬件拓扑、DDR、JTAG) |
| `docs/pcie/pcie-resources.md` | **PCIe 外部学习资源** — 书籍、教程、规范、FPGA、内核文档 |
| `docs/reference/memory-map.md` | **内存布局参考** — 全部物理地址速查 |
| `docs/architecture/DESIGN.md` | 构建框架设计文档 |
| `docs/architecture/framework-spec.md` | 框架规格说明 (YAML schema、命令模型) |
| `docs/architecture/repo-framework.md` | 仓库框架组织与迁移计划 |

### 关键源码（构建后位于 `src/`）

| 文件 | 说明 |
|---|---|
| `src/u-boot/board/sifive/unmatched/spl.c` | 板级 SPL 初始化 |
| `src/u-boot/arch/riscv/cpu/fu740/spl.c` | FU740 SPL (DDR init) |
| `src/u-boot/drivers/pci/pcie_dw_sifive.c` | U-Boot FU740 PCIe 驱动 |
| `src/u-boot/drivers/pci/pcie_dw_common.c` | U-Boot DWC 通用代码 |
| `src/linux/drivers/pci/controller/dwc/pcie-fu740.c` | Linux FU740 PCIe 驱动 |
| `src/linux/drivers/pci/controller/dwc/pcie-designware.c` | Linux DWC 通用实现 |
| `src/linux/drivers/pci/controller/dwc/pcie-designware-host.c` | Linux DWC host 模式 |
| `src/linux/arch/riscv/boot/dts/sifive/fu740-c000.dtsi` | FU740 SoC 设备树 |
| `src/linux/arch/riscv/boot/dts/sifive/hifive-unmatched-a00.dts` | Unmatched 板级设备树 |
| `scripts/litebuild.py` | 构建驱动脚本 |

### 参考文档

| 文件 | 说明 |
|---|---|
| `docs/reference/unmatched/hardware/hifive-unmatched-datasheet.pdf` | 板级数据手册 |
| `docs/reference/unmatched/hardware/hifive-unmatched-schematics-v3.pdf` | 板级原理图 |
| `docs/reference/unmatched/hardware/freedom-u740-c000-manual-v1p7.pdf` | FU740 SoC 手册 |
| `docs/reference/unmatched/software/hifive-unmatched-sw-reference-manual-v1p1.pdf` | 软件参考手册 |
