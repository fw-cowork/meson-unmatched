# Documentation Index

## Getting Started

| File | Description |
|---|---|
| [learning-plan.md](learning-plan.md) | Structured 5-phase learning plan for the project |

## Architecture & Design

Framework design, specification, and repository structure documentation.

| File | Description |
|---|---|
| [DESIGN.md](architecture/DESIGN.md) | Build framework design document |
| [baremetal-design.md](architecture/baremetal-design.md) | U-Boot `go` 裸机程序设计、Meson 分层和扩展约定 |
| [ADDING-APP.md](../baremetal/ADDING-APP.md) | 新增 bare-metal 测试程序、构建检查及 TFTP 上板指南 |
| [framework-spec.md](architecture/framework-spec.md) | Framework specification |
| [repo-framework.md](architecture/repo-framework.md) | Repository framework organization |

## Build

Build instructions and toolchain setup.

| File | Description |
|---|---|
| [build.md](build/build.md) | Build guide (SD card image, QEMU) |

## Boot Chain

Boot chain analysis — ZSBL → SPL → OpenSBI → U-Boot → Linux.

| File | Description |
|---|---|
| [boot-chain-overview.md](boot/boot-chain-overview.md) | Complete boot chain synthesis: all five stages in one document |
| [uboot-mmode-baremetal.md](boot/uboot-mmode-baremetal.md) | SPL 直启 M-mode U-Boot，并通过 TFTP 加载和返回 baremetal |
| [tftp-boot.md](boot/tftp-boot.md) | Static-IP Linux boot and SPL/OpenSBI/U-Boot TFTP update flow |
| [spl-analysis.md](boot/spl-analysis.md) | U-Boot SPL code analysis and compilation |
| [opensbi-on-unmatched.md](boot/opensbi-on-unmatched.md) | OpenSBI role, fw_dynamic protocol, and platform integration |
| [uboot-boot-log.md](boot/uboot-boot-log.md) | U-Boot proper boot log analysis: PCIe, USB, extlinux, kernel decompress & relocation |

## OpenSBI

| File | Description |
|---|---|
| [README.md](opensbi/README.md) | OpenSBI 官方资料索引、学习路线、版本固定和 Unmatched 关联入口 |
| [getting-started.md](opensbi/getting-started.md) | OpenSBI 从零开始：第一条命令、第一周顺序和入门验收标准 |
| [source-guide.md](opensbi/source-guide.md) | 从 firmware 入口、trap/ecall 到平台 hook 的源码导读与实验 |
| [architecture.md](opensbi/architecture.md) | 特权级、SBI ABI、scratch、domain 与 OpenSBI 分层 |
| [firmware-boot.md](opensbi/firmware-boot.md) | SPL -> FW_DYNAMIC -> relocation -> cold/warmboot -> mret 启动详解 |
| [ecall-extensions.md](opensbi/ecall-extensions.md) | trap/ecall 分发、扩展总表、TIME/IPI/RFENCE/HSM/SRST 源码路径 |
| [sbi-abi-reference.md](opensbi/sbi-abi-reference.md) | SBI 寄存器 ABI、错误码、常用扩展参数速查 |
| [platform-fu740.md](opensbi/platform-fu740.md) | generic FDT、FU740 override、DA9063 reset 与 TLB errata |
| [debug-testing.md](opensbi/debug-testing.md) | 构建、ELF、QEMU/GDB、SBIUnit、上板验证和回归矩阵 |
| [labs.md](opensbi/labs.md) | 从固件基线到 ecall、SBIUnit、PMIC 和 vendor extension 的动手实验 |
| [source-annotations.md](opensbi/source-annotations.md) | 按执行顺序的函数级源码索引、调用图和故障切分 |
| [porting-checklist.md](opensbi/porting-checklist.md) | 新 RISC-V 平台的硬件要求、FDT、Kconfig、设备和验收清单 |
| [security-model.md](opensbi/security-model.md) | OpenSBI 特权边界、domain/PMP、共享内存和安全审查 |

## Linux Kernel

Linux kernel perspective — config, device tree, drivers, and development workflow.

| File | Description |
|---|---|
| [linux-on-unmatched.md](linux/linux-on-unmatched.md) | Linux kernel config, DTS, PCIe driver hierarchy, BusyBox rootfs, dev workflow |

## Network

| File | Description |
|---|---|
| [lwip-port.md](network/lwip-port.md) | 从官方最新 lwIP 源码移植到 U-Boot 的构建过程、版本固定与上板验证 |
| [lwip-test.md](network/lwip-test.md) | U-Boot lwIP 端口的编译、部署、协议测试、抓包与验收清单 |
| [PORTING.md](../ports/uboot-lwip/PORTING.md) | lwIP 端口实现契约、收发路径、生命周期与新增协议指南 |

## PCIe

PCIe subsystem study notes on the HiFive Unmatched.

| File | Description |
|---|---|
| [pcie-learning.md](pcie/pcie-learning.md) | PCIe learning roadmap |
| [pcie-study.md](pcie/pcie-study.md) | PCIe study notes (from unmatched docs) |
| [pcie-resources.md](pcie/pcie-resources.md) | **PCIe learning resources** — books, tutorials, specs, FPGA, kernel docs |

## UML

PlantUML diagrams describing build process, component relationships, and data flow.

| File | Description |
|---|---|
| [README.md](uml/README.md) | UML diagram index |
| [artifact-flow.puml](uml/artifact-flow.puml) | Artifact flow diagram |
| [build-sequence.puml](uml/build-sequence.puml) | Build sequence diagram |
| [command-surface.puml](uml/command-surface.puml) | Command surface |
| [component-state.puml](uml/component-state.puml) | Component state diagram |
| [data-model.puml](uml/data-model.puml) | Data model |
| [dependency-graph.puml](uml/dependency-graph.puml) | Dependency graph |
| [repository-components.puml](uml/repository-components.puml) | Repository components |
| [sd-update-sequence.puml](uml/sd-update-sequence.puml) | SD card update sequence |

## Reference

Framework reference and official hardware documentation.

| File | Description |
|---|---|
| [memory-map.md](reference/memory-map.md) | Complete physical address reference for FU740 and Unmatched |
| [pwm.md](reference/pwm.md) | FU740 PWM 原理、寄存器、U-Boot 使用与学习路径 |

Official SiFive HiFive Unmatched hardware and software documentation.

| Directory | Description |
|---|---|
| [reference/unmatched/](reference/unmatched/) | Datasheets, schematics, manuals, BOM, mechanical drawings |
| [reference/unmatched/getting-started/](reference/unmatched/getting-started/) | Getting started guides (multi-language) |
| [reference/unmatched/hardware/](reference/unmatched/hardware/) | Hardware datasheets, schematics, FU740 manual |
| [reference/unmatched/software/](reference/unmatched/software/) | Software reference manual |
| [reference/unmatched/mechanical/](reference/unmatched/mechanical/) | Mechanical STEP file |
