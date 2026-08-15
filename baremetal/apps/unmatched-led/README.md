# D2 RGB LED

`unmatched-led.bin` 由 U-Boot `go` 命令直接调用，用于控制 HiFive Unmatched
板载 D2 RGB LED。它支持常亮、PWM 硬件闪烁和两档频率的 50% 占空比亮度测试，
配置寄存器后返回 U-Boot。

## U-Boot 运行

把 `builddir/baremetal/unmatched-led/unmatched-led.bin` 放入 TFTP server root，然后执行：

```text
=> setenv serverip 192.168.1.23
=> setenv ipaddr 192.168.1.24
=> setenv netmask 255.255.255.0
=> tftpboot 0x84000000 unmatched-led.bin
=> go 0x84000000 red
## Starting application at 0x84000000 ...
## Application terminated, rc = 0x0
```

一次下载后可以重复执行 `go` 切换颜色：

```text
=> go 0x84000000 off
=> go 0x84000000 red
=> go 0x84000000 green
=> go 0x84000000 blue
=> go 0x84000000 yellow
=> go 0x84000000 cyan
=> go 0x84000000 magenta
=> go 0x84000000 white
```

原有的颜色命令保持为常亮，也可以显式指定 `solid`。使用 `blink` 会让指定颜色
以约 1 Hz、50% 占空比持续闪烁；程序返回 U-Boot 后，闪烁仍由 PWM 硬件继续：

```text
=> go 0x84000000 red
=> go 0x84000000 solid red
=> go 0x84000000 blink red
=> go 0x84000000 blink yellow
=> go 0x84000000 pwm-low red
=> go 0x84000000 pwm-high red
=> go 0x84000000 off
```

比较 PWM 频率对亮度的影响时，先执行 `pwm-low`，再执行 `pwm-high`。两种模式都
保持名义 50% 占空比；`pwm-low` 选择最接近 100 Hz 的自然回绕档位，`pwm-high`
则启用 `pwmzerocmp`，使用通道 0 定义一个约 10 MHz 的短周期。若 PCLK 为
260 MHz，两档实际约为 124 Hz 和 10 MHz。测试时使用同一种颜色并保持观察条件
不变；如果高频档仍无明显变化，说明这块板的 LED、MOSFET 和布线在该频率下仍能
维持接近相同的平均电流，可用示波器继续比较两档的实际占空比和 LED 电流。

高频档会临时占用 PWM0 通道 0 的 `pwmcmp0` 作为周期寄存器，因此可能改变 D12
绿色 LED 的状态；D2 RGB 使用通道 1、2、3，不受这个通道复用影响。测试完成后
执行 `reset` 可恢复 SPL 设置的 PWM0 状态。

返回码含义：

| 返回码 | 含义 |
|---:|---|
| `0` | 颜色设置成功 |
| `2` | 参数数量错误，或参数缺失 |
| `3` | 不支持的颜色名称 |
| `4` | 不支持的输出模式（支持 `solid`、`blink`、`pwm-low`、`pwm-high`） |

例如 `go 0x84000000 orange` 会返回 `rc = 0x3`，不会改变当前颜色。
`go 0x84000000 pulse red` 会返回 `rc = 0x4`。

## 实现链路

以 `go 0x84000000 blink red` 为例，完整执行过程是：

```text
U-Boot go
  -> _start：保存 U-Boot 返回地址、清零 BSS
  -> baremetal_main(argc=3, argv={"0x84000000", "blink", "red"})
  -> 模式解析：blink -> LED_MODE_BLINK
  -> 颜色表：red -> LED_RED 位图
  -> fu740_pclk_rate()：读取 PRCI 当前配置并计算 PCLK
  -> sifive_pwm_scale_for_frequency()：选择最接近 1 Hz 的 pwmscale
  -> sifive_pwm_compare_for_fraction()：把 1/2 占空比转换为 0x8000
  -> set_color()：组装 PWM0 通道掩码和三个 D2 通道的比较值
  -> sifive_pwm_apply()：停止、更新、清零并重新启动 PWM0
  -> 返回 0，U-Boot 打印 rc=0x0
```

U-Boot 会把入口地址保留为 `argv[0]`。兼容命令的颜色参数是 `argv[1]`；显式模式
命令使用 `argv[1]` 作为输出模式，`argv[2]` 作为颜色。颜色表先把名称
转换为与硬件无关的红、绿、蓝位图，
`set_color()` 再处理 D2 非连续且非 RGB 顺序的实际 PWM 通道。这样增加组合色时只需
组合逻辑颜色位，不会把板级通道编号散落到参数解析代码中。

PRCI 访问位于 `baremetal/soc/fu740/clock.c`，频率换算和 PWM MMIO 更新位于
`baremetal/drivers/pwm/`，对应接口位于 `baremetal/include/soc/` 和
`baremetal/include/drivers/`。LED 程序只负责 D2 的颜色、模式和板级通道映射，
后续风扇或其他 PWM 测试可以在自己的应用清单中显式复用 FU740/PWM 公共层。

## 硬件连接与寄存器

FU740 的 PWM0 地址范围从 `0x10020000` 开始，每个 `PWMCMPn` 是一个 16 位
比较值，偏移为 `0x20 + 4 * n`。D2 的实际连接如下：

