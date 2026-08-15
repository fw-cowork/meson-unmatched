# OpenSBI 从零开始学习

如果你第一次接触 OpenSBI，不要一开始就打开整个 `src/opensbi` 目录，也不要先改
`fw_base.S`。按本文顺序完成第一轮学习，先建立启动链和 SBI ABI 的整体模型，再进入
汇编、trap、多核和平台移植。

## 1. 先知道自己要学什么

本项目中的 OpenSBI 位于 M-mode，主要承担两类工作：

```text
启动工作：SPL 传入 FDT 和 fw_dynamic_info
          OpenSBI 初始化 HART、scratch、timer、IPI、domain
          最后 mret 到 U-Boot/Linux

运行时工作：S-mode 执行 ecall
            OpenSBI 在 M-mode 处理 Timer/IPI/RFENCE/HSM/SRST 等请求
            返回 a0=error、a1=value
```

第一轮学习的验收目标不是“读完所有代码”，而是能够解释：

1. SPL 如何把 `fw_dynamic_info` 传给 OpenSBI；
2. `next_addr`、`next_mode`、FDT 如何变成最终的 `mepc/mstatus/a0/a1`；
3. 一条 S-mode `ecall` 如何到达具体 extension handler；
4. 为什么 timer、IPI、RFENCE 和 reset 需要 M-mode；
5. generic/FU740 平台代码和 U-Boot/Linux 的职责边界。

## 2. 第一次开始：只做三件事

### 第一步：读项目入口

先读 [OpenSBI 总入口](README.md) 的以下部分：

```text
1. 本项目使用的版本
2. 先建立正确的心智模型
3. 推荐学习路线中的专题表
```

确认当前固定版本：

```bash
git -C src/opensbi rev-parse HEAD
rg -n 'OPEN_SBI_REV|FW_TEXT_START|PLATFORM=generic' scripts/litebuild.py
```

当前项目固定 OpenSBI commit 为：

```text
74434f255873d74e56cc50aa762d1caf24c099f8
```

### 第二步：建立可观察的固件

如果工具链已经安装：

```bash
./build.sh check
./build.sh opensbi-fw
```

如果是新机器：

```bash
./build.sh toolchain
./build.sh fetch-sources
./build.sh check
./build.sh opensbi-fw
```

检查产物：

```bash
file deploy/fw_dynamic.elf deploy/fw_dynamic.bin
riscv64-freedomusdk-linux-readelf -h deploy/fw_dynamic.elf
riscv64-freedomusdk-linux-nm -n deploy/fw_dynamic.elf | \
  rg '_start|fw_boot_hart|fw_platform_init|sbi_init|sbi_trap_handler|sbi_ecall_handler'
```

第一天只观察，不修改 OpenSBI 源码。你应该看到 ELF、BIN、入口地址和关键符号。

### 第三步：画一张启动链图

在纸上或笔记中写出：

```text
ZSBL
  -> U-Boot SPL (M-mode)
     -> fw_dynamic.elf (M-mode OpenSBI)
        -> U-Boot proper (S-mode)
           -> Linux (S-mode)
```

旁边标注：

```text
SPL -> a0=hartid, a1=FDT, a2=fw_dynamic_info*
OpenSBI -> a0=hartid, a1=FDT, mret
S-mode -> a7=extension, a6=function, a0-a5=args, ecall
OpenSBI -> a0=error, a1=value
```

如果这张图还画不出来，不要进入 PMU、SSE、MPXY 或 Smepmp 细节。

## 3. 第一周学习顺序

### Day 1：启动链和特权级

阅读：

1. [architecture.md](architecture.md) §1-3；
2. [firmware-boot.md](firmware-boot.md) §1-2；
3. 项目 [boot-chain-overview.md](../boot/boot-chain-overview.md)。

完成：解释 M-mode、S-mode、SBI、SEE、`mret` 的关系。

### Day 2：FW_DYNAMIC

阅读：

1. `src/u-boot/common/spl/spl_opensbi.c`；
2. `src/u-boot/include/opensbi.h`；
3. `src/opensbi/include/sbi/fw_dynamic.h`；
4. [firmware-boot.md](firmware-boot.md) §3-6。

完成：写出 RV64 下 48-byte `fw_dynamic_info` 的六个字段和 offset。

### Day 3：OpenSBI 初始化

阅读：

1. `src/opensbi/lib/sbi/sbi_init.c:sbi_init()`；
2. `init_coldboot()`；
3. `init_warmboot()`；
4. [source-annotations.md](source-annotations.md) §2-4。

完成：解释为什么入口 lottery 和 coldboot lottery 是两次不同的竞争。

### Day 4：trap 和 ecall

