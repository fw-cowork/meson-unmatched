# generic 平台与 FU740 适配详解

本项目不是为 Unmatched 从零写一个 `platform/`，而是编译 OpenSBI 的 `PLATFORM=generic`，
再通过 FDT 和 `platform/generic/sifive/fu740.c` 注入 FU740 的差异。理解这个选择，
比记住某个寄存器地址更重要。

## 1. generic 平台的设计

generic 平台把“硬件发现”和“通用 SBI 生命周期”分开：

```text
前级传入 FDT
  -> platform/generic/platform.c
     解析 model、/cpus、hart ID、heap、cold-boot-harts
  -> fdt_driver_init_by_offset(root)
     选择 platform override（FU740 在这里命中）
  -> lib/utils/fdt
     根据 compatible 初始化 serial/timer/ipi/irqchip/i2c
  -> lib/sbi
     使用 sbi_platform_operations 提供 SBI runtime
```

`struct sbi_platform` 保存版本、名称、HART 数、每 HART stack、共享 heap 和
`platform_ops_addr`。`struct sbi_platform_operations` 是动作表，包括：

```text
cold_boot_allowed / nascent_init / early_init / final_init
extensions_init / domains_init / pmu_init
irqchip_init / timer_init / mpxy_init
get_tlbr_flush_limit / get_tlb_num_entries
vendor_ext_provider / emulate_load/store / pmp_set/disable
```

平台初始化函数可以修改 `generic_platform_ops` 的函数指针，但不应复制一份通用
`sbi_init()`。这也是 FU740 文件只有约 260 行、却能复用整套 SBI runtime 的原因。

## 2. FDT 入口的执行顺序

### 2.1 `fw_platform_init()`

`platform/generic/platform.c:fw_platform_init()` 是 firmware 早期 C 入口，收到
`a0=hartid`、`a1=FDT`：

1. 找到 FDT root；
2. `fdt_driver_init_by_offset()` 匹配 root compatible，运行 platform override；
3. 读取 root `model` 写入 `platform.name`；
4. 枚举 `/cpus` 子节点，解析 hart ID 和 `status`，生成 `generic_hart_index2id[]`；
5. 从 CPU 节点获取 CBOM block size；
6. 从 `/chosen/opensbi,config` 获取 heap/coldboot 配置；
7. 保存 HART count，返回原始 FDT 指针。

FDT 解析失败会进入 `wfi` hang，而不是带着不完整 hart map 继续启动。调试时先检查
FDT 的 `/cpus`、`#address-cells`、`reg` 和 `status = "okay"`。

### 2.2 early/final init

`generic_early_init()` 在 cold boot 初始化 serial 和所有 early FDT drivers；随后
`fdt_cmo_init()` 处理 cache block management。`generic_final_init()` 做 FDT fixup、
CPU/domain fixup，并为 FDT 增加 `CONFIG_PLATFORM_GENERIC_FDT_EMPTY_SPACE` 空间。

这样设备 driver 不需要在入口阶段一次性全部初始化：串口要尽早可用，PMU、PLIC、timer
等可以按 `sbi_init()` 的生命周期初始化。

## 3. FU740 override 做了什么

`platform/generic/sifive/fu740.c` 注册以下 compatible：

```text
sifive,fu740
sifive,fu740-c000
sifive,hifive-unmatched-a00
```

命中后，`sifive_fu740_platform_init()` 只修改两个 generic hook：

```c
generic_platform_ops.final_init = sifive_fu740_final_init;
generic_platform_ops.get_tlbr_flush_limit = sifive_fu740_tlbr_flush_limit;
```

这体现了 override 的边界：FU740 不重新实现 HART 枚举、UART、timer 或 ecall。

### 3.1 DA9063 PMIC reset

FU740 文件通过 FDT driver 匹配 `dlg,da9063`：

1. 读取 PMIC 节点的 I2C 地址；
2. 找到父节点对应的 `struct i2c_adapter`；
3. 注册 `da9063_reset_i2c` 到 `sbi_system_reset` device list；
4. reset 时先 sanity check page/device ID `0x61`；
5. shutdown 写 `CONTROL_F.SHUTDOWN`；
6. cold/warm reboot 停 watchdog、写 `WAKEUP` 和电源控制位；
7. 完成后调用 `sbi_hart_hang()`，等待外部复位真正发生。

`sifive_fu740_final_init()` 只在 cold boot 调用 `fdt_driver_init_one()`，找不到 PMIC
会打印失败，但不会让通用固件的其它 SBI 服务消失。

### 3.2 CIP-1200 TLB errata

