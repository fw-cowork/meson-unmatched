# HiFive Unmatched 裸机程序

本目录存放由 U-Boot `go` 命令调用的独立裸机程序。所有程序共用 Meson 构建
参数、启动代码和链接脚本，各自的源码与文档放在 `programs/<name>/` 中。

```text
baremetal/
├── meson.build                 程序清单和公共构建规则
├── common/                     所有程序共享的启动和硬件实现
│   ├── start.S                 U-Boot go 入口及 BSS 初始化
│   ├── baremetal.c             无 libc 字符串工具
│   ├── fu740.c                 FU740 运行时时钟计算
│   ├── sifive_pwm_math.c       PWM 频率和占空比换算
│   ├── sifive_pwm.c            PWM 寄存器配置
│   └── uboot-go.ld             内存布局和镜像大小检查
├── include/                    公共 API
│   ├── baremetal.h             基础类型、MMIO 和字符串工具
│   ├── fu740.h                 FU740 地址和外设时钟
│   └── sifive_pwm.h            PWM 频率、占空比和寄存器配置
└── programs/
    └── led/
        ├── main.c
        └── README.md
```

## 构建

裸机程序直接使用项目 Meson cross file 中声明的 RISC-V `c`、`as`、`ld` 和
`objcopy`，不再维护第二套 Makefile 或从 `PATH` 推断工具。ISA、ABI、code model
和 GCC 的 binutils 搜索目录由标准 `[built-in options]` 中的 `c_args`、
`c_link_args` 提供。`toolchain.sh setup` 会为仓库默认 SDK 准备对应目录。
默认配置见 `cross/sifive-freedom-u-sdk.ini`。构建全部程序：

```bash
./build.sh baremetal
```

只构建 LED 程序：

```bash
./build.sh unmatched-led-artifacts
```

`unmatched-led-bin` 仍可用于只生成板端加载的 `.bin`。

生成文件位于 Meson 构建目录：

```text
builddir/baremetal/unmatched-led.bin   U-Boot 加载的原始二进制
builddir/baremetal/unmatched-led.elf   调试符号和 ELF 元数据
builddir/baremetal/unmatched-led.map   GNU ld 链接映射
builddir/baremetal/unmatched-led.dis   带源码行的反汇编
builddir/baremetal/unmatched-led.sym   按地址排序的符号表
```

程序使用与当前 U-Boot 相同的 `rv64imafdc/lp64d` ABI，固定链接到
`0x84000000`。链接脚本检查完整运行时镜像不超过 64 KiB。

## 公共 API

`baremetal.h` 提供 `bm_u32`、`bm_ulong`、32 位 MMIO 读写、I/O fence、
`BM_ARRAY_SIZE()` 和不依赖 libc 的 `bm_streq()`。`fu740.h` 提供 PWM0/PWM1
基地址以及 `fu740_pclk_rate()`，后者读取 U-Boot 留下的 PRCI 配置并计算当前
外设时钟。

`sifive_pwm.h` 把与具体负载无关的 PWM 逻辑集中到公共层：

- `sifive_pwm_scale_for_frequency()` 为自然回绕模式选择最接近目标频率的
  `pwmscale`。
- `sifive_pwm_period_ticks_for_frequency()` 为 `pwmzerocmp` 模式计算 2 到
  65536 范围内的周期计数。
- `sifive_pwm_compare_for_fraction()` 把有效占空比分数转换成硬件使用的反相
  `pwmcmp` 值。
- `sifive_pwm_apply()` 先停止共享计数器，只更新 `compare_mask` 指定的通道，
  然后清零计数器并重新启动 PWM。

例如，在 PWM1 通道 2 上产生接近 20 kHz、25% 占空比的自然回绕波形：

```c
struct sifive_pwm_config config = { 0 };
bm_ulong pclk = fu740_pclk_rate();

config.scale = sifive_pwm_scale_for_frequency(pclk, 20000UL);
config.compare_mask = SIFIVE_PWM_CHANNEL(2);
config.compare[2] = sifive_pwm_compare_for_fraction(
	SIFIVE_PWM_NATURAL_PERIOD_TICKS, 1, 4);
sifive_pwm_apply(FU740_PWM1_BASE, &config);
```

一个 PWM 实例的四个通道共享周期和计数器。调用者必须确认自己可以改变该实例的
周期，并只在 `compare_mask` 中声明自己拥有的通道。启用 `zero_compare` 时，
通道 0 的 `compare[0]` 用作周期终点，不再是独立的普通输出。

## 添加程序

1. 新建 `baremetal/programs/<name>/main.c`，实现入口：

   ```c
   unsigned long baremetal_main(int argc, char *const argv[]);
   ```

2. 在 `baremetal/meson.build` 的 `baremetal_programs` 字典中登记输出名和程序
   私有源码。`common/` 中的公共实现会自动链接，Meson 也会通过编译器依赖文件
   跟踪头文件。例如：

   ```meson
   'unmatched-example': files(
     'programs/example/main.c',
     'programs/example/device.c',
   ),
   ```

3. 执行 `./build.sh baremetal`，或构建 `<输出名>-artifacts`。只需要板端原始镜像
   时可以构建 `<输出名>-bin`。

公共 `start.S` 保留 U-Boot 的返回地址，清零 `.bss`，调用 `baremetal_main()`，
再把返回值交给 U-Boot。链接器的 `--gc-sections` 会删除程序没有调用的公共函数，
所以新增通用模块不会强制增加每个镜像的大小。程序可以使用 U-Boot 当前栈，但
不能依赖 libc、OpenSBI 调用或 U-Boot 内部符号。若要修改统一加载地址或最大镜像
大小，只修改 `common/uboot-go.ld`。

当前示例及其板端运行方法见 [programs/led/README.md](programs/led/README.md)。
