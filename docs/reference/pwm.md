# FU740 PWM 原理、使用与学习方法

本文根据仓库中的 `freedom-u740-c000-manual-v1p7.pdf` 第 17 章，结合当前
U-Boot/Linux 驱动和 HiFive Unmatched 原理图，说明 FU740 PWM 的工作方式、常用
寄存器、U-Boot 使用方法，以及适合继续学习和验证的路径。

## 1. 先建立整体模型

PWM（Pulse Width Modulation，脉冲宽度调制）不是通过改变输出电压来调节功率，
而是让输出在一个固定周期内快速地开、关。周期不变时，输出为有效电平的时间
比例称为占空比：

```text
duty_cycle = active_time / period
```

例如，周期为 1 ms、有效时间为 0.25 ms，占空比就是 25%。对 LED 来说，人眼
通常把足够高频率的 PWM 平均成亮度；对电机、风扇或功率开关来说，占空比则影响
平均电流和平均功率。把 PWM 周期降低到约 1 Hz 时，人眼会直接看到亮灭，这就是
当前 bare-metal LED 程序的闪烁模式。

FU740 的 PWM 可以抽象成下面的数据流：

```text
HFPCLKPLL 外设时钟
        |
        v
  pwmcount 计数器  ----->  pwms 缩放计数值
        |                         |
        |                         +--> 与 pwmcmp0 比较 --> PWM0 输出
        |                         +--> 与 pwmcmp1 比较 --> PWM1 输出
        |                         +--> 与 pwmcmp2 比较 --> PWM2 输出
        |                         +--> 与 pwmcmp3 比较 --> PWM3 输出
        v
  自然回绕，或由 pwmzerocmp 触发周期重置
```

FU740 有两个 PWM 实例，每个实例默认有 4 个比较器、16 位比较精度：

| 实例 | 基地址 | 比较器 | 用途（Unmatched） |
|---|---:|---:|---|
| PWM0 | `0x10020000` | 0 到 3 | D12、D2 RGB LED |
| PWM1 | `0x10021000` | 0 到 3 | 板载风扇控制 |

一个 PWM 实例的 4 个通道共享计数器、缩放值和周期；每个通道只独立保存自己的
比较值。因此，4 个通道可以同时输出相同频率、不同占空比的波形，但不能在同一
实例内分别设置 4 个不同的 PWM 频率。

## 2. 计数器和 PWM 周期

### 2.1 `pwmcount`

`pwmcount` 位于基地址 `+0x08`。当 PWM 被使能时，计数器每个输入时钟周期加一。
手册描述的有效计数宽度是 `cmpwidth + 15` 位；FU740 的 `cmpwidth` 为 16，
所以实际使用 `pwmcount[30:0]`。

当不启用 `pwmzerocmp` 时，计数器超过有效宽度后自然回绕到 0。U-Boot SPL 和
bare-metal LED 程序的常亮、闪烁、低频 PWM 模式使用这种模式；LED 程序的
10 MHz 高频模式改用 `pwmzerocmp` 缩短周期。

### 2.2 `pwmscale` 和 `pwms`

`pwmcfg[3:0]` 是 `pwmscale`。手册定义它为 `pwmcount` 中 `pwms` 字段的起始
位。FU740 的 `pwms` 是 16 位，因此可理解为：

```text
pwms = (pwmcount >> pwmscale) & 0xffff
```

`pwms` 位于基地址 `+0x10`，也是比较器真正使用的计数值。增大 `pwmscale` 后，
比较值变化得更慢，PWM 周期变长、频率变低。

如果外设输入时钟为 `f_pwm`，自然回绕模式下：

```text
period_ticks = 2^(16 + pwmscale)
PWM 频率    = f_pwm / 2^(16 + pwmscale)
PWM 周期    = 2^(16 + pwmscale) / f_pwm
```

例如输入时钟为 260 MHz、`pwmscale=11` 时：

```text
period_ticks = 2^27
PWM 频率约为 1.94 Hz
```

实际频率必须使用当前固件提供给 PWM 的 HFPCLK 时钟计算。FU740 的外设时钟来自
HFPCLKPLL，U-Boot 驱动通过 clock framework 获取它；bare-metal 公共函数
`fu740_pclk_rate()` 读取 PRCI 配置计算 PCLK，`sifive_pwm_scale_for_frequency()`
再选择最接近目标频率的 scale。