阅读：

1. `src/opensbi/include/sbi/sbi_trap.h`；
2. `src/opensbi/firmware/fw_base.S` 的 trap 宏；
3. `src/opensbi/lib/sbi/sbi_trap.c`；
4. `src/opensbi/lib/sbi/sbi_ecall.c`；
5. [ecall-extensions.md](ecall-extensions.md) 和 [sbi-abi-reference.md](sbi-abi-reference.md)。

完成：画出 `ecall -> trap -> handler -> mepc += 4 -> mret`。

### Day 5：读三个最小 extension

按这个顺序读：

```text
sbi_ecall_base.c    # 无硬件副作用，最适合作为第一个 handler
sbi_ecall_time.c    # 一个参数进入 timer device
sbi_ecall_ipi.c     # 两个参数进入多 HART 路径
```

完成：用 ABI 速查表写出每个 function 的 `a0-a5` 用途和返回值。

### Day 6：平台和 FDT

阅读：

1. `src/opensbi/platform/generic/platform.c`；
2. `src/opensbi/platform/generic/sifive/fu740.c`；
3. [platform-fu740.md](platform-fu740.md)；
4. [porting-checklist.md](porting-checklist.md) §1-5。

完成：解释为什么当前 OpenSBI 不负责 Unmatched 的 DDR、PCIe 枚举和 Linux 驱动。

### Day 7：复盘和第一次调试

执行：

```bash
./qemu-gdb.sh --build
gdb-multiarch deploy/qemu/fw_dynamic.elf
```

在 GDB 中设置：

```gdb
target remote 127.0.0.1:1234
b fw_boot_hart
b fw_platform_init
b sbi_init
b sbi_hart_switch_mode
c
```

完成：记录 `a0/a1/a2`、`next_addr`、`next_mode` 和最终 `mepc`。

## 4. 第二周才开始改代码

修改顺序必须从低风险到高风险：

```text
1. readelf/nm/objdump 静态分析
2. 只增加一条 OpenSBI 日志
3. QEMU GDB 断点和寄存器观察
4. SBIUnit 纯函数测试
5. generic/FU740 platform hook
6. timer/IPI/RFENCE 多 HART
7. PMIC reset
8. fw_base.S、PMP/Smepmp、linker script
```

第一处源码修改建议选择一个无副作用函数或日志点。不要从以下位置开始：

```text
fw_base.S relocation
sbi_hart_switch_mode
sbi_hart_protection_configure
FU740 DA9063 reset
```

这些位置出错时，可能没有串口、没有 GDB 断点或直接触发真实板复位。

## 5. 遇到不懂的代码时怎么查

按四步查，不要从变量名猜行为：

1. 先找结构体定义：`rg -n 'struct xxx' include lib platform`；
2. 再找写入点：谁初始化它、谁修改它、是否每 HART 独立；
3. 再找调用者：函数在哪个 cold/warm/platform/trap 阶段调用；
4. 最后查失败路径：返回什么 SBI error，是否会 hang、redirect 或 `mret`。

例如不理解 `ipi_type` 时：

```bash
rg -n 'ipi_type|sbi_ipi_send_many|sbi_ipi_process' \
  src/opensbi/lib/sbi/sbi_ipi.c src/opensbi/include/sbi
```

然后依次阅读发送、远端 scratch、raw interrupt、事件处理四个位置，不要只看发送函数。

## 6. 每次学习都留下四项记录

```text
我读了哪些文件/函数：
输入寄存器或结构体字段：
共享状态如何被修改：
失败时返回 error、hang 还是 redirect：
```

建议把记录放在实验模板中，再用 [labs.md](labs.md) 的编号组织。这样下一次继续时，
可以从上次最后一个已验证的函数开始，而不是重新浏览整个源码树。

## 7. 第一轮学完的明确标准

达到下面标准后，才算完成入门：

```text
[ ] 能画出 SPL -> OpenSBI -> U-Boot -> Linux
[ ] 能解释 fw_dynamic_info 六个字段
[ ] 能在 GDB 中断到 fw_boot_hart/sbi_init/sbi_hart_switch_mode
[ ] 能解释 a7/a6/a0-a5 和 a0/a1 返回协议
[ ] 能从 sbi_ecall.c 找到 TIME/IPI handler
[ ] 能说出 generic 和 fu740.c 的职责边界
[ ] 能解释 domain_check_addr_range 为什么存在
[ ] 能用 U-Boot sbi 命令观察 SBI capability
[ ] 能完成一个 SBIUnit 测试或 QEMU ecall 实验
```

完成后再进入 [security-model.md](security-model.md)、PMU、HSM 深入路径和新平台移植。
