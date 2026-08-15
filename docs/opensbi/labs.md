# OpenSBI 动手实验手册

这组实验按风险从低到高排列。每个实验都要求留下“输入、观察点、结论”三类记录，
这样读源码不会退化成只看启动日志。实验默认使用仓库固定的 OpenSBI revision 和
`riscv64-freedomusdk-linux-` 工具链。

## 实验总览

| 编号 | 主题 | 是否修改固件 | 主要证据 |
|---:|---|---|---|
| 0 | 固件基线和版本 | 否 | ELF、SPL、OpenSBI banner |
| 1 | 链接脚本和重定位 | 否 | `readelf`、`objdump`、relocation symbol |
| 2 | FW_DYNAMIC 交接 | 否 | SPL 结构体、DTB、`fw_boot_hart` |
| 3 | `mret` 回到 U-Boot | 否 | `mstatus.MPP/mepc/a0/a1` |
| 4 | Base/扩展探测 | 否 | U-Boot `sbi` 命令、SBI ABI 返回值 |
| 5 | Timer/IPI/RFENCE | 否 | QEMU GDB、S-mode 行为、多核日志 |
| 6 | SBIUnit | 是配置/测试 | 测试 suite 输出 |
| 7 | FU740 reset device | 可选 | DA9063 FDT/I2C/reset 证据 |
| 8 | 安全的 vendor extension | 是源码 | `PROBE_EXT`、错误码、回归测试 |

## 实验 0：建立固件基线

### 目标

回答“我现在调试的到底是哪一个 firmware、哪一个 commit、哪一个链接地址”。

### 操作

```bash
./build.sh fetch-sources
./build.sh check
./build.sh opensbi-fw
git -C src/opensbi rev-parse HEAD
git status --short
file deploy/fw_dynamic.elf
riscv64-freedomusdk-linux-readelf -h deploy/fw_dynamic.elf
```

### 观察点

```text
OpenSBI revision = 74434f255873d74e56cc50aa762d1caf24c099f8
PLATFORM = generic
FW_TEXT_START = 0x80000000
输出包含 fw_dynamic/fw_jump/fw_payload 的 ELF 和 BIN
```

记录真实板串口中的 `Firmware Base/Size`、`Platform HART Count`、SBI version 和
extension 列表。若这些值之后变化，先解释构建输入变化，再解释代码变化。

## 实验 1：理解链接脚本和重定位

### 目标

把 linker symbol 对应到 `fw_base.S` 使用的地址计算，理解为什么 ELF 不能被任意搬移。

### 操作

```bash
riscv64-freedomusdk-linux-readelf -S deploy/fw_dynamic.elf
riscv64-freedomusdk-linux-readelf -l deploy/fw_dynamic.elf
riscv64-freedomusdk-linux-nm -n deploy/fw_dynamic.elf | \
  rg '_fw_start|_text_start|_text_end|_fw_rw_start|_fw_end|_start|_start_warm'
riscv64-freedomusdk-linux-readelf -r deploy/fw_dynamic.elf
riscv64-freedomusdk-linux-objdump -drS deploy/fw_dynamic.elf | \
  rg -n -C 4 'relocate|R_RISCV_RELATIVE|_fw_start|_fw_end'
```

### 要画出的图

```text
FW_TEXT_START
  [.entry/.text]
  [.rodata/.dynsym/.rela.dyn]
  aligned _fw_rw_start
  [.data/.bss]
aligned _fw_end
```

说明 text/rodata 与 data/bss 为什么分开、`_fw_start/_fw_end` 如何成为固件范围、
以及 relocation delta 为什么不能只修改 `mepc` 而忽略 `R_RISCV_RELATIVE`。

## 实验 2：验证 FW_DYNAMIC 交接

### 目标

把 `src/u-boot/common/spl/spl_opensbi.c` 的结构体字段和 OpenSBI 汇编读取对应起来。

### 源码阅读顺序

