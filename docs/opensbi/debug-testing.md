# OpenSBI 构建、调试与验证手册

本文把源码阅读变成可重复实验。所有命令都从仓库根目录执行；板上实验涉及 reset、
多核和 PMIC 时，先用 QEMU 或静态 ELF 分析确认预期。

## 1. 可复现基线

本仓库通过 `scripts/litebuild.py` 固定 OpenSBI revision、平台和链接地址：

```text
OPEN_SBI_REV = 74434f255873d74e56cc50aa762d1caf24c099f8
PLATFORM     = generic
FW_TEXT_START= 0x80000000
toolchain    = riscv64-freedomusdk-linux-
```

首次构建：

```bash
./build.sh toolchain       # 新机器准备 Freedom U-SDK 工具链
./build.sh fetch-sources
./build.sh check
./build.sh opensbi-fw
```

产物在 `out/opensbi/platform/generic/firmware/`，构建脚本会复制到 `deploy/`：

```text
fw_dynamic.elf / fw_dynamic.bin
fw_jump.elf    / fw_jump.bin
fw_payload.elf / fw_payload.bin
```

ELF 保留符号和 section，适合 GDB/objdump；BIN 只有加载字节，不能用来做符号断点。

## 2. ELF 三件套检查

```bash
file deploy/fw_dynamic.elf deploy/fw_dynamic.bin
riscv64-freedomusdk-linux-readelf -h deploy/fw_dynamic.elf
riscv64-freedomusdk-linux-readelf -S deploy/fw_dynamic.elf
riscv64-freedomusdk-linux-readelf -l deploy/fw_dynamic.elf
riscv64-freedomusdk-linux-nm -n deploy/fw_dynamic.elf | \
  rg '_start|fw_boot_hart|fw_platform_init|sbi_init|sbi_trap_handler|sbi_ecall_handler'
riscv64-freedomusdk-linux-objdump -drS deploy/fw_dynamic.elf | less
```

重点对照：

| 检查项 | 期望/问题 |
|---|---|
| ELF class/machine | RV64、RISC-V；错则工具链或目标错 |
| entry | 应落在 firmware entry 区域 |
| `.entry` | `_start`、`fw_dynamic` 早期代码 |
| `.text/.rodata` | RX，和 `_fw_rw_start` 前后的 PMP 边界一致 |
| `.data/.bss` | RW，入口会清 BSS |
| `.rela.dyn` | PIE firmware 的相对重定位记录 |
| `_fw_start/_fw_end` | 固件整体范围，不能覆盖前级仍在执行的内存 |

如果没有 `riscv64-freedomusdk-linux-*`，不要用宿主 `objdump` 猜 RISC-V relocation；
先执行 `./build.sh toolchain` 或显式指定工具链前缀。

## 3. QEMU 启动和 GDB

仓库的 QEMU 脚本会准备 debug stub：

```bash
# 终端 1
./qemu-gdb.sh --build

# 终端 2
gdb-multiarch deploy/qemu/fw_dynamic.elf
(gdb) target remote 127.0.0.1:1234
(gdb) set pagination off
(gdb) b fw_boot_hart
(gdb) b fw_platform_init
(gdb) b sbi_init
(gdb) b sbi_trap_handler
(gdb) b sbi_ecall_handler
(gdb) b sbi_hart_switch_mode
(gdb) c
```

### 3.1 入口断点要记录什么

在 `fw_boot_hart`：

```gdb
p/x $a0                 # 前级 boot hart 参数
p/x $a1                 # 前级 FDT 参数
p/x $a2                 # fw_dynamic_info 指针
x/6gx $a2               # RV64 六个字段
```

在 `fw_platform_init`：

```gdb
p/x $a0
p/x $a1
x/64bx $a1             # 仅确认 DTB magic，不要把二进制当字符串
```

在 `sbi_hart_switch_mode`：

```gdb
p/x next_addr
p/x next_mode
p/x next_arg1
p/x $mstatus            # gdb target 支持时
p/x $mepc
```

若 QEMU GDB 不支持 CSR 伪寄存器，直接在汇编 `csrr` 后查看临时寄存器，或在 C 结构体
中观察保存值。GDB 的 ELF 必须与 QEMU 实际加载地址匹配；先用 `info files` 检查。

### 3.2 追踪一次 ecall

从 S-mode caller 断在 `sbi_trap_handler` 后，按下面顺序单步：

```text
trap assembly 保存 regs
  -> sbi_trap_handler
  -> sbi_ecall_handler
  -> sbi_ecall_find_extension
  -> 具体 sbi_ecall_*.c handler
  -> regs->mepc += 4 / regs->a0,a1
  -> trap assembly 恢复 / mret
```

如果 `a7` 没有命中，检查 Base `PROBE_EXT`、Kconfig、`objects.mk` 和 extension 的
`register_extensions()`；如果命中但返回 `SBI_ENODEV`，检查平台 device 是否初始化。

## 4. SBIUnit：先测通用库，再上板

