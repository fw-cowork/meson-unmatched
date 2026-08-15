# HiFive Unmatched 裸机程序设计

## 1. 目的与范围

本目录实现一组由 HiFive Unmatched U-Boot `go` 命令调用的独立裸机程序。
当前板端应用包括 D2 RGB LED、FU740 板级测试和 M-mode CSR 检查；M-mode 检查还
通过 SiFive UART 驱动直接输出结果。公共层为后续风扇、GPIO、时钟和其他外设测试
提供复用边界。

本设计覆盖：

- U-Boot `go` 的调用约定和返回路径；
- RISC-V 入口汇编、BSS 初始化和固定加载地址；
- Meson 的目录分层、组件依赖和产物规则；
- FU740 SoC 时钟接口与 SiFive PWM、UART 驱动；
- 主机算法测试、镜像检查和应用扩展方法。

本设计不把裸机程序定义为复位后独立启动的固件，也不提供操作系统、libc、OpenSBI
调用或 U-Boot 内部符号 API。

## 2. 设计目标与约束

### 2.1 目标

1. 使用 U-Boot 已完成的 DDR、时钟、引脚复用和当前栈，快速加载并运行小型测试程序。
2. 公共代码按架构、SoC、驱动、库和应用分层，避免板级映射污染通用驱动。
3. 每个应用显式声明所需组件，未引用的组件不进入最终 ELF。
4. 同一份纯算法代码可以被 RISC-V 固件和主机测试分别编译。
5. 每个镜像同时生成调试和审计产物，并检查入口地址、重定位和镜像大小。

### 2.2 约束

- 加载格式是原始 BIN，不是 ELF；U-Boot `go` 不负责 ELF 解析和重定位。
- 镜像固定链接到 `0x84000000`，链接地址必须与 `tftpboot` 和 `go` 地址一致。
- 链接脚本限制完整镜像不超过 64 KiB。
- 目标 ABI 为 `rv64imafdc/lp64d`，程序以当前 U-Boot 的特权级运行。当前
  Unmatched U-Boot 在 S-mode，不能假设可以直接使用 M-mode 专用 CSR。
- 程序是 freestanding，不能依赖 libc、OpenSBI 服务或 U-Boot 私有函数。
- `baremetal_main()` 返回后，U-Boot 的栈、返回地址、必要寄存器和特权级必须仍然有效。
- MMIO 写入不会因为返回 U-Boot 自动回滚；PWM 等硬件可以在程序退出后继续运行。

## 3. 总体架构

```text
U-Boot go
    |
    | raw BIN at 0x84000000, a0=argc, a1=argv
    v
arch/riscv/cpu/start.S
    |
    | save ra/sp, clear .bss, call baremetal_main
    v
apps/<name>/main.c
    |
    +--> lib/                 freestanding utility
    +--> soc/fu740/           FU740 PRCI and addresses
    +--> drivers/pwm/         SiFive PWM math and MMIO
    +--> drivers/serial/      SiFive UART polled I/O
    |
    | return value in a0
    v
U-Boot command loop
```

Meson 的配置和构建依赖如下：

```text
meson.build
  -> baremetal/meson.build
       -> tools/                 build-machine helper programs
       -> arch/riscv/cpu/        startup + U-Boot go linker policy
       -> lib/                   static libraries
       -> soc/fu740/             FU740 static library
       -> drivers/pwm/           PWM static library + math source dependency
       -> drivers/serial/        UART static library
       -> tests/                 native host tests
       -> apps/                  application manifests
```

依赖只能由上层指向下层：应用可以依赖 SoC、驱动和库；SoC 与驱动只能依赖
`include/baremetal/` 的基础 API；架构入口不依赖任何外设实现。

## 4. 目录与职责