```text
u-boot/common/spl/spl_opensbi.c:spl_invoke_opensbi
u-boot/include/opensbi.h
opensbi/include/sbi/fw_dynamic.h
opensbi/firmware/fw_dynamic.S:fw_boot_hart/fw_save_info
```

### 操作

先在 SPL 侧打开已有调试输出，或用 GDB 断在 `fw_boot_hart`：

```gdb
b fw_boot_hart
commands
  silent
  p/x $a0
  p/x $a1
  p/x $a2
  x/6gx $a2
  c
end
```

### 预期字段

```text
magic     = 0x4942534f
version   = 2
next_addr = FIT 中 U-Boot proper entry
next_mode = 1 (S-mode)
options   = SPL 配置值
boot_hart = gd->arch.boot_hart
```

故意把 magic 改成错误值只应在隔离镜像/仿真中进行；预期结果是 `_start_hang`，而不
是“跳到 U-Boot 但行为异常”。恢复时不要修改正在使用的生产 SD 镜像。

## 实验 3：观察 `mret` 返回 U-Boot

### 目标

证明“返回 U-Boot”是 CSR 配置加 `mret`，不是普通函数 return。

### 断点

```gdb
b sbi_hart_switch_mode
display/x next_addr
display/x next_mode
display/x next_arg1
si
```

在 `src/opensbi/lib/sbi/sbi_hart.c` 记录：

```text
mstatus.MPP = PRV_S
mstatus.MPIE = 0
mepc = next_addr
stvec = next_addr
sscratch = 0
sie = 0
satp = 0
a0 = hartid
a1 = FDT address
```

单步越过 `mret` 后，PC 应位于 U-Boot proper 的 entry，当前 mode 由 GDB target 或
U-Boot 启动输出确认。若 `mepc` 正确但立即 fault，检查 U-Boot entry 的可执行权限、
FDT 是否被覆盖和 linker/load address 是否一致。

## 实验 4：Base extension 和 U-Boot `sbi` 命令

### 目标

不修改 OpenSBI，观察 S-mode caller 如何使用 Base ABI 探测实现和扩展。

### 操作

在 U-Boot proper 命令行执行：

```text
sbi
```

源码路径：`src/u-boot/cmd/riscv/sbi.c`，它依次调用 `sbi_get_spec_version()`、
`sbi_get_impl_id()`、`sbi_get_impl_version()`、`sbi_probe_extension()` 和 CSR 查询。

### 对照源码

```text
U-Boot arch/riscv/include/asm/sbi.h
  -> ecall wrapper
OpenSBI lib/sbi/sbi_ecall_base.c
  -> GET_SPEC_VERSION / PROBE_EXT
OpenSBI lib/sbi/sbi_ecall.c
  -> a0=error, a1=value, mepc += 4
```

记录至少三类结果：存在的标准扩展、不存在的扩展、硬件 CSR 返回值。不要只复制
banner，因为 `sbi` 命令展示的是 caller 实际探测结果。

## 实验 5：Timer、IPI 和 RFENCE

### 5.1 Timer

在 QEMU GDB 中断在 `sbi_ecall_time_handler`，检查：

```text
a7 = 0x54494d45
a6 = 0
a0 = absolute next_event
```

继续跟踪 `sbi_timer_event_start()`，确认当前 QEMU/平台是走 `stimecmp` 还是
`timer_dev->timer_event_start()`。在 RV32 额外验证 `a1:a0` 拼接。

### 5.2 IPI

在 `sbi_ecall_ipi_handler` 和 `sbi_ipi_send_many` 断点：

```text
a0 = hart mask
a1 = hart mask base
target mask = 当前 domain 可中断 HART 与请求的交集
remote scratch.ipi_type 从 0 变为事件位时才触发底层 IPI
```

需要多 HART 环境；单 HART QEMU 只能验证参数检查，不能证明远端事件处理。

### 5.3 RFENCE

在 `sbi_ecall_rfence_handler` 观察 `struct sbi_tlb_info`，再跟踪：