### 2.3 `pwmzerocmp`

`pwmcfg[9]` 置 1 时，`pwms` 达到或超过 `pwmcmp0` 后，`pwmcount` 在下一个
周期被清零。此时 `pwmcmp0` 不再只是普通输出边沿，也可以用来定义整个 PWM
周期或产生定时中断。

常见选择如下：

| 模式 | `pwmzerocmp` | 周期来源 |
|---|---:|---|
| 自然回绕 | `0` | `pwms` 16 位溢出，周期为 `2^(16+scale)` 个时钟 |
| 比较器 0 定义周期 | `1` | `pwms` 达到 `pwmcmp0` 后重置 |

如果需要 4 个独立 PWM 输出，通常保留 `pwmzerocmp=0`，这样 `pwmcmp0` 也可以
作为普通通道使用。

## 3. 比较器和占空比

### 3.1 比较寄存器

比较器寄存器偏移如下：

| 寄存器 | 偏移 | 地址（PWM0） |
|---|---:|---:|
| `pwmcmp0` | `0x20` | `0x10020020` |
| `pwmcmp1` | `0x24` | `0x10020024` |
| `pwmcmp2` | `0x28` | `0x10020028` |
| `pwmcmp3` | `0x2c` | `0x1002002c` |

手册给出的原始比较关系是：当 `pwms >= pwmcmpX` 时，对应比较器结果为高。把
`pwmcmpX` 看成阈值，就能理解占空比的基本变化：阈值改变，输出边沿在周期中的
位置随之移动。

### 3.2 FU740 的实际输出极性

这里有一个容易混淆的细节。手册描述的是比较器的原始关系；从比较器到 GPIO 的
输出路径还包含中心模式和输出逻辑。FU740 的 U-Boot/Linux 驱动明确按“反相 PWM”
处理，Linux 驱动也注明硬件实际输出为反相关系，并在软件中把用户的 duty 反转后
写入比较寄存器。

因此在当前 Unmatched 固件约定中：

```text
pwmcmp = 0x0000  -> 通道全开（U-Boot 的 enable 值）
pwmcmp = 0x8000  -> 约 50% 占空比
pwmcmp = 0xffff  -> 通道关闭（视觉上约 0%）
```

这也是 U-Boot SPL 的定义：`0x0` 为 `PWM_CMP_ENABLE_VAL`，`0xffff` 为
`PWM_CMP_DISABLE_VAL`。因此调试板级 LED 时，应同时参考手册、驱动和原理图，
不要只根据原始比较器的 `pwms >= pwmcmp` 一条描述推断“LED 一定亮还是灭”。

### 3.3 Unmatched 的通道连接

Linux 设备树和板级代码给出了 D2 RGB LED 的实际通道：

| 颜色 | PWM0 通道 | 比较寄存器 |
|---|---:|---|
| D2 红色 | 2 | `pwmcmp2` |
| D2 绿色 | 1 | `pwmcmp1` |
| D2 蓝色 | 3 | `pwmcmp3` |
| D12 绿色 | 0 | `pwmcmp0` |

D2 的三个颜色通道共享 PWM0 的计数器和周期，因此把多个通道设置为 `0x8000`
时，它们会同步闪烁。切换 D2 的 PWM 周期也会改变 D12 通道 0 的计数频率，但
不会自动修改 D12 的比较值。

## 4. `pwmcfg` 完整结构

`pwmcfg` 位于基地址 `+0x00`：

| 位 | 名称 | 作用 |
|---:|---|---|
| 3:0 | `pwmscale` | 选择 `pwmcount` 到 `pwms` 的缩放位置 |
| 8 | `pwmsticky` | 保持比较器中断 pending 状态，避免软件还没处理就被清除 |
| 9 | `pwmzerocmp` | `pwmcmp0` 匹配后自动清零计数器 |
| 10 | `pwmdeglitch` | 在一个 PWM 周期内锁存比较器边沿，减少改寄存器时的毛刺 |
| 12 | `pwmenalways` | 持续运行 PWM |
| 13 | `pwmenoneshot` | 运行一个周期后自动停止 |
| 16:19 | `pwmcmpXcenter` | 对应通道使用中心对齐/相位正确波形 |
| 24:27 | `pwmcmpXgang` | 将相邻比较器组合成更复杂的波形 |
| 28:31 | `pwmcmpXip` | 比较器中断 pending 状态 |