| 目录 | 职责 | 输出或接口 |
|---|---|---|
| `arch/riscv/cpu/` | U-Boot `go` 入口、链接布局、启动依赖 | `baremetal_uboot_go_dep` |
| `soc/fu740/` | FU740 PRCI 时钟计算和 SoC 地址 | `baremetal_fu740_dep` |
| `drivers/pwm/` | SiFive PWM 计算和寄存器操作 | `baremetal_sifive_pwm_dep` |
| `drivers/serial/` | SiFive UART 配置、轮询收发和输出辅助函数 | `baremetal_sifive_uart_dep` |
| `lib/` | 不依赖硬件的 freestanding 工具 | `baremetal_string_dep` |
| `apps/` | 命令解析、板级通道映射、板级测试和应用清单 | `baremetal_apps` |
| `tests/` | 不访问 MMIO 的主机测试 | `baremetal-test`、Meson test suite |
| `tools/` | `objcopy`、`nm`、`readelf` 和检查脚本发现 | 构建辅助变量 |
| `include/` | 按命名空间组织的公共头文件 | `baremetal/`、`soc/`、`drivers/` |

组件目录使用 Meson `static_library()` 构建实现，再用 `declare_dependency()` 导出
头文件、编译参数和链接输入。应用 manifest 只保存三项信息：输出名、应用私有源码
和组件依赖。

## 5. U-Boot `go` 运行时

### 5.1 调用协议

典型操作是：

```text
=> tftpboot 0x84000000 unmatched-led.bin
=> go 0x84000000 blink red
```

U-Boot 将命令地址转换为函数指针并执行：

```c
unsigned long entry(int argc, char *const argv[]);
```

对于上面的命令，入口看到的参数约定为：

```text
argc = 3
argv[0] = "0x84000000"
argv[1] = "blink"
argv[2] = "red"
```

RISC-V ABI 将 `argc` 放在 `a0`、`argv` 放在 `a1`。`start.S` 不改动这两个参数，直接
转交给 C 入口 `baremetal_main()`。

### 5.2 入口汇编

[`start.S`](../../baremetal/arch/riscv/cpu/start.S) 的执行顺序是：

1. `sp -= 16`，为保存现场保留 ABI 对齐的栈空间；
2. 将 U-Boot 提供的 `ra` 保存到 `8(sp)`；
3. 使用 `t0` 和 `t1` 清零链接脚本定义的 `__bss_start` 到 `__bss_end`；
4. `call baremetal_main`；
5. 恢复 `ra` 和 `sp`，保持 `a0` 中的返回值不变；
6. `ret` 回到 U-Boot 的 `do_go_exec()` 调用点。

保存 `ra` 是必要的，因为 `call baremetal_main` 会覆盖 `ra`。入口不初始化 DDR、栈、
页表、异常向量或中断控制器，这些状态属于调用它的 U-Boot 运行环境。

### 5.3 返回语义

`baremetal_main()` 返回的 `a0` 会成为 U-Boot `go` 命令的返回码。当前应用定义：

| 返回码 | 含义 |
|---:|---|
| `0` | 操作成功 |
| `2` | 参数数量或参数缺失 |
| `3` | 未知颜色 |
| `4` | 未知模式 |

返回只恢复 CPU 调用现场，不撤销对 PWM、GPIO、时钟或其他 MMIO 的修改。修改关键
CSR、破坏 U-Boot 栈、改变特权级或进入永久循环，都可能使返回失败。

## 6. 镜像与内存布局

[`u-boot-go.lds`](../../baremetal/arch/riscv/cpu/u-boot-go.lds) 定义唯一的加载区域：

```text
IMAGE_BASE = 0x84000000
IMAGE_SIZE = 64 KiB
```

链接布局为：

```text
0x84000000  .text              _start 必须位于此处
            .rodata
            .data
            .bss (NOLOAD)      __bss_start .. __bss_end
            __image_end
```

- `.text.entry` 使用 `KEEP()` 放在 `.text` 最前面，保证 BIN 第一条指令是 `_start`。
- `.bss` 属于镜像地址空间但标记为 `NOLOAD`，因此不占用原始 BIN 文件字节；入口汇编
  负责运行时清零。
- `.comment`、`.note`、`.eh_frame` 和 `.riscv.attributes` 被丢弃，避免裸机镜像携带
  无法执行或无用的运行时元数据。
- `ASSERT()` 检查 `_start` 地址和 64 KiB 大小上限。
- `--gc-sections` 删除未引用的函数和数据；`--no-relax` 与 `-mno-relax` 保证链接
  地址和入口检查保持确定性。

构建完成后，`check_uboot_go_image.sh` 使用 `readelf` 和 `nm` 检查：

