# HiFive Unmatched 裸机程序

本目录存放由 U-Boot `go` 命令调用的独立裸机程序。架构入口、SoC、驱动、通用库和
应用分别管理；每个应用在自己的目录中声明需要链接的组件。
整体设计、运行时约束和扩展规则见
[裸机程序设计](../docs/architecture/baremetal-design.md)。

```text
baremetal/
├── meson.build                 公共编译选项、组件和产物规则
├── arch/riscv/cpu/             RISC-V 入口与链接布局
│   ├── start.S                 入口、BSS 初始化和返回现场
│   ├── start-standalone.S      私有栈和不返回入口
│   ├── u-boot-go.lds           可返回镜像的固定布局
│   └── u-boot-go-standalone.lds 私有栈镜像布局
├── lib/                        不依赖硬件的 freestanding 工具
│   └── string.c                无 libc 字符串工具
├── soc/fu740/                  FU740 SoC 代码
│   └── clock.c                 运行时时钟计算
├── drivers/pwm/                可复用的 SiFive PWM 驱动
│   ├── sifive_pwm_math.c       频率和占空比换算
│   └── sifive_pwm.c            PWM 寄存器配置
├── drivers/serial/             可复用的 SiFive UART 轮询驱动
│   └── sifive_uart.c           配置、收发和基础输出格式化
├── include/                    按层命名空间组织的公共 API
│   ├── baremetal/              类型、MMIO、字符串和应用入口
│   ├── drivers/                驱动接口
│   └── soc/                    SoC 接口
├── apps/                       板端应用及其 Meson 依赖清单
│   ├── unmatched-led/          D2 RGB LED 应用
│   ├── unmatched-tests/        U-Boot go 板级测试
│   ├── unmatched-mmode-check/  M-mode CSR 访问检查
│   └── unmatched-standalone/   私有栈、不返回检查
├── tests/                      可在主机运行的纯算法测试
└── tools/                      镜像检查等构建辅助工具
```

依赖只能由上层指向下层：应用可以使用 SoC、驱动和通用库；SoC 与驱动可以使用
`baremetal/io.h` 等基础接口；架构入口只负责 U-Boot 调用约定，不包含外设策略。
具体来说：

- `arch/riscv/cpu/` 拥有 RISC-V 入口、链接布局、BSS 初始化，以及返回或接管 U-Boot 的
  两种 ABI 处理。
- `soc/` 拥有 FU740 地址空间和 PRCI 等 SoC 专属逻辑，不处理 D2 等板级功能。
- `drivers/` 拥有 SiFive PWM/UART IP 的计算和寄存器操作，不写死 FU740 实例基地址。
- `apps/` 拥有命令行、D2 通道映射和测试模式，并显式选择链接组件。
- `tests/` 只在主机执行不访问 MMIO 的纯逻辑；镜像验证脚本归 `tools/`。

这种边界保证后续风扇、GPIO 或时钟测试可以复用底层实现，同时不会把某个应用的
板级映射塞进通用驱动。

Meson 的依赖方向与目录边界保持一致：`lib/`、`soc/` 和 `drivers/` 各自构建
`static_library()`，并通过 `declare_dependency()` 导出带头文件和编译选项的组件
依赖；`arch/riscv/cpu/` 把入口汇编和 freestanding 链接参数封装成同一个架构依赖，
应用目录只登记自己的源文件和需要的组件，顶层统一生成 ELF、BIN、MAP、反汇编和符号表。
这样新增应用不需要复制启动代码、链接参数或产物规则，未被应用引用的组件也不会进入
最终镜像。这个做法采用 Meson 官方推荐的内部依赖模式；构建规则按源码目录分散的方式
参考 QEMU，而启动代码、链接脚本和主机测试共同纳入 Meson 的方式参考 Picolibc：

