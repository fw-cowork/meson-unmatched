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
| [tftp-boot.md](boot/tftp-boot.md) | Static-IP Linux boot and SPL/OpenSBI/U-Boot TFTP update flow |
| [spl-analysis.md](boot/spl-analysis.md) | U-Boot SPL code analysis and compilation |
| [opensbi-on-unmatched.md](boot/opensbi-on-unmatched.md) | OpenSBI role, fw_dynamic protocol, and platform integration |
| [uboot-boot-log.md](boot/uboot-boot-log.md) | U-Boot proper boot log analysis: PCIe, USB, extlinux, kernel decompress & relocation |

## Linux Kernel

Linux kernel perspective — config, device tree, drivers, and development workflow.

| File | Description |
|---|---|
| [linux-on-unmatched.md](linux/linux-on-unmatched.md) | Linux kernel config, DTS, PCIe driver hierarchy, BusyBox rootfs, dev workflow |

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