```text
entry == _start == 0x84000000
ELF 中不存在 R_RISCV_* 或 .rela/.rel 重定位
```

## 7. Meson 构建设计

### 7.1 编译策略

`baremetal_target_dep` 集中携带 freestanding 编译选项：

```text
-ffreestanding -fno-builtin -fno-common
-fno-pic -fno-pie -fno-stack-protector
-ffunction-sections -fdata-sections
-mno-relax -msmall-data-limit=0
```

ISA、ABI 和 code model 由 [`cross/sifive-freedom-u-sdk.ini`](../../cross/sifive-freedom-u-sdk.ini)
提供。目标静态库设置 `pic: false`，应用统一使用 `-nostdlib`、`-static` 和项目链接脚本。

架构目录导出 `baremetal_uboot_go_dep`，其中包含：

- `baremetal-u-boot-go-startup` 静态库；
- `link_whole`，确保 `_start` 不会因归档库按符号裁剪而丢失；
- `-T u-boot-go.lds`、`--gc-sections`、`--no-relax` 等链接参数；
- 通用目标编译依赖。

因此应用 target 不需要列出 `start.S`、`.lds` 或 freestanding 链接选项。

### 7.2 应用产物

顶层应用循环为每个 manifest 生成一个 `name_suffix: 'elf'` 的 ELF，然后统一生成：

| Target | 产物 | 用途 |
|---|---|---|
| `<name>-bin` | `.bin`、`.check` | 下载到板端并验证 |
| `<name>-artifacts` | `.bin`、`.elf`、`.map`、`.dis`、`.sym`、`.check` | 完整分析 |
| `baremetal` | 所有应用的全部产物 | 聚合构建 |

每个 target 使用 Meson `build_subdir` 写入
`builddir/baremetal/<app-name>/`。`.map` 先写到该应用的 Meson 私有对象目录，再复制到
同一应用目录的稳定用户路径；其他分析文件通过 `capture: true` 写入对应输出。

### 7.3 主机测试

PWM 数学实现通过 `baremetal_sifive_pwm_math_dep` 以 source dependency 导出，不携带
RISC-V 编译选项。`tests/meson.build` 使用 `native: true`，所以：

```text
RISC-V 构建: sifive_pwm_math.c -> libbaremetal-sifive-pwm.a
主机测试:   sifive_pwm_math.c + sifive_pwm_math_test.c -> baremetal-pwm-math-test
```

`meson test --suite baremetal` 负责测试注册，`baremetal-test` 负责在 Ninja target 中
顺序运行所有主机测试。新增纯算法测试应同时加入这两个入口。

`./build.sh test` 是面向新开发机和 CI 的统一入口。它使用与正式构建相同的
`riscv64-freedomusdk-linux-*` 和独立的 `builddir-test/`，运行主机测试并构建
`unmatched-tests-artifacts`。新机器通过共享的 `populate_sdk` 安装器获得该工具链；
没有安装器时才从固定的 Freedom-U-SDK 源码生成。

需要 MMIO 或真实硬件的用例由 `unmatched-tests` 应用承载。U-Boot 作为控制端，
通过 `go 0x84000000 <test-name>` 选择测试，并从应用返回码获取结果。这类测试
不登记为默认 Meson test，避免普通构建依赖开发板。

## 8. SoC 与驱动设计

### 8.1 FU740 时钟

[`soc/fu740/clock.c`](../../baremetal/soc/fu740/clock.c) 读取 U-Boot 留下的 PRCI 状态：

1. 以 26 MHz HFCLK 作为输入；
2. 根据 `hfpclkpll_config` 的 `DIVR`、`DIVF`、`DIVQ` 计算 PLL 输出；
3. 根据时钟源选择和 bypass 位决定是否使用 PLL；
4. 根据 `hfpclk_divider + 2` 得到外设 PCLK。

应用不写死 PCLK，PWM 频率计算因此能适应不同 SPL/U-Boot 时钟配置。

### 8.2 SiFive PWM

PWM0/PWM1 的基地址由 [`include/soc/fu740.h`](../../baremetal/include/soc/fu740.h) 提供，
寄存器布局和算法由 [`drivers/pwm/`](../../baremetal/drivers/pwm/) 提供。一个 PWM 实例的
四个通道共享计数器和周期，驱动 API 要求调用者通过 `compare_mask` 明确声明要更新的通道。