FU740 CIP-1200 存在“非 global `SFENCE.VMA` 可能没有刷新 I-TLB”的 errata。hook
返回 `0`，表示不使用按范围的优化阈值，要求 TLB 请求走全量
`SFENCE.VMA x0, x0` workaround。这个值最终被 `sbi_tlb.c` 使用，影响 RFENCE 的
本地处理策略。

它不是普通的“清 cache”开关：只影响 OpenSBI 代理的 TLB flush 语义，不会替 Linux
直接执行的本地 `sfence.vma`。

## 4. 设备所有权边界

| 设备/动作 | SPL | OpenSBI generic/FU740 | U-Boot proper | Linux |
|---|---|---|---|---|
| DDR 初始化 | 负责 | 不负责 | 使用 | 使用/管理 |
| FDT 传递/fixup | 产生/加载 | 解析、必要时修正 | 再次 fixup | 使用 |
| M-mode timer/IPI | 不提供 runtime | 提供 SBI 代理 | 通过 SBI | 通过 SBI |
| DA9063 reset | 可能有早期 GPIO | SBI SRST/PMIC I2C | 调用 reset | reboot/shutdown |
| PCIe PHY/枚举 | 板级复位准备 | 不负责 | 负责枚举 | 驱动/资源管理 |
| 串口 | SPL console | OpenSBI console | U-Boot console | tty driver |

不要把 U-Boot 的 PCIe 初始化搬进 OpenSBI：这会扩大 M-mode 固件、改变域权限，并
让 Linux/U-Boot 对硬件 ownership 产生竞争。只有下一级无法安全完成的 M-mode 服务
才适合进入 OpenSBI。

## 5. 新平台/新板卡的移植决策

优先级建议：

1. 先确认现有 generic FDT driver 已覆盖 UART/timer/PLIC/CLINT/PLIC/PMIC；
2. 只增加 FDT compatible 或平台 override；
3. 若硬件同类但寄存器不同，新增 `lib/utils` device，而不是在 SBI ecall handler
   加 `if (soc == ...)`；
4. 只有 reset、TLB errata、特殊 PMP/PMU 等无法由 FDT 表达的行为，才新增平台 hook；
5. 最后才考虑独立 `platform/<vendor>/<board>`，并遵守官方 platform guide 的目录、
   Kconfig、objects.mk 和 platform.c 契约。

## 6. FDT 检查清单

启动异常时，将实际传入 OpenSBI 的 DTB 导出，用 `fdtget`/`fdtdump` 检查：

```text
/model
/cpus/*/reg, status, compatible
/chosen/opensbi,config/heap-size
/chosen/opensbi,config/cold-boot-harts
timer / clint / aclint-mswi / plic / imsic
serial compatible + reg + clocks
i2c controller + dlg,da9063 child
memory nodes 和 reserved-memory
```

常见错误是 `/cpus` 的 hart ID 与 OpenSBI 预期不连续；generic 会建立 index->ID 表，
但调用者传的是真实 hart ID，二者不能混用。另一个错误是 PMIC 节点存在却没有父 I2C
adapter 的 compatible，导致 `fdt_i2c_adapter_get()` 失败。

## 7. 平台调试方法

```gdb
b fw_platform_init
b fdt_driver_init_by_offset
b sifive_fu740_platform_init
b sifive_fu740_final_init
b da9063_reset_init
b sifive_fu740_tlbr_flush_limit
```

在 `fw_platform_init` 观察 `platform.hart_count`、`generic_hart_index2id[]` 和
`platform.heap_size`；在 PMIC 初始化处观察 `nodeoff`、I2C adapter 和 `da9063.reg`。
如果 SRST 仍返回不支持，顺着 `sbi_ecall_srst.c -> sbi_system_reset.c -> device`
链表检查“未注册”还是“注册但 check 返回 0”。

## 8. 与官方文档的对应关系

- [Platform Support Guideline](https://github.com/riscv-software-src/opensbi/blob/master/docs/platform_guide.md)：
  平台目录和 hook 契约；
- [Platform Requirements](https://github.com/riscv-software-src/opensbi/blob/master/docs/platform_requirements.md)：
  firmware 入口、FDT、HART 和设备要求；
- [Domain Support](https://github.com/riscv-software-src/opensbi/blob/master/docs/domain_support.md)：
  memory region、PMP 和多 domain；
- [Library Usage](https://github.com/riscv-software-src/opensbi/blob/master/docs/library_usage.md)：
  外部 firmware 如何链接 `libsbi.a`/`libplatsbi.a`。

下一步阅读：[debug-testing.md](debug-testing.md)。