当前常亮和闪烁实现最少只需要：

```text
PWMCFG = pwmenalways | pwmscale
PWMCMPx = duty 对应的比较值
```

更新正在运行的 PWM 时，推荐先清除 `pwmenalways`，清零 `pwmcount`，再写比较值
和新的配置，最后重新置位 `pwmenalways`。这样可以从一个完整周期开始，避免新旧
周期参数混合造成短暂毛刺。若不能停止 PWM，可以研究手册中的 `pwmdeglitch`。

## 5. 波形类型

### 5.1 左对齐/右对齐 PWM

普通比较器产生的是边沿随阈值移动的基本 PWM 波形。手册称其为 basic right-aligned
waveform；GPIO 端可以再做反相，以得到相反极性的 left-aligned waveform。

对于 LED 亮度控制，普通 PWM 通常已经足够。关键是确认：

1. 周期是否足够高，避免肉眼看到闪烁。
2. 占空比是否在负载允许范围内。
3. 板级 GPIO 或 MOSFET 是否改变了信号极性。

### 5.2 中心对齐/相位正确 PWM

每个通道都有一个 `pwmcmpXcenter` 位。置位后，`pwms` 的最高位为 1 时，比较器
使用按位取反后的计数值，形成关于周期中心对称的波形。它适合降低边沿集中造成的
噪声或电流突变。

代价是有效分辨率降低约一半：在同样的输入时钟和 16 位精度下，中心对齐周期的
最高频率低于普通边沿对齐 PWM。手册给出的例子是 16 MHz 时，16 位中心对齐约
244 Hz；降低到 8 位精度时可提高到约 62.5 kHz。

### 5.3 Ganging

`pwmcmpXgang` 可以把相邻比较器组合起来：一个比较器负责置位输出，另一个负责
清零输出，从而生成比单一阈值 PWM 更灵活的脉冲。需要精确脉冲位置、双边沿或
非标准波形时再使用此功能；学习时先掌握单比较器模式。

### 5.4 One-shot

置位 `pwmenoneshot` 后，PWM 运行一个完整周期；发生重置条件后该位由硬件清零，
不会自动开始第二个周期。它适合产生一次性精确脉冲，而不是持续的 LED 闪烁。

### 5.5 去毛刺和中断

`pwmdeglitch` 会把比较器在当前周期产生的有效边沿锁存到对应的 `pwmcmpXip`，
避免软件更新比较寄存器时在同一个周期内出现不完整的边沿。`pwmsticky` 可以防止
pending 状态被自动清掉。

设置 `pwmzerocmp=1` 并让 `pwmcmp0` 定义周期时，可以得到周期性 PWM 中断；也可以
不自动清零计数器，把比较器当作普通定时器边沿中断使用。

## 6. 在 U-Boot 中使用

当前 `sifive_unmatched_defconfig` 打开了 `CONFIG_CMD_PWM`。命令格式如下：

```text
pwm config <pwm_dev> <channel> <period_ns> <duty_ns>
pwm enable <pwm_dev> <channel>
pwm disable <pwm_dev> <channel>
```

所有参数使用十进制。Unmatched 上通常使用 PWM 设备 0：

```text
=> pwm config 0 2 10000000 5000000
```

这表示配置 PWM0 通道 2，目标周期 10 ms、目标有效时间 5 ms，即目标 100 Hz、
50% 占空比。U-Boot 驱动会根据当前 PWM 时钟按对数选择一个可实现的 `pwmscale`，
因为硬件只能以 2 的幂改变周期，实际周期会有量化误差；bare-metal 公共函数
`sifive_pwm_scale_for_frequency()` 则比较相邻 scale 的频率误差来选择近似值。

注意 `pwm config` 在当前驱动中会写入 `pwmenalways`，因此配置后通常已经开始
输出。`pwm enable` 和 `pwm disable` 是强制通道全开或关闭的快捷操作：