`sifive_pwm_apply()` 的更新顺序是：

```text
停止 PWMCFG
  -> fence
  -> 清零计数器
  -> 更新 compare_mask 指定的 PWMCMP
  -> 写入 scale/zero_compare/pwmenalways
  -> fence
```

PWM 输出在 FU740 D2 连接上是反相的，比较值越小表示有效时间越长：

```text
compare = 0x0000  -> 全周期有效
compare = 0x8000  -> 约 50% 有效
compare = 0xffff  -> 视觉上关闭
```

自然回绕模式周期为 `2^(16 + pwmscale)` 个 PCLK；`pwmzerocmp` 模式使用通道 0
定义周期终点，算法将周期计数限制在 2 到 65536。

### 8.3 SiFive UART

FU740 只实现 UART0 和 UART1，基地址分别为 `0x10010000` 和 `0x10011000`；Unmatched
的 `stdout-path` 指向 UART0。通用驱动只接收实例基地址，不包含板级控制台选择。

U-Boot `go` payload 复用 U-Boot 已初始化的 UART0，只轮询 TXFIFO/RXFIFO，不重新配置
时钟、波特率、引脚复用或中断。驱动也提供 `sifive_uart_configure()`，供以后从复位入口
运行的程序按 `f_baud = f_in / (div + 1)` 独立初始化。

### 8.4 应用板级映射

`unmatched-led` 保持通用 PWM 驱动与 D2 映射分离：

| 颜色 | PWM0 通道 |
|---|---:|
| 红 | 2 |
| 绿 | 1 |
| 蓝 | 3 |

应用负责颜色组合、命令解析和模式选择；驱动不包含 D2、RGB 顺序或板级引脚知识。

## 9. 应用扩展流程

新增一个应用时：

1. 创建 `baremetal/apps/<name>/main.c`，实现 `baremetal_main()`。
2. 创建该目录的 `meson.build`，声明 `name`、应用源码和组件依赖。
3. 在 `baremetal/apps/meson.build` 加入 `subdir('<name>')`。
4. 运行 `./build.sh <name>-artifacts`。
5. 检查 `.check`、`.map` 和 `.dis`，确认入口、重定位和镜像大小。

新增公共组件时：

1. 将实现放入对应的 `lib/`、`soc/<chip>/` 或 `drivers/<ip>/` 目录；
2. 在本目录构建 `static_library()`，设置 `pic: false` 和 `baremetal_target_dep`；
3. 用 `declare_dependency(link_with: ...)` 导出 `<component>_dep`；
4. 只在应用 manifest 中加入实际需要的依赖；
5. 对不依赖 MMIO 的算法同步增加 `native: true` 主机测试。

新增架构或 SoC 时，应新增对应的分层 `meson.build` 和独立链接策略，不在应用目录
复制 `start.S` 或硬件寄存器实现。当前只有 `riscv`、`fu740` 和 U-Boot `go` 一种组合，
因此没有引入未实现的 board option 或多架构配置。

## 10. 运行风险与恢复策略

- `.bin` 必须加载到 `0x84000000`，不能随意改变 `go` 地址。
- 程序返回后，PWM 可能继续输出；测试结束建议执行 U-Boot `reset`。
- 不要覆盖 U-Boot 当前栈及其返回地址所在区域。
- 不要修改页表、特权级、trap 入口或关键 CSR 后直接返回。
- 不要把通用驱动中的地址常量替换为板级外设映射。
- 镜像超过 64 KiB、入口不是 `_start` 或出现重定位时，应让构建失败而不是尝试加载。

## 11. 验收标准

一个合格的裸机应用必须满足：

1. `./build.sh <name>-artifacts` 成功生成完整产物；
2. `.check` 报告 `entry == _start == 0x84000000` 且无重定位；
3. 链接脚本的 64 KiB `ASSERT()` 通过；
4. 不依赖 libc、OpenSBI 或 U-Boot 私有符号；
5. 若包含纯算法，`meson test --suite baremetal` 通过；
6. 在 U-Boot 中执行后能按约定返回 `rc`，或文档明确说明该应用会永久运行。