| 颜色 | PWM0 通道 | 比较寄存器 | 地址 |
|---|---:|---|---:|
| 红 | 2 | `PWMCMP2` | `0x10020028` |
| 绿 | 1 | `PWMCMP1` | `0x10020024` |
| 蓝 | 3 | `PWMCMP3` | `0x1002002c` |

这个映射不是程序自行约定的，可以由三处板级资料相互印证：

- 原理图中 D2 的红、绿、蓝三路分别经过限流电阻和低边 MOSFET 接到
  `PWM0_2`、`PWM0_1`、`PWM0_3`。
- Linux 设备树 `src/linux/arch/riscv/boot/dts/sifive/hifive-unmatched-a00.dts`
  也把 D2 的红、绿、蓝声明为 PWM0 通道 2、1、3。
- U-Boot 的 `src/u-boot/board/sifive/unmatched/spl.c` 使用通道 1 和 2 点亮
  黄灯，并关闭通道 3，与红加绿得到黄光的结果一致。

PWM 比较器在缩放计数值 `pwms >= pwmcmp` 时输出高电平。因此一个完整的
16 位计数周期中，高电平比例约为：

```text
(0x10000 - pwmcmp) / 0x10000
```

D2 是共阳连接，颜色 LED 的阳极接 `VDD_3V3`，PWM 高电平使对应的低边
MOSFET 导通并产生电流，所以比较值越小越亮：

| `PWMCMPn` | PWM 输出 | D2 视觉效果 |
|---:|---|---|
| `0x0000` | 全周期为高 | 该颜色常亮 |
| `0x8000` | 半周期为高 | 该颜色以 50% 占空比闪烁或调光 |
| `0xffff` | 仅最后一个计数为高 | 该颜色视觉上关闭 |

`0xffff` 不是电气意义上的绝对 0% 占空比，而是每 65536 个计数仍有一个高电平
计数；FU740 PWM 硬件不能生成真正的 0% 高电平。这个窄脉冲不足以产生可见亮度，
U-Boot 的 PWM 驱动和 Unmatched SPL 因此也使用 `0xffff` 表示关闭。

三个比较寄存器设置完后，程序向 `PWMCFG` (`0x10020000`) 写入 bit 12
`pwmenalways` 和 `pwmscale`，让 PWM 计数器按选定周期持续运行。常亮模式使用
`pwmscale=0`；闪烁模式使用 `PWMCMP=0x8000`，并选择最接近 1 Hz 的 scale：

```text
PWM 频率 = PCLK / 2^(16 + pwmscale)
```

PCLK 不是写死的。程序读取 FU740 PRCI 的 `hfpclkpll_config`、时钟源选择和
`hfpclk_divider`，按照当前固件实际配置计算 PCLK，再比较 `pwmscale=0..15` 的
误差。这样更换 SPL 或 PLL 配置后，闪烁速度仍会尽量接近 1 Hz。所有选中颜色共享
同一个 PWM0 计数器和相同的 `0x8000` 比较值，因此黄色、青色、品红和白色的多个
分量会同步闪烁。

## 为什么直接写寄存器

`go` 只提供“按 C ABI 调用一个入口地址”的接口，并没有为外部二进制提供稳定的
U-Boot 驱动 API。直接访问已知的 FU740 MMIO 地址不依赖 U-Boot 内部符号和版本，
也不需要把驱动模型或 C 库链接进程序，最终二进制可以保持很小。

这种实现依赖 U-Boot 已完成 DDR、PWM 外设时钟和引脚复用初始化，所以它是
U-Boot 之下运行的板级测试程序，不是复位后可独立启动的完整固件。程序使用
freestanding C，并自行实现字符串比较，也是为了避免产生 `strcmp` 等未解析的
libc 依赖。

寄存器写入时先停止 PWM 并执行 I/O fence，确认后续寄存器访问排在停机操作之后；
然后清零计数器并设置三个 `PWMCMPn`，最后启用持续计数。这使闪烁从完整周期开始，
也避免首次启用 PWM 时短暂使用未知的比较值。末尾再执行一次
`fence iorw, iorw`，确保这些 MMIO 写入先于程序返回后的 I/O 操作被观察到。

程序不访问 PWM1，因此不会改变板上的风扇控制。常亮、闪烁和低频 PWM 模式也不
修改 PWM0 通道 0 的比较值；不过四个 PWM0 通道共享配置和计数器，改变周期仍会
改变 D12 的 PWM 周期和相位。10 MHz 模式例外：它会把通道 0 用作周期基准并启用
`pwmzerocmp`，所以不保持 D12 的状态。当前实现支持八种 RGB 组合、固定的
1 Hz/50% 闪烁，以及用于亮度对比的约 100 Hz 和 10 MHz 两档 50% PWM，不支持
自定义频率或单路亮度调节。

## `go` 入口为什么这样写

固定链接地址、U-Boot 调用链、`argc/argv` 传递、`ra` 保存以及
bare-metal 程序能够返回 U-Boot 的完整原理，见
[bare-metal 公共文档](../../README.md#u-boot-go-如何启动程序)。

对本 LED 程序来说，返回 U-Boot 只表示 CPU 恢复执行 U-Boot 命令循环，
不会撤销 PWM 寄存器写入；LED 会继续按设定模式工作。程序依赖
U-Boot 已完成 DDR、时钟和引脚复用初始化。完成硬件测试后建议执行
`reset`，再继续正常启动流程。`go` 能执行任意机器码，只应运行
自己构建并确认来源的二进制。