```text
=> pwm enable 0 2      # 写入 pwmcmp2 = 0，强制全开
=> pwm disable 0 2     # 写入 pwmcmp2 = 0xffff，强制关闭
```

如果希望保留 50% 占空比，不要在 `pwm config` 后再执行 `pwm enable`，因为它会
把比较值覆盖为全开值。需要重新设置占空比时，再执行一次 `pwm config`。

用 U-Boot 直接查看 PWM0 寄存器：

```text
=> md.l 0x10020000 0x0c
```

输出中应能看到：

```text
0x10020000  pwmcfg
0x10020008  pwmcount
0x10020010  pwms
0x10020020  pwmcmp0
0x10020024  pwmcmp1
0x10020028  pwmcmp2
0x1002002c  pwmcmp3
```

可以连续执行 `md.l` 观察 `pwmcount`/`pwms` 是否变化；如果不变化，先检查
`pwmcfg` 的 bit 12、外设时钟和对应 GPIO 复用。

### 用当前 bare-metal LED 程序验证

```text
=> tftpboot 0x84000000 unmatched-led.bin
=> go 0x84000000 solid red
=> go 0x84000000 blink red
=> go 0x84000000 off
```

程序使用 PWM0 通道 1、2、3，常亮时写 `0x0000`，闪烁时写 `0x8000`，关闭时写
`0xffff`，并且会在程序返回后让 PWM 硬件继续运行。实现细节见
[`baremetal/apps/unmatched-led/README.md`](../../baremetal/apps/unmatched-led/README.md)。
通用的 PRCI 实现位于 `baremetal/soc/fu740/`，频率、占空比和 MMIO 更新实现位于
`baremetal/drivers/pwm/`，新测试程序应优先调用 `include/soc/fu740.h` 和
`include/drivers/sifive_pwm.h` 中的公共 API。

## 7. 直接寄存器编程顺序

如果不使用 U-Boot PWM 命令，而是在 U-Boot `go` 程序或其他裸机代码中直接访问
MMIO，可以按下面顺序实现一个持续 PWM：

```c
/* PWM0_BASE = 0x10020000, channel = 2 */

write32(PWM0_BASE + 0x00, 0);       /* 停止 pwmenalways */
fence_iorw();
write32(PWM0_BASE + 0x08, 0);       /* 从周期起点开始 */
write32(PWM0_BASE + 0x28, 0x8000);  /* 约 50% 占空比 */
write32(PWM0_BASE + 0x00,
        (scale & 0xf) | (1 << 12)); /* pwmscale + pwmenalways */
fence_iorw();
```

实际开发时还要完成以下前置条件：

1. 外设时钟已经打开，并且知道 PWM 输入时钟频率。
2. 对应 GPIO 已切换到 PWM 功能，而不是普通 GPIO 或其他复用功能。
3. 先确认通道与板上负载的连接关系。
4. 修改周期和比较值时避免 32 位/16 位宽度错误；比较寄存器有效位只有 15:0。
5. 多通道使用同一 PWM 实例时，意识到修改 `pwmscale` 会影响所有通道。

U-Boot `go` 程序还依赖 U-Boot 已完成 DDR、时钟和引脚初始化；它不是复位后可以
独立运行的完整固件。

## 8. 建议的学习路径

### 第一步：只读懂一个通道

先忽略 ganging、中断和 one-shot，只回答 4 个问题：

1. 计数器每个时钟如何加一？
2. `pwmscale` 如何改变比较计数值的速度？
3. `pwmcmpX` 在周期的哪个位置产生边沿？
4. 负载的电气极性如何把 PWM 高低电平转换成亮灭或转速？

使用下面这组寄存器练习最容易建立直觉：

```text
pwmcfg[3:0]   = pwmscale
pwmcfg[12]    = pwmenalways
pwms           = 缩放后的计数值
pwmcmp2       = 通道 2 的边沿阈值
```

### 第二步：做可重复的 U-Boot 实验

建议每次只改一个变量：

1. 固定 `pwmcmp2`，逐步把 `pwmscale` 从 0 改到 4，观察频率下降。
2. 固定 `pwmscale`，把 `pwmcmp2` 从 `0x0000`、`0x4000`、`0x8000` 改到
   `0xffff`，观察占空比或 LED 亮度变化。