```text
sbi_tlb_request
  -> per-HART FIFO
  -> IPI_TLB
  -> tlb_entry_local_process
  -> sync counter
```

特别记录 FU740 的 `get_tlbr_flush_limit()` 返回 0 如何强制全量 flush；不要把这个
实验误解成 Linux 页表代码本身发生了变化。

## 实验 6：SBIUnit

### 目标

先验证 M-mode 通用库，再做平台实验，减少“设备问题伪装成算法问题”。

### 操作

```bash
cd src/opensbi
make PLATFORM=generic O="$OLDPWD/out/opensbi-sbiunit" \
     CROSS_COMPILE=riscv64-freedomusdk-linux- \
     FW_TEXT_START=0x80000000 menuconfig
# 选择 CONFIG_SBIUNIT=y
make PLATFORM=generic O="$OLDPWD/out/opensbi-sbiunit" \
     CROSS_COMPILE=riscv64-freedomusdk-linux- \
     FW_TEXT_START=0x80000000 -j"$(nproc)"
make PLATFORM=generic O="$OLDPWD/out/opensbi-sbiunit" \
     CROSS_COMPILE=riscv64-freedomusdk-linux- \
     FW_TEXT_START=0x80000000 run
```

先运行 math/bitops/atomic/locks，再看 console/ecall。新增测试时：

1. 在 `lib/sbi/tests/` 建立 suite 和 case；
2. 在 `lib/sbi/tests/objects.mk` 注册 carray 与对象；
3. 先验证通过，再故意改变一个期望值验证失败输出；
4. 记录 suite 名、通过/失败数量和源码行号。

## 实验 7：FU740 DA9063 reset

### 目标

把“SRST extension 可用”与“PMIC 真的能让板子复位”拆成两个可验证问题。

### 先做静态检查

```text
platform/generic/sifive/fu740.c
  -> fdt_reset_da9063.match_table
  -> da9063_reset_init
  -> sbi_system_reset_add_device
lib/sbi/sbi_ecall_srst.c
  -> srst_available
  -> sbi_system_reset_supported
```

### 上板顺序

1. 先执行 U-Boot `sbi`，确认 SRST 被探测到；
2. 保存完整串口日志；
3. 再执行 U-Boot `reset`；
4. 观察 PMIC sanity check、watchdog 和重新出现的 SPL banner；
5. 最后验证 shutdown，准备串口/JTAG 断电恢复方案。

如果没有重新出现 banner，分别排查 FDT `dlg,da9063`、父 I2C adapter、PMIC device ID
`0x61` 和板级电源，而不是先修改 SBI dispatcher。

## 实验 8：新增安全 vendor extension

### 目标

实现一个只读、无硬件副作用的 vendor function，例如返回 OpenSBI build marker，练习
完整 extension 生命周期。

### 最小变更清单

```text
include/sbi/sbi_ecall_interface.h  # 组织分配的 vendor ID/function
lib/sbi/sbi_ecall_vendor_demo.c    # register + handle
lib/sbi/objects.mk                  # carray + object
lib/sbi/tests/ 或 baremetal caller   # success/error/probe
```

handler 必须：

- 拒绝未知 function 和非零保留参数；
- 不解引用任何 S-mode 指针；
- 检查当前 domain；
- 返回公开 SBI error，而不是内部 `SBI_ENODEV`；
- 通过 Base `PROBE_EXT` 验证注册状态。

完成后执行 `./build.sh opensbi-fw`、QEMU `sbi` 探测和至少一个错误参数测试，再考虑
板上验证。不要用 SRST、PMIC、HSM stop 作为第一个自定义 extension 实验。

## 实验记录模板

每个实验保留如下内容：

```text
实验编号/日期：
OpenSBI revision：
构建命令：
镜像/DTB：
源码断点：
关键寄存器或日志：
预期：
实际：
结论：
未解决问题：
```

关联速查：[sbi-abi-reference.md](sbi-abi-reference.md)、[debug-testing.md](debug-testing.md)。