- [Meson internal dependencies](https://mesonbuild.com/Dependencies.html#declaring-your-own)
- [QEMU Meson build](https://gitlab.com/qemu-project/qemu/-/blob/master/meson.build)
- [Picolibc source tree](https://github.com/picolibc/picolibc)

分层思路参考了 OpenSBI 的通用库、平台和固件拆分，以及 Freedom E SDK 的 target
BSP 与 portable HAL 分工。文件位置则跟随 U-Boot RISC-V：U-Boot/SPL 本体将
`start.S`、`u-boot.lds` 和 `u-boot-spl.lds` 放在 `arch/riscv/cpu/`。U-Boot 的
standalone 示例更简单，直接把应用 C 函数设为入口，通过 `-Ttext` 指定加载地址，
不提供通用入口汇编或 RISC-V 专用链接脚本。

本项目提供两套共享 runtime：`start.S` 保存 U-Boot 返回现场，
`start-standalone.S` 切换到镜像私有栈并禁止返回。两者都在进入 C 代码前清零 BSS，
对应链接脚本负责各自的 64 KiB 边界检查。这些文件放到 `arch/riscv/cpu/`，并通过
`u-boot-go*.lds` 明确它们属于 `go` payload，而不是 U-Boot 本体。当前仓库只有一个
SoC 和一种加载方式，没有照搬上游的完整构建框架：

- [OpenSBI source tree](https://github.com/riscv-software-src/opensbi)
- [Freedom E SDK documentation](https://sifive.github.io/freedom-e-sdk-docs/contents.html)
- [Freedom Metal documentation](https://sifive.github.io/freedom-metal-docs/)
- [U-Boot standalone examples](https://source.denx.de/u-boot/u-boot/-/tree/master/examples/standalone)

## 构建

裸机程序直接使用项目 Meson cross file 中声明的 RISC-V `c`、`as`、`ld` 和
`objcopy`，不维护第二套 Makefile 或从 `PATH` 推断工具。ISA、ABI、code model
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

只构建需要在 Unmatched 上运行的板级测试镜像：

```bash
./build.sh unmatched-tests-artifacts
```

只构建用于验证 U-Boot M-mode 调用链的 CSR 检查镜像：

```bash
./build.sh unmatched-mmode-check-artifacts
```

构建使用私有栈且不会返回 U-Boot 的 standalone 检查镜像：

```bash
./build.sh unmatched-standalone-artifacts
```

运行不需要开发板的公共算法测试：

```bash
./build.sh baremetal-test
```

该测试使用主机 C 编译器，覆盖 PWM scale 选择、`pwmzerocmp` 周期换算、占空比
换算和非法输入的边界行为。Meson 仍需要先用项目交叉编译配置完成初始化。

新开发机和 CI 也必须使用 `riscv64-freedomusdk-linux-*`。优先安装团队共享的
Yocto `populate_sdk` 安装器，再运行测试：

```bash
./build.sh toolchain /path/to/freedom-u-sdk-toolchain.sh
./build.sh test
```

没有共享安装器时，执行 `./build.sh toolchain` 从固定的 Freedom-U-SDK 2026.01.00
源码生成。`test` 使用独立的 `builddir-test/`，运行上述主机测试，并用与正式构建
相同的工具链交叉编译 `unmatched-tests` 和 `unmatched-standalone`，检查入口地址、
重定位和私有栈符号。MMIO 及不返回行为仍需在真实 Unmatched 上由 U-Boot `go` 执行。

`unmatched-led-bin` 仍可用于只生成板端加载的 `.bin`。

生成文件位于 Meson 构建目录：

```text
builddir/baremetal/unmatched-led/unmatched-led.bin   U-Boot 加载的原始二进制
builddir/baremetal/unmatched-led/unmatched-led.elf   调试符号和 ELF 元数据
builddir/baremetal/unmatched-led/unmatched-led.map   GNU ld 链接映射
builddir/baremetal/unmatched-led/unmatched-led.dis   带源码行的反汇编
builddir/baremetal/unmatched-led/unmatched-led.sym   按地址排序的符号表
builddir/baremetal/unmatched-led/unmatched-led.check 入口地址和重定位检查标记
```

每个应用都拥有 `builddir/baremetal/<app-name>/` 独立产物目录；`unmatched-tests` 使用
相同命名规则。具体命令、用例和返回码见
[`apps/unmatched-tests/README.md`](apps/unmatched-tests/README.md)。

程序使用与当前 U-Boot 相同的 `rv64imafdc/lp64d` ABI，固定链接到
`0x84000000`。可返回 runtime 允许代码、数据和 BSS 使用完整 64 KiB；standalone
runtime 把顶部 16 KiB 保留为私有栈，因此其他段最多使用前 48 KiB。构建产物检查还会
确认 `_start` 位于镜像首地址，且 ELF 中没有 `go` 无法处理的运行时重定位。

## U-Boot `go` 如何启动程序

`go` 不是 ELF 加载器，也不会像 `bootm` 那样解析镜像头、重定位程序或建立新的
运行环境。例如：

```text
tftpboot 0x84000000 unmatched-led.bin
go 0x84000000 blink red
```

`tftpboot` 先把原始 BIN 字节放到 DDR 的 `0x84000000`。`arch/riscv/cpu/u-boot-go.lds`
也把 `_start` 固定链接到该地址，并把 `.text.entry` 放在 BIN 开头。因此该地址
上的第一条指令就是 `_start`。当前 BIN 不是位置无关代码，装载地址、`go` 地址和
链接地址必须相同。

U-Boot `cmd/boot.c` 中的 `do_go()` 将命令行地址转换为函数指针，最终按下面的
C 函数类型调用它：

```c
unsigned long entry(int argc, char *const argv[]);
```

RISC-V 的 `do_go_exec()` 在调用入口前先执行 `cleanup_before_linux()`。FU740 的
实现会调用 `disable_interrupts()` 和 `cache_flush()`；后者使指令缓存失效并回写
数据缓存，使 CPU 能看到刚刚下载的代码。在当前 U-Boot 源码中，RISC-V
`disable_interrupts()` 本身是空实现，不应把这个调用链简化成“`go` 一定会关闭中断”。

`go` 会把命令中的地址字符串当作程序的 `argv[0]`。上面的示例中，入口收到的参数为：

```text
argc = 3
argv[0] = "0x84000000"
argv[1] = "blink"
argv[2] = "red"
```

RISC-V ABI 用 `a0` 传递 `argc`、`a1` 传递 `argv`。`start.S` 清零 `.bss` 时只使用
临时寄存器，因此可以原样把这两个参数交给 `baremetal_main()`。BIN 不包含链接脚本
中标记为 `NOLOAD` 的 `.bss`，所以这一步也保证未初始化的全局变量从零开始。

这个程序虽然是 freestanding bare-metal 程序，却不是从芯片复位后独立运行的完整
固件。它沿用 U-Boot 已经初始化的 DDR、时钟、引脚复用、当前栈和执行特权级。
默认 Unmatched U-Boot 配置为 S-mode，因此普通构建下这些程序也在 S-mode 执行。
独立的 `u-boot-mmode` 变体由 M-mode U-Boot 直接调用，payload 也随之在 M-mode
执行；详细构建、更新和验证步骤见
[U-Boot M-mode baremetal 实验](../docs/boot/uboot-mmode-baremetal.md)。

## 为什么 bare-metal 程序能返回 U-Boot

Bare metal 表示程序不依赖操作系统和 hosted C 运行时，不表示它不能被另一段程序当作
函数调用。`go` 的调用是同步的：U-Boot 等待入口函数返回，然后继续执行命令循环。

```text
U-Boot do_go_exec()
    |
    | entry(argc, argv)：ra 中保留返回 U-Boot 所需的继续地址
    v
_start
    | 把 ra 保存到 U-Boot 当前栈
    | call baremetal_main：ra 改为 _start 中 call 之后的地址
    v
baremetal_main()
    | ret：返回 _start，a0 中保留函数返回值
    v
_start
    | 恢复原来的 ra 和 sp
    | ret
    v
U-Boot do_go()/命令行
```

RISC-V 的 `ret` 本质是跳到 `ra` 保存的地址。`start.S` 必须先保存 U-Boot 给它的
`ra`，因为随后的 `call baremetal_main` 会覆盖 `ra`。栈下移 16 字节保持 ABI 要求的
对齐，返回前再完整恢复。编译器可以把 U-Boot 中的间接调用优化为尾调用，但这不改变
`_start` 入口时 `ra` 中有一个有效 U-Boot 继续地址这个 ABI 结果。

`baremetal_main()` 的 `unsigned long` 返回值留在 `a0`。`_start` 恢复现场时不改写
`a0`，所以 U-Boot 最终会打印这个值为 `rc`。`rc == 0` 时 `go` 命令成功，非零时
命令失败。

返回能力建立在程序遵守 ABI 和保留 U-Boot 运行环境的前提上。如果程序覆盖 U-Boot
的栈或返回地址、破坏必须保留的寄存器、改变特权级或关键 CSR、触发复位，或进入永久
循环，就无法正常返回。返回也不会回滚 MMIO 写入：例如 PWM 计数器会在 CPU 回到
U-Boot 后继续运行。对时钟、中断、缓存、页表或 trap 配置的改动也不会自动恢复；这类
测试完成后应重置开发板，再继续正常启动流程。

## 如何接管 hart 而不返回 U-Boot

应用 manifest 设置 `'runtime': 'standalone'` 后，构建系统会改用
`start-standalone.S` 和 `u-boot-go-standalone.lds`。入口立即把 `sp` 设置为
`0x84010000`；栈在 `0x8400c000..0x84010000` 内向低地址增长。C 入口即使意外返回，
汇编也只会进入永久循环，不会使用已经放弃的 U-Boot 栈或返回地址。

```meson
baremetal_apps += [{
  'name': 'my-standalone-test',
  'runtime': 'standalone',
  'sources': files('main.c'),
  'dependencies': [baremetal_sifive_uart_dep],
}]
```

这种模式只接管执行 `go` 的当前 hart；它仍依赖 U-Boot 已初始化的 DDR、时钟、串口和
引脚复用，不是复位入口固件。程序不会恢复 U-Boot 命令提示符，结束实验必须复位开发板。
可直接上板验证的实现见
[apps/unmatched-standalone/README.md](apps/unmatched-standalone/README.md)。

## 公共 API

`include/baremetal/` 提供基础类型、32 位 MMIO 读写、I/O fence、`BM_ARRAY_SIZE()`、
不依赖 libc 的 `bm_streq()` 以及应用入口约定。`include/soc/fu740.h` 提供 UART0/UART1、
PWM0/PWM1 基地址以及 `fu740_pclk_rate()`，后者读取 U-Boot 留下的 PRCI 配置并计算
当前外设时钟。
`include/drivers/sifive_pwm.h` 把与具体负载无关的 PWM 频率、占空比和寄存器配置接口
集中到公共层。

`include/drivers/sifive_uart.h` 提供 SiFive UART 的轮询接口。U-Boot `go` 应用可以直接
复用已初始化的 `FU740_CONSOLE_UART_BASE`，不要再次修改波特率；从复位入口运行的程序
才需要用 `fu740_pclk_rate()` 和目标波特率调用 `sifive_uart_configure()`。

```c
sifive_uart_puts(FU740_CONSOLE_UART_BASE, "value = ");
sifive_uart_put_hex_ulong(FU740_CONSOLE_UART_BASE, value);
sifive_uart_puts(FU740_CONSOLE_UART_BASE, "\n");
```

- `sifive_pwm_scale_for_frequency()` 为自然回绕模式选择最接近目标频率的
  `pwmscale`。
- `sifive_pwm_period_ticks_for_frequency()` 为 `pwmzerocmp` 模式计算 2 到
  65536 范围内的周期计数。
- `sifive_pwm_compare_for_fraction()` 把有效占空比分数转换成硬件使用的反相
  `pwmcmp` 值。
- `sifive_pwm_apply()` 先停止共享计数器，只更新 `compare_mask` 指定的通道，然后
  清零计数器并重新启动 PWM。

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
周期，并只在 `compare_mask` 中声明自己拥有的通道。启用 `zero_compare` 时，通道 0
的 `compare[0]` 用作周期终点，不再是独立的普通输出。

## 添加程序

可直接照做的完整目录模板、入口代码、依赖选择、构建检查和 TFTP 上板步骤见
[添加 bare-metal 测试程序](ADDING-APP.md)。下面保留扩展规则摘要。

1. 新建 `baremetal/apps/<name>/main.c`，实现入口：

   ```c
   #include <baremetal/app.h>

   bm_ulong baremetal_main(int argc, char *const argv[])
   {
           /* argv[0] 是 U-Boot go 命令中的入口地址字符串。 */
           (void)argc;
           (void)argv;
           return 0;
   }
   ```

2. 在应用目录增加 `meson.build`，登记输出名、私有源码和组件依赖；再在
   `baremetal/apps/meson.build` 增加 `subdir('<name>')`。例如：

   ```meson
   baremetal_apps += [{
     'name': 'unmatched-example',
     'sources': files('main.c', 'device.c'),
     'dependencies': [baremetal_string_dep],
   }]
   ```

   需要 FU740 寄存器、PWM 或 UART 时，再显式加入 `baremetal_fu740_dep`、
   `baremetal_sifive_pwm_dep` 或 `baremetal_sifive_uart_dep`。不使用的组件不会进入该程序
   的链接输入。

   默认 runtime 是 `return`。需要使用私有栈并永久接管当前 hart 时，在 manifest 中增加
   `'runtime': 'standalone'`；不要在应用目录复制入口汇编或链接脚本。

3. 执行 `./build.sh baremetal`，或构建 `<输出名>-artifacts`。只需要板端原始镜像时
   可以构建 `<输出名>-bin`。

链接器的 `--gc-sections` 会删除程序没有调用的公共函数，所以新增通用模块不会强制
增加每个镜像的大小。程序不能依赖 libc、OpenSBI 调用或 U-Boot 内部符号。加载地址或
镜像边界属于公共 runtime 策略，应同步审查 `arch/riscv/cpu/u-boot-go*.lds` 和镜像检查
脚本，不能在单个应用中覆盖。

当前示例及其板端运行方法见
[apps/unmatched-led/README.md](apps/unmatched-led/README.md)。需要验证 MMIO 和真实硬件的
测试程序见 [apps/unmatched-tests/README.md](apps/unmatched-tests/README.md)。

## 添加主机测试

把不依赖 MMIO 的待测逻辑写成独立源文件，在 `tests/meson.build` 中创建
`native: true` 的 executable，随后同时登记给 Meson test 和统一测试目标：

```meson
example_test = executable(
  'baremetal-example-test',
  files('../lib/example.c', 'example_test.c'),
  include_directories: baremetal_include,
  native: true,
  build_by_default: false,
)
test('baremetal-example', example_test, suite: 'baremetal')
baremetal_host_tests += [example_test]
```

这样 `meson test --suite baremetal` 和 `./build.sh baremetal-test` 都会执行新增测试。