3. 同时设置通道 1、2、3，确认多个输出共享频率但可以使用不同比较值。
4. 设置 `pwmcfg[9]`，观察 `pwmcmp0` 如何从普通通道变成周期重置源。
5. 最后再尝试中心对齐、去毛刺和 one-shot。

每次实验都记录 4 项数据：输入时钟、`pwmcfg`、比较值、示波器测得的周期/占空比。
这样可以区分“寄存器计算错误”和“GPIO/负载极性错误”。

### 第三步：从驱动理解抽象层

推荐按这个顺序阅读源码：

1. FU740 手册第 17 章：先掌握硬件模型和寄存器语义。
2. `src/u-boot/drivers/pwm/pwm-sifive.c`：看 period/duty 如何转换成 scale 和
   compare 值。
3. `src/u-boot/cmd/pwm.c`：看 U-Boot 命令如何调用 PWM 驱动。
4. `src/linux/drivers/pwm/pwm-sifive.c`：对照 Linux 如何处理硬件反相和时钟变化。
5. `src/linux/arch/riscv/boot/dts/sifive/hifive-unmatched-a00.dts`：看通道、周期和
   LED 设备如何声明。
6. `src/u-boot/board/sifive/unmatched/spl.c`：看板级启动阶段如何直接写寄存器。

### 第四步：用仪器验证

LED 只能告诉你“看起来亮或灭”，不能准确告诉你频率、占空比和边沿毛刺。真正
学习 PWM 时，最好把逻辑分析仪或示波器接在 PWM 输出节点，验证：

```text
频率    = 1 / 周期
占空比  = 有效高（或有效低）时间 / 周期
边沿    = 比较值改变后是否只在预期位置变化
```

对于 Unmatched，还要在测量结果旁记录当前负载是 D12、D2 RGB 还是风扇，因为同一
个 PWM 数值经过不同的 MOSFET、LED 或风扇电路后，视觉/机械结果可能不同。

## 9. 常见问题

### 为什么写了比较值但没有输出？

按以下顺序排查：

1. `pwmcfg[12]` 是否为 1，或者是否只设置了 one-shot 但已经运行完？
2. PWM 输入时钟是否打开，频率是否为 0？
3. GPIO 是否仍处于 GPIO、QSPI 或其他复用功能？
4. 通道编号是否正确？D2 红/绿/蓝是 2/1/3，不是 0/1/2。
5. 是否把硬件反相输出误当成普通 active-high PWM？

### 为什么实际频率不是目标值？

FU740 的 `pwmscale` 只能选择 0 到 15 的整数，周期只能按 2 的幂变化，因此
任意目标周期都需要量化。U-Boot 驱动会选择近似值；如果要求更高精度，需要使用
外部定时器、软件补偿或重新设计时钟/周期方案。

### 为什么改变 D2 会影响 D12？

它们都在 PWM0 上，共享 `pwmcount` 和 `pwmscale`。改变某个程序写入的 `PWMCFG`
不仅影响当前颜色，也会改变 PWM0 所有通道的周期和相位。比较值仍然是各通道
独立的。

### 为什么 `0xffff` 不是严格的 0%？

FU740 PWM 的比较器宽度为 16 位，硬件不能表达真正的 0% 有效时间。驱动使用
`0xffff` 表示关闭，残留的单个计数脉冲通常不足以点亮 LED，但在高速测量仪器上
仍可能看到它。

## 10. 参考资料

- `docs/reference/unmatched/hardware/freedom-u740-c000-manual-v1p7.pdf`，第 17 章
  “Pulse Width Modulator (PWM)”（页 153 到 162）。
- `src/u-boot/drivers/pwm/pwm-sifive.c`
- `src/u-boot/cmd/pwm.c`
- `src/u-boot/board/sifive/unmatched/spl.c`
- `src/linux/drivers/pwm/pwm-sifive.c`
- `src/linux/arch/riscv/boot/dts/sifive/hifive-unmatched-a00.dts`
- `docs/reference/unmatched/hardware/hifive-unmatched-schematics-v3.pdf`
