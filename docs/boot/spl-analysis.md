# HiFive Unmatched U-Boot SPL 代码解析

## 目录

1. [总体架构](#1-总体架构)
2. [启动流程详解](#2-启动流程详解)
3. [通用 SPL 框架 — `board_init_r`](#3-通用-spl-框架--board_init_r)
4. [FIT 镜像加载](#4-fit-镜像加载)
5. [跳转到 OpenSBI](#5-跳转到-opensbi)
6. [SPL 编译过程](#6-spl-编译过程)
7. [设计要点](#7-设计要点)

---

## 1. 总体架构

U-Boot SPL (Secondary Program Loader) 是 HiFive Unmatched 上电后的**第一级可编程引导程序**。ROM 中固化的 ZSBL (Zeroth Stage Boot Loader) 将 SPL 加载到 SRAM 后，SPL 在 **M-mode（Machine mode）** 运行，负责：

1. 初始化 DRAM
2. 初始化板载外设（PWM LED、风扇、PHY、USB 桥接器等）
3. 从 SPI Flash 或 SD 卡加载下一阶段镜像（OpenSBI + U-Boot proper + DTB 的 FIT 包）
4. 跳转到 OpenSBI，由 OpenSBI 在 S-mode 启动 U-Boot proper

### 关键内存地址布局（来自 `configs/sifive_unmatched_defconfig`）

| 地址 | 宏 | 用途 |
|---|---|---|
| `0x08000000` | `SPL_TEXT_BASE` | SPL 代码段基址（SRAM） |
| `0x80200000` | `CUSTOM_SYS_INIT_SP_ADDR` / `TEXT_BASE` | 初始栈顶 / U-Boot proper 入口 |
| `0x81cfe60` | `SPL_STACK` | SPL 栈顶（SRAM 内，SPL_TEXT_BASE ~ SPL_TEXT_BASE + 1MB 范围） |
| `0x84000000` | `SPL_LOAD_FIT_ADDRESS` | FIT 镜像加载缓冲地址（DDR） |
| `0x85000000` | `SPL_BSS_START_ADDR` | SPL BSS 段（DDR） |
| `0x100000` (1MB) | `SPL_MAX_SIZE` | SPL 最大尺寸限制 |
| `0x80000000` | `SPL_OPENSBI_LOAD_ADDR` | OpenSBI 加载地址 |

### 源文件清单

```
board/sifive/unmatched/spl.c          — 板级 SPL 初始化（外设复位初始化）
arch/riscv/cpu/fu740/spl.c            — FU740 专属 SPL（DDR 初始化、harts_early_init）
arch/riscv/cpu/fu740/dram.c           — DDR 控制器驱动
arch/riscv/lib/spl.c                  — RISC-V 通用 board_init_f / jump_to_image
arch/riscv/cpu/start.S                — 汇编入口（_start）
arch/riscv/cpu/u-boot-spl.lds         — SPL 链接脚本
common/spl/spl.c                      — SPL 通用框架（board_init_r）
common/spl/spl_fit.c                  — FIT 镜像解析与加载
common/spl/spl_opensbi.c              — OpenSBI 启动协议（fw_dynamic）
board/sifive/unmatched/hifive-platform-i2c-eeprom.c  — I2C EEPROM 读取（PCB 版本检测）
```

---

## 2. 启动流程详解

### 2.1 整体调用流程

```
ZSBL (ROM) → 加载 SPL 到 0x08000000
  │
  ▼
_start (arch/riscv/cpu/start.S)
  │
  ├─ harts_early_init()     (arch/riscv/cpu/fu740/spl.c)
  ├─ board_init_f_alloc_reserve() / board_init_f_init_reserve()
  ├─ board_init_f()          (arch/riscv/lib/spl.c)
  │   ├─ spl_early_init()    → DM/FDT/malloc/bootstage
  │   ├─ riscv_cpu_setup()   → FPU、缓存等
  │   ├─ preloader_console_init() → 串口
  │   └─ spl_board_init_f()  (board/sifive/unmatched/spl.c) ⬅ 核心初始化
  │
  ├─ spl_clear_bss
  ├─ spl_relocate_stack_gd()
  └─ board_init_r()          (common/spl/spl.c)
      ├─ boot_from_devices() → 从 SPI/SD 加载 FIT
      └─ spl_invoke_opensbi() → 跳转 OpenSBI
```

### 2.2 汇编入口 `_start` — `arch/riscv/cpu/start.S`

`_start` 标号是所有 RISC-V harts 的复位入口，关键步骤：

```
_start:
  mv   tp, a0         // tp = hart ID (来自 mhartid)
  mv   s1, a1         // s1 = DTB 指针（ZSBL 传入）
  mv   gp, zero       // 全局数据指针初始化为 0（防止早期异常误用）

  la   t0, trap_entry
  csrw tvec, t0       // 设置异常处理入口
  csrw ie, zero       // 屏蔽所有中断

  [SMP] 检查 tp < CONFIG_NR_CPUS，超出则 WFI 死循环

  li   t0, CONFIG_VAL(STACK)  // SPL 栈顶 = 0x81cfe60
  [SMP] sp = t0 - tp × stack_size  // 每 hart 独立栈
  [!SMP] sp = t0

  jal  board_init_f_alloc_reserve  → a0 = gd 基址
  mv   s0, a0                      // s0 = gd 指针
  jal  harts_early_init            // FU740 特性 CSR 初始化   ← [2.3]
```

#### SMP 同步机制 — hart lottery（原子抢锁）

```asm
  la   t0, hart_lottery
  li   t1, 1
  amoswap.w s2, t1, 0(t0)   // 原子交换：s2=旧值, [t0]=1
  bnez s2, wait_for_gd_init  // s2≠0 → 从 hart，等待 gd 初始化
  // s2=0 → boot hart，继续初始化
```

- boot hart (s2=0) 继续执行 gd 初始化、板级初始化
- 从 harts 跳转到 `wait_for_gd_init`，注册到 `available_harts` 位掩码后，进入 `secondary_hart_loop` WFI 等待 IPI

### 2.3 FU740 专属早期初始化 — `arch/riscv/cpu/fu740/spl.c`

```c
void harts_early_init(void)
{
    // CSR 0x7c1 = U74 Feature Disable Register
    // 写入 0 = 启用所有特性（分支预测、缓存等）
    // 仅在 M-mode 下执行（RISCV_MMODE=y 时）
    if (CONFIG_IS_ENABLED(RISCV_MMODE))
        csr_write(CSR_U74_FEATURE_DISABLE, 0);
}

int spl_dram_init(void)
{
    struct udevice *dev;
    // 通过 Driver Model 获取 RAM 设备并初始化 DDR
    // 实际实现在 arch/riscv/cpu/fu740/dram.c
    ret = uclass_get_device(UCLASS_RAM, 0, &dev);
    return ret;
}
```

### 2.4 RISC-V 通用 `board_init_f` — `arch/riscv/lib/spl.c`

```c
void board_init_f(ulong dummy)
{
    spl_early_init();          // DM 驱动模型、FDT、malloc、bootstage
    riscv_cpu_setup();         // RISC-V CPU 设置（FPU、缓存等）
    preloader_console_init();  // 串口控制台初始化
    spl_board_init_f();        // ⭐ 板级回调 → 2.5
}
```

### 2.5 板级初始化 — `board/sifive/unmatched/spl.c`

```c
int spl_board_init_f(void)
{
    // ① DDR 初始化（必须在任何 DRAM 访问之前完成）
    ret = spl_dram_init();

    // ② PWM 设备初始化（LED + 风扇）
    spl_pwm_device_init();

    // ③ 千兆以太网 PHY 复位（VSC8541）
    ret = spl_gemgxl_init();

    // ④ USB-PCIe 桥接芯片复位（ASM1042A）
    ret = spl_usb_pcie_bridge_init();

    // ⑤ USB Hub 复位（ASM1074）
    ret = spl_usb_hub_init();

    // ⑥ USB ULPI PHY 复位（USB3320C）
    ret = spl_ulpi_init();
}
```

#### ② PWM 初始化细节 (`spl_pwm_device_init`)

```
PWM0_BASE = 0x10020000  (3色LED + 1风扇)
  cmp0 = DISABLE         (不使用)
  cmp1 = ENABLE (0x0)    (红色)
  cmp2 = ENABLE (0x0)    (绿色)
  cmp3 = DISABLE         (蓝色)
  cfg  = 0x1000          (使能PWM)
  → LED 显示黄色 (红+绿)

PWM1_BASE = 0x10021000  (3路风扇控制)
  rev3 板: cmp1 = DISABLE (SoC风扇J21在rev3不可控)
  其他版本: cmp1 = ENABLE
  cmp2 = ENABLE (J23风扇)
  cmp3 = ENABLE (J24风扇)
  cfg  = 0x1000
```

#### ③-⑥ GPIO 复位序列

| 函数 | GPIO | 目标器件 | 复位脉冲宽度 | 稳定等待 |
|---|---|---|---|---|
| `spl_gemgxl_init` | GPIO12 | VSC8541 GbE PHY | 1µs | 15ms |
| `spl_usb_pcie_bridge_init` | GPIO7 | ASM1042A USB-PCIe | 3000µs | - |
| `spl_usb_hub_init` | GPIO11 | ASM1074 USB Hub | 100µs | - |
| `spl_ulpi_init` | GPIO9 | USB3320C ULPI PHY | 1µs | - |

复位实现方式：GPIO 置高 → 延时 → GPIO 拉低（有效复位）→ 维持指定宽度 → GPIO 拉高（释放复位）

#### 启动设备选择 — `spl_boot_device()`

```c
u32 spl_boot_device(void)
{
    // 读取 MSEL (Mode Select) 寄存器 0x1000 的低 4 位
    u32 mode_select = readl((void *)0x1000);
    u32 boot_device = mode_select & 0xf;

    switch (boot_device) {
    case 0x6: return BOOT_DEVICE_SPI;  // SPI Flash
    case 0xb: return BOOT_DEVICE_MMC1; // SD 卡
    default:  return BOOT_DEVICE_MMC1; // 默认回退 SD
    }
}
```

MSEL 的值由板上 DIP 开关设定，物理上决定了 ZSBL 从哪个设备加载 SPL，SPL 再从同一设备加载下一阶段。

---

## 3. 通用 SPL 框架 — `board_init_r`

汇编入口在 `spl_call_board_init_r` 处调用 `board_init_r`（位于 `common/spl/spl.c`）：

```
board_init_r()
  │
  ├─ spl_set_bd()              → 设置 board_info
  ├─ spl_init()                → 若 spl_early_init 未执行 DM，则在此补做
  ├─ timer_init()              → 定时器初始化
  ├─ bloblist_init()           → (若启用) Bloblist 传参机制
  ├─ [SOC_INIT] spl_soc_init() → SoC 级别初始化
  ├─ [PCI] pci_init()          → PCI 总线扫描（失败不致命）
  ├─ [BOARD_INIT] spl_board_init() → 板级 init
  │
  ├─ board_boot_order(spl_boot_list)
  │   └─ 调用 spl_boot_device() → BOOT_DEVICE_SPI 或 BOOT_DEVICE_MMC1
  │
  ├─ boot_from_devices()
  │   │  遍历 spl_boot_list，寻找匹配的 spl_image_loader
  │   │  调用 loader->load_image()
  │   └─ 最终调用 spl_load_simple_fit() [spl_fit.c]  ← 见第 4 章
  │
  ├─ spl_perform_arch_fixups()  → 架构级 fixup
  ├─ spl_perform_board_fixups() → 板级 fixup
  │
  └─ spl_image.os == IH_OS_OPENSBI → jumper = spl_invoke_opensbi  ← 见第 5 章
     jumper(&spl_image)
```

---

## 4. FIT 镜像加载

Unmatched 的 SPL 加载的是 **FIT (Flattened Image Tree)** 格式镜像。FIT 使用 `.its` (Image Tree Source) 描述镜像结构，编译为 `.itb` (Image Tree Blob) 烧写入 SPI Flash。

### 4.1 FIT 镜像结构

```
FIT Image (.itb)
  ├── /images
  │   ├── fw_dynamic.o     ← OpenSBI firmware (FW_DYNAMIC 类型)
  │   ├── u-boot.bin        ← U-Boot proper (loadable)
  │   └── dtb               ← 设备树 (fdt)
  └── /configurations
      └── config-1
           firmware = "fw_dynamic.o"
           loadables = "u-boot.bin"
           fdt = "dtb"
```

### 4.2 加载流程 — `spl_load_simple_fit()` (common/spl/spl_fit.c)

```
spl_load_simple_fit(spl_image, info, offset, fit_header)
  │
  ├─ spl_simple_fit_read()
  │   ├─ 计算 FIT 总大小 = ALIGN(fdt_totalsize(fit_header), 4)
  │   ├─ board_spl_fit_buffer_addr(size, 1, 1)
  │   │   └─ 优先尝试 malloc_cache_aligned(size)
  │   │      失败则回退 spl_get_load_buffer(0, size)
  │   └─ info->read(info, offset, size, buf) → 读取完整 FIT 到内存
  │
  ├─ spl_simple_fit_parse()
  │   ├─ fit_find_config_node() → 定位 /configurations 下的匹配节点
  │   ├─ [SIGNATURE] fit_config_verify() → 验证配置签名
  │   └─ 定位 /images 节点
  │
  ├─ 加载 firmware (OpenSBI)
  │   └─ load_simple_fit(info, fit_offset, ctx, fw_node, spl_image)
  │       ├─ fit_image_get_load() → OpenSBI 加载地址
  │       ├─ fit_image_get_data_offset() / get_data_position()
  │       │   ├─ 外部数据: info->read() 从设备读取
  │       │   └─ 嵌入数据: 直接在 FIT blob 中
  │       ├─ [GZIP/LZMA] 解压
  │       └─ fit_image_get_entry() → 获取入口地址
  │
  ├─ 附加 FDT
  │   └─ spl_fit_append_fdt(spl_image, info, offset, ctx)
  │       ├─ spl_fit_get_image_node(ctx, "fdt", 0)
  │       ├─ load_simple_fit() 加载 DTB
  │       ├─ [OVERLAY] 加载并应用 DT overlays
  │       └─ spl_image->fdt_addr = 设备树地址
  │
  ├─ 加载 loadables (U-Boot proper)
  │   └─ 遍历所有 loadables 条目
  │       load_simple_fit() 逐一加载到对应地址
  │
  └─ 设置 spl_image->flags |= SPL_FIT_FOUND
```

### 4.3 FIT 配置匹配

Unmatched 的 `board_fit_config_name_match()` 简单返回 0（使用第一个配置）：

```c
#ifdef CONFIG_SPL_LOAD_FIT
int board_fit_config_name_match(const char *name)
{
    return 0;  // 总是使用第一个 FIT 配置
}
#endif
```

---

## 5. 跳转到 OpenSBI

`common/spl/spl.c` 的 `board_init_r()` 中检测到 OS 类型后选择跳转路径：

```c
} else if (CONFIG_IS_ENABLED(OPENSBI) && os == IH_OS_OPENSBI) {
    jumper = &spl_invoke_opensbi;
}
```

### 5.1 `spl_invoke_opensbi()` — `common/spl/spl_opensbi.c`

```
spl_invoke_opensbi(spl_image)
  │
  ├─ 验证 fdt_addr 非空、8字节对齐
  │
  ├─ 定位 U-Boot proper 节点
  │   spl_opensbi_find_os_node(fdt, &node, IH_OS_U_BOOT)
  │   └─ 在 /fit-images 下遍历，找到 os="u-boot" 的节点
  │
  ├─ fit_image_get_entry(fdt, node, &os_entry)
  │   └─ 获取 U-Boot proper 入口地址
  │
  ├─ 准备 fw_dynamic_info 结构体:
  │   struct fw_dynamic_info opensbi_info = {
  │       .magic     = 0x4942534f,  // "OSBI"
  │       .version   = 2,           // FW_DYNAMIC_INFO_VERSION
  │       .next_addr = os_entry,    // ← OpenSBI 完成后跳转地址
  │       .next_mode = 1,           // PRV_S (S-mode)
  │       .options   = SPL_SCRATCH_OPTIONS,
  │       .boot_hart = gd->arch.boot_hart
  │   }
  │
  ├─ [SMP] smp_call_function(entry, dtb, &info, wait=1)
  │   └─ 向所有从 hart 发送 IPI，让它们也跳转到 OpenSBI
  │       wait=1 → 等待所有 hart 确认后才继续
  │       （防止 OpenSBI 重定位时还在 U-Boot SPL 代码区执行的冲突）
  │
  └─ opensbi_entry(gd->arch.boot_hart, dtb_addr, &info)
      → 跳转到 OpenSBI fw_dynamic 入口

OpenSBI 收到控制权后:
  1. 初始化自身（重定位、中断委托、定时器、IPI）
  2. 根据 next_addr 和 next_mode 在 S-mode 启动 U-Boot proper
  3. 传递设备树给 U-Boot proper
```

### 5.2 完整启动链

```
ZSBL (ROM, M-mode)
  → U-Boot SPL (M-mode)
    → OpenSBI (M-mode)
      → U-Boot proper (S-mode)
        → Linux (S-mode)
```

---

## 6. SPL 编译过程

### 6.1 构建系统架构

该项目的 U-Boot SPL 编译涉及三层构建系统：

```
meson.build (顶层编排)
  → scripts/litebuild.py
    → make -C src/u-boot sifive_unmatched_defconfig  (Kconfig 生成 .config)
    → make -C src/u-boot all                           (编译 U-Boot + SPL)
        → make obj=spl -f scripts/Makefile.xpl all     (独立编译 SPL)
```

#### 第一层：Meson/Ninja 编排

`meson.build` 定义了 `u-boot` 作为 `run_target`，实际由 `scripts/litebuild.py` 驱动：

```python
# litebuild.py 中大致逻辑：
# 1. 展开 defconfig → src/u-boot/.config
# 2. make olddefconfig (或 silentoldconfig)
# 3. make -j${JOBS} CROSS_COMPILE=... all
```

#### 第二层：U-Boot 顶层 Makefile

编译 `all` 目标时，检测到 `CONFIG_SPL=y` 后会触发 SPL 编译：

```makefile
# src/u-boot/Makefile:2366
spl/u-boot-spl: tools prepare $(if $(CONFIG_SPL_OF_CONTROL),dts/dt.dtb)
    $(Q)$(MAKE) obj=spl -f $(srctree)/scripts/Makefile.xpl all
```

生成的产物：
- `spl/u-boot-spl` — ELF 格式的 SPL（带调试符号）
- `spl/u-boot-spl-nodtb.bin` — objcopy 后的纯二进制（不含 DTB）
- `spl/u-boot-spl.bin` — 最终二进制（可能含 DTB）

#### 第三层：Makefile.xpl — SPL 专属构建

`scripts/Makefile.xpl` 管理 SPL 的完整编译流程：

### 6.2 编译宏定义

`Makefile.xpl` 开头定义了关键宏：

```makefile
# 自动添加 CONFIG_XPL_BUILD 和 CONFIG_SPL_BUILD 宏
KBUILD_CPPFLAGS += -DCONFIG_XPL_BUILD
ifeq ($(CONFIG_SPL_BUILD),y)
KBUILD_CPPFLAGS += -DCONFIG_SPL_BUILD
endif

# 编译阶段名称（SPL_ 前缀）
PHASE_ := SPL_

# 输出二进制名称
SPL_BIN := u-boot-spl
SPL_NAME := spl
```

这意味着所有 C 代码编译时会额外定义 `CONFIG_XPL_BUILD` 和 `CONFIG_SPL_BUILD` 宏。代码中用这些宏来条件编译：

- `CONFIG_IS_ENABLED(xxx)` → 检查 `CONFIG_SPL_xxx`
- `IS_ENABLED(CONFIG_xxx)` → 检查 `CONFIG_xxx`

例如，SPL 阶段和 U-Boot proper 阶段使用不同的源文件：
```makefile
# arch/riscv/cpu/fu740/Makefile
ifeq ($(CONFIG_XPL_BUILD),y)
obj-y += spl.o     # SPL: 只编译 spl.o (dram_init + harts_early_init)
else
obj-y += dram.o    # U-Boot proper: 编译完整的 dram.o + cpu.o
obj-y += cpu.o
endif
```

### 6.3 Kconfig 关键配置

从 `sifive_unmatched_defconfig` 生成的 `.config` 中，SPL 相关选项：

```kconfig
CONFIG_SPL=y                          # 启用 SPL 构建
CONFIG_SPL_TEXT_BASE=0x08000000       # SPL 链接地址
CONFIG_SPL_MAX_SIZE=0x100000          # SPL 最大 1MB
CONFIG_SPL_STACK=0x81cfe60            # SPL 栈
CONFIG_SPL_BSS_START_ADDR=0x85000000  # BSS 段在 DDR

CONFIG_SPL_MMC=y                      # 支持从 MMC/SD 启动
CONFIG_SPL_SPI_FLASH_SUPPORT=y        # 支持 SPI Flash
CONFIG_SPL_SPI=y                      # SPI 驱动
CONFIG_SPL_SPI_LOAD=y                 # SPI 加载
CONFIG_SPL_DM_SPI=y                   # SPI DM 驱动
CONFIG_SPL_DM_SPI_FLASH=y             # SPI Flash DM 驱动
CONFIG_SPL_GPIO=y                     # GPIO 驱动
CONFIG_SPL_LOAD_FIT=y                 # FIT 镜像加载
CONFIG_SPL_LOAD_FIT_ADDRESS=0x84000000 # FIT 加载地址
CONFIG_SPL_OPENSBI=y                  # OpenSBI 启动支持
CONFIG_SPL_SYS_MALLOC=y               # SPL 中 heap 分配
CONFIG_SPL_HAVE_INIT_STACK=y          # 有独立初始栈
```

`board/sifive/unmatched/Kconfig` 中 `BOARD_SPECIFIC_OPTIONS`：

```kconfig
select SIFIVE_FU740       # 选择 FU740 SoC 支持
select SUPPORT_SPL        # 声明支持 SPL
select BINMAN             # 使用 Binman 打包镜像
```

`sifive_fu740` (`arch/riscv/cpu/fu740/Kconfig`) 隐式选择了关键 SPL 功能：

```kconfig
select SPL_RAM if SPL                  # SPL RAM 驱动
imply SPL_OPENSBI                      # OpenSBI 启动
imply SPL_LOAD_FIT                     # FIT 加载
imply SPL_RISCV_ACLINT                 # ACLINT (中断控制器)
imply SPL_I2C                          # I2C 驱动（读 EEPROM）
```

### 6.4 编译优化选项

`Makefile.xpl` 中施加的编译/链接选项：

```makefile
# 每个函数/数据独立段，配合 link-time garbage collection 减小体积
KBUILD_CFLAGS += -ffunction-sections -fdata-sections
LDFLAGS_FINAL += --gc-sections

# 禁用栈保护（减小体积）
KBUILD_CFLAGS += -fno-stack-protector
```

### 6.5 链接脚本选择过程

`Makefile.xpl:168-192` 定义了链接脚本的查找优先级：

```
1. CONFIG_SPL_LDSCRIPT (如果定义)  → 用户自定义脚本
2. board/<VENDOR>/<BOARD>/u-boot-spl.lds     → 板级
3. arch/<ARCH>/cpu/<CPU>/u-boot-spl.lds       → 该 CPU
4. arch/<ARCH>/cpu/u-boot-spl.lds             → 该架构
```

对于 Unmatched (RISC-V FU740)，最终使用：
```
arch/riscv/cpu/u-boot-spl.lds
```

链接脚本在链接前先经 C 预处理器处理：

```makefile
$(obj)/u-boot-spl.lds: $(LDSCRIPT) FORCE
    $(call if_changed_dep,cpp_lds)
# 实际命令：$(CPP) $(LDPPFLAGS) -D__ASSEMBLY__ -x assembler-with-cpp -P -o $@ $<
```

预处理时注入的关键符号：

```makefile
LDPPFLAGS += \
    -DIMAGE_MAX_SIZE=$(CONFIG_SPL_MAX_SIZE)     # 0x100000
    -DIMAGE_TEXT_BASE=$(CONFIG_SPL_TEXT_BASE)   # 0x08000000
```

### 6.6 最终链接命令

```makefile
# 非 LTO 模式：
$(LD) $(KBUILD_LDFLAGS) $(LDFLAGS_$(SPL_BIN)) \
    arch/riscv/cpu/start.o          \    ← head-y (必须排第一！)
    --whole-archive                      \
        board/sifive/unmatched/built-in.a \
        arch/riscv/cpu/built-in.a        \
        arch/riscv/cpu/fu740/built-in.a  \
        arch/riscv/lib/built-in.a        \
        common/spl/built-in.a            \
        ...                              \
    --no-whole-archive                   \
    $(PLATFORM_LIBS)                     \
    -Map u-boot-spl.map -o u-boot-spl
```

其中 `LDFLAGS_$(SPL_BIN)` 包含：
```makefile
LDFLAGS_u-boot-spl += -T u-boot-spl.lds $(LDFLAGS_FINAL)
LDFLAGS_u-boot-spl += --no-dynamic-linker
LDFLAGS_u-boot-spl += --build-id=none
LDFLAGS_u-boot-spl += -Ttext $(CONFIG_SPL_TEXT_BASE)  # -Ttext 0x08000000
```

### 6.7 编译单元一览

SPL 中的 `.o` 文件来自以下目录：

```
arch/riscv/cpu/start.o                         ← 汇编入口（强制第一个）
arch/riscv/cpu/cpu.o, mtrap.o                  ← RISC-V 通用 CPU/异常
arch/riscv/cpu/fu740/spl.o                     ← FU740 SPL (dram_init, harts_early_init)
arch/riscv/lib/spl.o                           ← RISC-V board_init_f, jump_to_image
board/sifive/unmatched/spl.o                   ← 板级初始化
board/sifive/unmatched/hifive-platform-i2c-eeprom.o  ← EEPROM
common/spl/spl.o                               ← SPL 通用框架
common/spl/spl_fit.o                           ← FIT 加载
common/spl/spl_opensbi.o                       ← OpenSBI 启动
common/spl/spl_mmc.o / spl_spi.o               ← 启动设备加载
drivers/ram/sifive/sifive_ddr.o                ← FU740 DDR 驱动
drivers/spi/...                                ← SPI 驱动
drivers/gpio/...                               ← GPIO 驱动
drivers/i2c/...                                ← I2C 驱动
drivers/pwm/...                                ← PWM 驱动
dts/...                                        ← 设备树
```

### 6.8 编译流程图

```
make sifive_unmatched_defconfig
  → .config 生成 (Kconfig)
  → include/autoconf.mk 生成

make all
  │
  ├─ tools/ 编译 (mkimage 等)
  │
  ├─ U-Boot proper 编译
  │   arch/riscv/cpu/start.o
  │   board/sifive/unmatched/unmatched.o
  │   arch/riscv/cpu/fu740/dram.o, cpu.o
  │   ...
  │
  └─ SPL 编译 (obj=spl, Makefile.xpl)
      │
      ├─ -DCONFIG_XPL_BUILD -DCONFIG_SPL_BUILD
      │   所有源文件重新编译，条件编译选择 SPL 版本
      │
      ├─ 链接脚本: arch/riscv/cpu/u-boot-spl.lds
      │   预处理后: -DIMAGE_TEXT_BASE=0x08000000 -DIMAGE_MAX_SIZE=0x100000
      │
      ├─ LD → spl/u-boot-spl (ELF)
      │   链接地址 0x08000000, BSS 在 0x85000000
      │
      ├─ OBJCOPY → spl/u-boot-spl-nodtb.bin (纯二进制)
      │
      ├─ [size check] spl_size_limit 工具检查是否超过 SPL_MAX_SIZE
      │
      └─ → spl/u-boot-spl.bin (最终 SPL 二进制)
```

### 6.9 SPI Flash 布局

SPL 和下一阶段镜像在 SPI Flash 中的典型布局：

```
SPI Flash (ISSI, 32MB)
├─ 0x00000000: U-Boot SPL (spl/u-boot-spl.bin)  ← ZSBL 从此加载
├─ 0x00010000: env 环境变量 (可选)
├─ 0x00100000: FIT Image (.itb)
│              ├─ OpenSBI (fw_dynamic)
│              ├─ U-Boot proper
│              ├─ DTB
│              └─ (可选) Linux Kernel
└─ ...
```

---

## 7. 设计要点

### 7.1 多核同步

| 机制 | 实现 |
|---|---|
| **Boot Hart 选择** | 原子 `amoswap` 抢锁 (`hart_lottery`)，先到先得 |
| **从 Hart 等待** | `secondary_hart_loop` → WFI → 检查 IPI → `handle_ipi` |
| **Available Harts** | 位掩码 `available_harts`，每 bit 对应一个 hart |
| **OpenSBI 启动同步** | `smp_call_function(wait=1)` 等待所有从 hart 确认收到 |

### 7.2 PCB 版本兼容

`get_pcb_revision_from_eeprom()` 通过 I2C 读取 AT24C02 EEPROM：
- EEPROM 地址：0x54 (I2C Bus 0)
- 格式：SiFive 私有格式（magic: `f1 5e 50 45`）
- Rev3 板特殊处理：SoC 风扇 (J21) PWM 不可控，跳过其初始化

### 7.3 FIT 灵活性

使用 FIT 格式的优势：
- 一个镜像包包含 OpenSBI + U-Boot + DTB 全部组件
- 支持压缩（GZIP/LZMA）
- 支持签名验证（可选的 FIT_SIGNATURE）
- 支持 DT overlay（运行时修改设备树）

### 7.4 代码体积优化

| 技术 | 目的 |
|---|---|
| `-ffunction-sections -fdata-sections` + `--gc-sections` | 链接时丢弃未引用代码 |
| `CONFIG_XPL_BUILD` / `CONFIG_SPL_BUILD` | 条件编译，排除非 SPL 代码 |
| 源文件分叉 (`spl.o` vs `dram.o + cpu.o`) | SPL 仅链接必需的文件 |
| `FIT_IMAGE_TINY` | 精简 FIT 解析器 |

### 7.5 U-Boot proper 入口地址

`jump_to_image_linux` 的注释解释了 DTB 放置策略：

```
Originally, u-boot-spl will place DTB directly after the kernel,
but the size of the kernel did not include the BSS section, which
means u-boot-spl will place the DTB in the kernel BSS section
causing the DTB to be cleared by kernel BSS initialization.
Moving DTB in front of the kernel can avoid the error.
```

### 7.6 DM (Driver Model) 的 SPL 适配

SPL 中启用的 DM 子系统：
- `UCLASS_RAM` → DDR 初始化
- `SPI / SPI_FLASH` → 从 SPI Flash 读取下一阶段
- `GPIO` → 外设复位控制
- `I2C` → EEPROM 读取（PCB 版本检测）
- `CLK / RESET` → 时钟和复位控制
- `PWM` → LED 和风扇控制

---

## 附录: 关键文件索引

| 文件 | 作用 |
|---|---|
| `src/u-boot/arch/riscv/cpu/start.S` | RISC-V SPL/U-Boot 共用汇编入口 |
| `src/u-boot/arch/riscv/lib/spl.c` | RISC-V 通用 `board_init_f` + `jump_to_image` |
| `src/u-boot/arch/riscv/cpu/fu740/spl.c` | FU740: DDR init + harts_early_init |
| `src/u-boot/board/sifive/unmatched/spl.c` | 板级 SPL: GPIO/PWM 外设初始化 |
| `src/u-boot/common/spl/spl.c` | SPL 通用框架: `board_init_r` |
| `src/u-boot/common/spl/spl_fit.c` | FIT 镜像加载 |
| `src/u-boot/common/spl/spl_opensbi.c` | OpenSBI fw_dynamic 启动协议 |
| `src/u-boot/arch/riscv/cpu/u-boot-spl.lds` | SPL 链接脚本 |
| `src/u-boot/scripts/Makefile.xpl` | SPL 专属 Makefile |
| `src/u-boot/configs/sifive_unmatched_defconfig` | Unmatched defconfig |
| `src/u-boot/board/sifive/unmatched/Kconfig` | 板级 Kconfig（SPL_TEXT_BASE 等） |
| `src/u-boot/arch/riscv/cpu/fu740/Kconfig` | FU740 Kconfig（imply SPL 功能） |
| `src/u-boot/hifive-platform-i2c-eeprom.c` | I2C EEPROM（PCB 版本/MAC 地址） |

---

## 相关文档

- [U-Boot 启动日志与内核重定位分析](uboot-boot-log.md) — SPL 之后 U-Boot proper 阶段：PCIe/USB 枚举、extlinux 引导、内核解压与重定位