OpenSBI 的 SBIUnit 运行在 M-mode，适合纯算法和可 mock 的设备结构，不是 Linux 用户态
测试。官方示例位于 `src/opensbi/docs/writing_tests.md`。

### 4.1 开启测试

不要直接修改生成的 `out/` 配置后忘记记录。可以在独立 OpenSBI 构建目录开启：

```bash
cd src/opensbi
make PLATFORM=generic O="$OLDPWD/out/opensbi-sbiunit" \
     CROSS_COMPILE=riscv64-freedomusdk-linux- \
     FW_TEXT_START=0x80000000 \
     menuconfig
# 选择 CONFIG_SBIUNIT=y
make PLATFORM=generic O="$OLDPWD/out/opensbi-sbiunit" \
     CROSS_COMPILE=riscv64-freedomusdk-linux- \
     FW_TEXT_START=0x80000000 -j"$(nproc)"
make PLATFORM=generic O="$OLDPWD/out/opensbi-sbiunit" \
     CROSS_COMPILE=riscv64-freedomusdk-linux- run
```

`run` 需要当前 OpenSBI 版本的 QEMU 运行配置；若目标平台缺少 QEMU runner，使用仓库
`qemu-gdb.sh` 加载对应 ELF，或把 `CONFIG_SBIUNIT` 接入项目构建流程后再运行。

### 4.2 写一个测试

纯函数测试由 `include/sbi/sbi_unit_test.h` 的宏组成：

```c
static void strlen_test(struct sbiunit_test_case *test)
{
    SBIUNIT_EXPECT_EQ(test, sbi_strlen("Hello"), 5);
}

static struct sbiunit_test_case cases[] = {
    SBIUNIT_TEST_CASE(strlen_test),
    SBIUNIT_END_CASE,
};

SBIUNIT_TEST_SUITE(string_test_suite, cases);
```

然后在 `lib/sbi/tests/objects.mk` 加入 carray 和对象。修改 static function 时，
官方推荐在被测 `.c` 文件内 `#include "tests/..._test.c"`，避免导出测试专用符号。

### 4.3 测试层次

| 层次 | 例子 | 能证明什么 |
|---|---|---|
| SBIUnit | math/bitops/atomic/lock/ecall | 通用 C 逻辑和边界 |
| QEMU | timer/IPI/HSM/ecall/GDB | trap、寄存器、启动链 |
| baremetal S-mode | Base/Time/DBCN/console | ABI 和返回值 |
| Unmatched 串口 | banner、FDT、SBI 扩展 | 真实设备和内存布局 |
| Unmatched reset | reboot/shutdown | DA9063 I2C、板级复位 |

## 5. 真实板验证清单

### 5.1 不改代码的基线

记录完整串口日志和版本：

```text
OpenSBI version / commit
Platform Name / HART Count
Firmware Base / Size
Runtime SBI Version
Standard/Experimental Extensions
Domain0 regions
Handing off to next stage
```

同时保存 SPL 打印的 `next_addr`、`next_mode`、FDT 地址和 boot HART，确保两端协议
字段一致。

### 5.2 低风险改动顺序

1. 只改 `sbi_printf` 日志，确认编译和加载地址；
2. 改一个纯函数并用 SBIUnit 覆盖；
3. 在 QEMU 单步 trap/ecall；
4. 改 generic/FU740 hook，先验证 banner 不变；
5. 最后测试 HSM、多核 RFENCE 和 PMIC reset。

不要第一步就修改 `fw_base.S` 的 relocation 或 PMP；这类错误通常表现为“无串口、无
GDB、无错误码”，需要 JTAG 才能恢复现场。

## 6. 回归矩阵

| 改动 | 编译 | QEMU | QEMU GDB | Unmatched boot | Unmatched reset |
|---|---|---|---|---|---|
| ecall handler | 必须 | 必须 | 建议 | 必须 | 按扩展 |
| timer/IPI/TLB | 必须 | 必须 | 必须 | 必须 | 不适用 |
| FDT driver | 必须 | 有对应 DT 时 | 建议 | 必须 | 视设备 |
| FU740 PMIC | 必须 | 不一定 | 建议 | 必须 | 必须 |
| firmware 汇编/linker | 必须 | 必须 | 必须 | 必须 | 视启动链 |

## 7. 故障证据优先级

遇到失败时按以下顺序保留证据，而不是只描述“板子不亮”：

```text
构建命令 + git revision
ELF readelf -h/-l/-S 输出
SPL 的 entry/FDT/fw_dynamic_info 字段
OpenSBI 串口日志
GDB 断点处 PC/寄存器/CSR
是否单核/多核、是否 QEMU/真实板
复现所需镜像和 DTB
```

这样可以快速区分协议错误、链接地址错误、FDT/设备错误、trap 错误和板级供电/复位
问题。

相关文档：[firmware-boot.md](firmware-boot.md)、[platform-fu740.md](platform-fu740.md)。
