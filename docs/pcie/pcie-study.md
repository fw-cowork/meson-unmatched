# Unmatched PCIe study notes

## Study order

Use this order when learning the Unmatched stack. It keeps the board-level
facts, boot flow, firmware, Linux, PCIe, and JTAG debug path connected.

### 1. Board overview

Primary documents:

```text
HiFive Unmatched Datasheet
HiFive Unmatched Schematics v3
```

Goals:

- Identify the main devices: FU740, DDR4, QSPI flash, microSD, PCIe x16, FTDI
  USB-JTAG/UART, PMIC, and clock sources.
- Understand the board-level reset, power, clock, DIP switch, and debug
  topology before reading firmware code.

Schematic sheets to inspect first:

```text
FU740 - Misc
Boot Select
JTAG MUX
Reset
Clock Select
PCIe power/reset GPIOs
```

### 2. Boot mode and MSEL

Primary documents:

```text
FU740-C000 Manual: Boot Process
HiFive Unmatched Software Reference Manual: Bootflow
Freedom-U-SDK README: MSEL for Unmatched
```

Key concepts:

```text
MSEL[3:0]
ZSBL
U-Boot SPL
OpenSBI
U-Boot proper
Linux
```

Important MSEL values:

```text
1011  uSD boot
0110  QSPI0 x4 SPI flash boot
0000  wait for debugger
```

`CHIPIDSEL` is separate from `MSEL[3:0]`. The Unmatched schematic connects it
to FU740 `CHIP_ID_SELECT_0`, but the public FU740 boot table only uses
`MSEL[3:0]` for boot-source selection.

### 3. Image layout

Primary files:

```text
meta-sifive/scripts/lib/wic/canned-wks/unmatched-spl-opensbi.wks
meta-sifive/conf/machine/unmatched.conf
```

Key artifacts:

```text
u-boot-spl.bin
u-boot.itb
fw_dynamic.bin
rootfs
GPT partition GUIDs
```

Understand why SPL and U-Boot ITB are placed in raw GPT partitions instead of
being loaded as normal `/boot` filesystem files.

### 4. OpenSBI role

Primary source path:

```text
opensbi/platform/generic/sifive/fu740.c
```

OpenSBI is not the PCIe initialization owner on this platform. Its role is the
M-mode runtime: SBI services, FU740 errata hooks, and board reset/poweroff
integration. It is loaded as FW_DYNAMIC by the SPL/U-Boot chain.

### 5. U-Boot SPL and U-Boot proper

Primary source paths:

```text
u-boot/board/sifive/unmatched/
u-boot/arch/riscv/dts/
u-boot/configs/sifive_unmatched_defconfig
```

Topics:

```text
DDR initialization
OpenSBI loading
DTB fixups
MAC address / OTP handling
QSPI and microSD boot
PCIe / NVMe early scan
```

Useful U-Boot commands:

```text
bdinfo
fdt print
pci enum
pci 0
nvme scan
```

### 6. DDR

Primary source paths and documents:

```text
HiFive Unmatched Datasheet: 16 GB 64-bit DDR4
HiFive Unmatched Schematics v3: FU740 - DDR4 / DDR4x8 sheets
FU740-C000 Manual: DDR Subsystem
U-Boot: drivers/ram/sifive/sifive_ddr.c
U-Boot: arch/riscv/dts/fu740-c000-u-boot.dtsi
U-Boot: arch/riscv/dts/fu740-hifive-unmatched-a00-ddr.dtsi
```

Study in this order:

```text
DDR4 topology in the schematic
DDR address range and memory node
PRCI DDRPLL and DDR reset lines
U-Boot SPL dmc node
sifive,ddr-params register table
Denali controller / PHY register programming
write leveling, read leveling, gate training, Vref training
physical filter / bus blocker release
get_ram_size() validation
Linux memory@80000000 handoff
```

Important working conclusion:

```text
DDR is brought up by U-Boot SPL, not by OpenSBI or Linux.
Linux consumes the final memory description through the device tree.
```

### 7. Linux device tree

Primary source paths:

```text
arch/riscv/boot/dts/sifive/hifive-unmatched-a00.dts
arch/riscv/boot/dts/sifive/fu740-c000.dtsi
```

Read these before the Linux drivers. The DTS explains how drivers are probed
and which resources they receive:

```text
memory
cpus
clint
plic
uart
gpio
i2c
spi
mmc
pcie@e00000000
```

### 8. PCIe

Primary source paths and documents:

```text
Linux:  drivers/pci/controller/dwc/pcie-fu740.c
U-Boot: drivers/pci/pcie_dw_sifive.c
FU740-C000 Manual: PCIe X8 AXI4 Subsystem
```

Study in this order:

```text
DTS pcie node
reset-gpios / pwren-gpios / clocks / mgmt reg
PHY init through CR_PARA
LTSSM enable
DesignWare DBI registers
link-up polling
Linux PCI enumeration
endpoint driver probe
```

Important working conclusion:

```text
Controller: Synopsys DesignWare PCIe.
PHY: likely Synopsys DesignWare PCIe PHY/PCS, inferred from CR_PARA naming and
     the DesignWare glue driver, but not explicitly named in the public manual.
```

### 9. JTAG and debug

Primary files and documents:

```text
openocd_hifive_unmatched.cfg
FU740-C000 Manual: MSEL=0000 debugger wait mode
HiFive Unmatched Schematics v3: JTAG MUX / FTDI
```

Key concepts:

```text
FTDI 0403:6010
OpenOCD
gdb-multiarch
MSEL=0000 wait for debugger
reset halt
```

### 10. Integrated experiments

Recommended experiment order:

```text
1. Boot from SD card and save a complete UART log.
2. Switch to SPI flash boot and confirm the boot source.
3. Use JTAG to halt the CPU and inspect registers.
4. Confirm U-Boot reports DRAM: 16 GiB and inspect bdinfo.
5. Run U-Boot pci enum / nvme scan.
6. Run Linux free / dmesg / lspci checks.
7. Match DDR initialization and PCIe SerDes/link-up logs to source.
8. Add temporary U-Boot or Linux debug prints around reset, PHY init, LTSSM,
   and link polling.
```

Short route:

```text
board hardware -> MSEL/boot chain -> image layout -> OpenSBI -> U-Boot ->
DDR -> Linux DTS -> PCIe -> JTAG debug
```

## GitHub source map and databook status

I did not find a credible public GitHub copy of the real vendor databooks for
the FU740 PCIe or DDR IP blocks. Treat the public GitHub material as driver
source and board parameter data, not as the complete IP vendor databook.

### PCIe public source

Relevant GitHub entry points:

```text
Linux:
https://github.com/torvalds/linux/blob/master/drivers/pci/controller/dwc/pcie-fu740.c
https://github.com/torvalds/linux/blob/master/drivers/pci/controller/dwc/pcie-designware.c
https://github.com/torvalds/linux/blob/master/drivers/pci/controller/dwc/pcie-designware.h
https://github.com/torvalds/linux/blob/master/Documentation/devicetree/bindings/pci/sifive,fu740-pcie.yaml
https://github.com/torvalds/linux/blob/master/arch/riscv/boot/dts/sifive/fu740-c000.dtsi
https://github.com/torvalds/linux/blob/master/arch/riscv/boot/dts/sifive/hifive-unmatched-a00.dts

U-Boot:
https://github.com/u-boot/u-boot/blob/master/drivers/pci/pcie_dw_sifive.c
https://github.com/u-boot/u-boot/blob/master/drivers/pci/pcie_dw_common.c
```

Corresponding IP documentation, if available under NDA/vendor access:

```text
Controller databook: Synopsys DesignWare PCIe Root Complex / PCIe controller.
PHY databook: likely Synopsys DesignWare PCIe PHY/PCS, inferred from CR_PARA
              access registers and the DesignWare PCIe software stack.
```

The public FU740 manual is the best non-NDA overview for the FU740 PCIe
subsystem, but it does not publish the full Synopsys register databook.

### DDR public source

Relevant GitHub entry points:

```text
U-Boot:
https://github.com/u-boot/u-boot/blob/master/drivers/ram/sifive/sifive_ddr.c
https://github.com/u-boot/u-boot/blob/master/arch/riscv/dts/fu740-c000-u-boot.dtsi
https://github.com/u-boot/u-boot/blob/master/arch/riscv/dts/hifive-unmatched-a00-u-boot.dtsi
https://github.com/u-boot/u-boot/blob/master/arch/riscv/dts/fu740-hifive-unmatched-a00-ddr.dtsi

Linux:
https://github.com/torvalds/linux/blob/master/arch/riscv/boot/dts/sifive/hifive-unmatched-a00.dts
```

Corresponding IP documentation, if available under NDA/vendor access:

```text
DDR controller / PHY databook: Denali DDR controller and PHY documentation.
Vendor lineage: Denali IP is associated with Cadence.
Board-specific timing data: sifive,ddr-params in
                            fu740-hifive-unmatched-a00-ddr.dtsi.
```

For practical bring-up, the U-Boot DDR parameter DTSI is the most important
public file. It contains the generated controller/PHY register image used by
SPL; it is not a readable replacement for the Denali/Cadence databook.

## Code-based end-to-end flow

This section follows the actual source paths in the local SDK tree.

### 1. Boot ROM to SPL

The FU740 boot ROM / ZSBL selects the boot source from `MSEL[3:0]`, loads the
next-stage image, and starts U-Boot SPL before DRAM is usable.

At this point the important fact is:

```text
SPL must run from on-chip memory.
SPL must initialize DDR before OpenSBI and U-Boot proper can run from DRAM.
```

### 2. SPL DDR input from device tree

U-Boot SPL gets its DDR resources from:

```text
arch/riscv/dts/hifive-unmatched-a00-u-boot.dtsi
arch/riscv/dts/fu740-c000-u-boot.dtsi
arch/riscv/dts/fu740-hifive-unmatched-a00-ddr.dtsi
```

The `dmc@100b0000` node provides:

```text
controller base:       0x100b0000
PHY base:              0x100b2000
physical filter base:  0x100b8000
DDRPLL target:         clock-frequency = 933333324
DDR register image:    sifive,ddr-params
```

### 3. SPL DDR driver flow

Main file:

```text
drivers/ram/sifive/sifive_ddr.c
```

Call flow:

```text
sifive_ddr_probe()
  fdtdec_setup_mem_size_base()
  gd->ram_base / gd->ram_size -> priv->info
  clk_get_by_index()
  dev_read_u32("clock-frequency")
  clk_set_rate(ddr_clk, clock)
  clk_enable(ddr_clk)
  dev_read_addr_index(0) -> Denali controller base
  dev_read_addr_index(1) -> Denali PHY base
  dev_read_addr_index(2) -> physical filter base
  sifive_ddr_setup()
```

`sifive_ddr_setup()` does the actual register programming:

```text
dev_read_u32_array("sifive,ddr-params")
copy params->pctl_regs.denali_ctl to controller registers
copy params->phy_regs.denali_phy to PHY registers
disable read interleave
disable optimal read-modify-write
enable write leveling
enable read leveling
enable read-leveling gate
enable DDR4 Vref training if DRAM class is DDR4
mask init/training/out-of-range/port-command interrupts
program address range protection
sifive_ddr_start()
sifive_ddr_phy_fixup()
get_ram_size()
```

`sifive_ddr_start()` is the point where the memory controller is started:

```text
set DENALI_CTL_0.start
poll DENALI_CTL_132.MC_INIT_COMPLETE
open the physical filter / bus blocker for the DRAM range
```

If `get_ram_size()` does not match the DT memory size, SPL prints
`DDR invalid size` and stops the boot path.

### 4. SPL loads OpenSBI and U-Boot proper

After DDR is valid, SPL can load larger payloads into DRAM:

```text
OpenSBI FW_DYNAMIC
U-Boot proper
runtime DTB
```

OpenSBI's FU740 platform file:

```text
platform/generic/sifive/fu740.c
```

does not initialize DDR or PCIe. In this tree it is mainly responsible for
FU740 platform integration such as PMIC-backed reset/shutdown handling and
runtime SBI services.

### 5. U-Boot PCIe resource discovery

Main U-Boot PCIe file:

```text
drivers/pci/pcie_dw_sifive.c
```

The U-Boot `pcie@e00000000` DT node provides:

```text
reg-names = "dbi", "config", "mgmt"
pwren-gpios = <&gpio 5 0>
reset-gpios = <&gpio 8 0>
clocks = <&prci PRCI_CLK_PCIEAUX>
resets = <&prci PRCI_RST_PCIE_POWER_UP_N>
ranges = PCI I/O, MEM, and prefetchable MEM windows
interrupts = MSI and INTx lines
```

U-Boot platform data flow:

```text
pcie_sifive_of_to_plat()
  get "dbi" base
  get "mgmt" base
  request pwren-gpios
  request reset-gpios
  get pcie_aux clock
  get pcie_power_up_rst_n reset
```

### 6. U-Boot PCIe bring-up flow

Probe path:

```text
pcie_sifive_probe()
  pcie_sifive_init_port(SV_PCIE_HOST_TYPE)
  print link speed/width when link is up
  pcie_dw_prog_outbound_atu_unroll(region0, MEM)
```

Port initialization:

```text
pcie_sifive_assert_reset()
  reset GPIO low
  PCIEX8MGMT_PERST_N = 0

pcie_sifive_power_on()
  pwren GPIO high

pcie_sifive_deassert_reset()
  PCIEX8MGMT_PERST_N = 1
  reset GPIO high

enable pcie_aux clock
PCIEX8MGMT_APP_HOLD_PHY_RST = 1
deassert PRCI pcie_power_up_rst_n
pcie_sifive_init_phy()
disable pcie_aux clock
PCIEX8MGMT_APP_HOLD_PHY_RST = 0
enable pcie_aux clock
PCIEX8MGMT_DEVICE_TYPE = Root Complex
pcie_dw_setup_host()
pcie_sifive_start_link()
pcie_sifive_wait_for_link()
```

PHY programming uses the CR_PARA window:

```text
pcie_sifive_init_phy()
  PCIEX8MGMT_PHY0_CR_PARA_SEL = 1
  PCIEX8MGMT_PHY1_CR_PARA_SEL = 1
  for each lane in each PHY block:
    write CR_PARA_ADDR
    write CR_PARA_WR_DATA
    set CR_PARA_WR_EN
    wait CR_PARA_ACK == 1
    clear CR_PARA_WR_EN
    wait CR_PARA_ACK == 0
```

The only board-specific PHY setting in this driver is AC termination mode for
the lanes. That is why the full PHY behavior is hidden behind the hard macro
and the generated/implicit reset defaults.

Before enabling LTSSM, U-Boot forces Gen1 operation:

```text
pcie_sifive_force_gen1()
  enable DBI read-only writes
  update PCIe link capability max speed field
  disable DBI read-only writes
```

Then:

```text
PCIEX8MGMT_APP_LTSSM_ENABLE = 1
poll PHY_DEBUG_R1 / PCIE_PORT_DEBUG1
  bit 4  == link up
  bit 29 == link in training
```

After link-up, U-Boot programs outbound iATU windows so CPU accesses can reach
PCIe memory/config space.

### 7. Linux PCIe flow

Main Linux files:

```text
drivers/pci/controller/dwc/pcie-fu740.c
drivers/pci/controller/dwc/pcie-designware.c
drivers/pci/controller/dwc/pcie-designware-host.c
```

Linux probe:

```text
fu740_pcie_probe()
  map "mgmt" resource
  get reset-gpios
  get pwren-gpios
  get pcie_aux clock
  get PRCI reset
  dw_pcie_host_init()
```

Generic DesignWare host init calls the FU740-specific init hook first:

```text
dw_pcie_host_init()
  dw_pcie_host_get_resources()
  fu740_pcie_host_init()
  MSI setup
  dw_pcie_version_detect()
  dw_pcie_iatu_detect()
  dw_pcie_setup_rc()
  fu740_pcie_start_link()
  dw_pcie_wait_for_link()
  pci_host_probe()
```

FU740 Linux host init mirrors the U-Boot sequence:

```text
fu740_pcie_drive_reset()
  assert reset GPIO and controller PERST_N
  enable slot power
  wait at least 100 ms
  deassert controller PERST_N and reset GPIO

enable pcie_aux clock
PCIEX8MGMT_APP_HOLD_PHY_RST = 1
deassert PRCI pcie_power_up_rst_n
fu740_pcie_init_phy()
disable pcie_aux clock
PCIEX8MGMT_APP_HOLD_PHY_RST = 0
enable pcie_aux clock
PCIEX8MGMT_DEVICE_TYPE = Root Complex
```

Linux also uses CR_PARA to write the PHY lane termination setting, then starts
the link:

```text
fu740_pcie_start_link()
  temporarily force 2.5 GT/s in PCI_EXP_LNKCAP
  PCIEX8MGMT_APP_LTSSM_ENABLE = 1
  dw_pcie_wait_for_link()
  restore original speed capability if needed
  request speed change
  dw_pcie_wait_for_link()
```

Generic DWC link status is:

```text
dw_pcie_link_up()
  read PCIE_PORT_DEBUG1
  link is up when bit 4 is set and bit 29 is clear
```

Finally `pci_host_probe()` enumerates the hierarchy and endpoint drivers such
as NVMe, AHCI, or GPU drivers can bind to their devices.

### 8. Ownership summary

```text
Boot ROM / ZSBL: boot-source selection and initial load
U-Boot SPL:      DDR clock, controller, PHY, training, memory validation
OpenSBI:         SBI runtime, reset/shutdown integration, no DDR/PCIe init
U-Boot proper:   optional early PCIe/NVMe enumeration
Linux:           final PCIe host setup, enumeration, MSI/INTx, endpoint drivers
```

## What to separate

OpenSBI is normally not where PCIe enumeration happens on this board. It runs
in M-mode and hands off to U-Boot through FW_DYNAMIC. For this SDK, PCIe study
starts mainly in two places:

- U-Boot: early device tree, clocks/resets, PCIe host bridge probing, optional
  storage discovery before Linux.
- Linux: final DTB, `pcie-fu740` host controller driver, resource windows,
  interrupt mapping, MSI/MSI-X behavior, and endpoint drivers.

OpenSBI's FU740 platform code is not the PCIe SerDes/link-training owner in
this SDK. The relevant OpenSBI file is:

```text
platform/generic/sifive/fu740.c
```

It handles board reset integration and FU740 errata hooks, but there is no
PCIe, SerDes, LTSSM, or link-up sequence there.

## DDR bring-up path

The Unmatched board has onboard 16 GB 64-bit DDR4. The public datasheet lists
the board memory as 16 GB DDR4 at 1866 MT/s. Older software reference manual
examples show a DDR PLL value that works out to 1846 MT/s; treat the exact
rate as firmware-version dependent and verify it from the active U-Boot DTS or
the PRCI DDRPLL register.

The FU740 manual describes the DDR subsystem as:

```text
DDR PHY
DDR Controller
Physical Filter
```

The important memory map is:

```text
DDR controller / PHY control: 0x100b0000 - 0x100b3fff
Physical filter registers:    0x100b8000 - 0x100b8fff
DDR memory base:              0x80000000
```

On Unmatched, the Linux and U-Boot board DTS describe the installed memory as:

```dts
memory@80000000 {
	device_type = "memory";
	reg = <0x0 0x80000000 0x4 0x00000000>;
};
```

That is 16 GiB starting at physical address `0x80000000`.

### DDR owner

DDR is initialized by U-Boot SPL. The flow is:

```text
Boot ROM / ZSBL runs before DRAM is usable
U-Boot SPL runs from L2 LIM
U-Boot SPL programs CPU/DDR clocks
U-Boot SPL configures DDR controller and PHY
U-Boot SPL validates DRAM size
OpenSBI + U-Boot proper are loaded into DRAM
Linux receives the final memory description from the DTB
```

OpenSBI does not train DDR, and Linux does not perform the initial DDR bring-up.

### U-Boot files

Main driver:

```text
drivers/ram/sifive/sifive_ddr.c
```

Clock/reset support:

```text
drivers/clk/sifive/fu740-prci.c
```

U-Boot SPL device tree pieces:

```text
arch/riscv/dts/hifive-unmatched-a00-u-boot.dtsi
arch/riscv/dts/fu740-c000-u-boot.dtsi
arch/riscv/dts/fu740-hifive-unmatched-a00-ddr.dtsi
```

The Unmatched U-Boot overlay includes the board-specific DDR parameter table:

```dts
#include "fu740-hifive-unmatched-a00-ddr.dtsi"
```

The `dmc` node gives the driver its controller, PHY, physical-filter, clock,
and reset resources:

```dts
dmc: dmc@100b0000 {
	compatible = "sifive,fu740-c000-ddr";
	reg = <0x0 0x100b0000 0x0 0x0800
	       0x0 0x100b2000 0x0 0x2000
	       0x0 0x100b8000 0x0 0x1000>;
	clocks = <&prci PRCI_CLK_DDRPLL>;
	clock-frequency = <933333324>;
	u-boot,dm-spl;
};
```

The `clock-frequency` value is the DDR PLL target used by the SPL driver in
this source tree. With the formula from the software reference manual, the DDR
data rate is approximately twice the DDR PLL rate.

The board DDR register table is stored in:

```text
sifive,ddr-params
```

inside:

```text
fu740-hifive-unmatched-a00-ddr.dtsi
```

This large table is copied into the Denali DDR controller and PHY registers by
the SPL RAM driver.

### SPL init sequence

The relevant driver path is:

```text
sifive_ddr_probe()
  clk_get_by_index()
  dev_read_u32("clock-frequency")
  clk_set_rate(ddr_clk, clock)
  clk_enable(ddr_clk)
  read controller / phy / physical filter base addresses from DT
  sifive_ddr_setup()
```

`sifive_ddr_setup()` then:

```text
reads sifive,ddr-params
copies Denali controller registers
copies Denali PHY registers
disables read interleave for valid TileLink operation
disables optimal read-modify-write logic
enables write leveling
enables read leveling
enables read-leveling gate
enables Vref training for DDR4
masks DDR interrupts used during init
sets up controller address range protection
sets DENALI_CTL_0.start
polls DENALI_CTL_132.MC_INIT_COMPLETE
opens the physical filter / bus blocker for the valid DRAM range
applies PHY fixup / errata checks
validates size with get_ram_size()
```

The FU740 manual describes the same high-level sequence:

```text
start DDRCTRLCLK from DDRPLL
release DDR controller / AXI / AHB / PHY resets through PRCI
program controller registers at 0x100b0000
program PHY registers at 0x100b2000
set burst length
mask interrupts
set DDR controller start bit
poll initialization complete
disable the bus blocker / physical filter for the valid DRAM range
DDR responds at 0x80000000
```

### DDR checkpoints

At the U-Boot prompt:

```text
bdinfo
fdt addr ${fdtcontroladdr}
fdt print /memory@80000000
fdt print /soc/dmc@100b0000
md.l 0x1000000c 1       # DDRPLL config register
md.l 0x100b0000 8       # DDR controller control registers
md.l 0x100b0210 1       # DENALI_CTL_132, init status bits
md.l 0x100b8000 4       # physical filter registers
```

Expected high-level output:

```text
DRAM: 16 GiB
```

In Linux:

```bash
dmesg -T | grep -Ei 'memory|memblock|OF: fdt'
cat /proc/iomem | grep -Ei 'System RAM|reserved'
free -h
cat /proc/meminfo | head
```

The software reference manual shows Linux ignoring the low reserved region near
`0x80000000` during early boot. That is expected; not all physical DRAM is made
available to the kernel as normal page allocator memory.

### DDR failure triage

- No U-Boot proper output after SPL: suspect DDR clock/reset, DDR params, or
  PHY training failure.
- SPL prints `DDR invalid size`: inspect `memory@80000000`, the board DDR
  params table, and `get_ram_size()` result.
- Intermittent failures: lower CPU/DDR clock experiments are more useful than
  PCIe changes; DDR must be stable before PCIe debugging has meaning.
- Linux sees much less memory than U-Boot: compare the runtime DTB
  `/memory@80000000`, Linux `memblock` logs, and reserved-memory regions.

## PCIe SerDes and link-up path

The FU740 PCIe SerDes/link-up sequence is visible in the SiFive DesignWare PCIe
glue drivers.

U-Boot:

```text
drivers/pci/pcie_dw_sifive.c
```

Key functions:

```text
pcie_sifive_init_port()
pcie_sifive_init_phy()
pcie_sifive_start_link()
pcie_sifive_check_link()
pcie_sifive_wait_for_link()
```

Linux:

```text
drivers/pci/controller/dwc/pcie-fu740.c
drivers/pci/controller/dwc/pcie-designware.c
```

Key functions:

```text
fu740_pcie_host_init()
fu740_pcie_init_phy()
fu740_pcie_start_link()
dw_pcie_wait_for_link()
```

The board DTS node that feeds both drivers is:

```text
arch/riscv/dts/fu740-c000.dtsi                         # U-Boot
arch/riscv/boot/dts/sifive/fu740-c000.dtsi             # Linux
```

Look for:

```text
pcie@e00000000
compatible = "sifive,fu740-pcie"
reg-names = "dbi", "config", "mgmt"
clock-names = "pcie_aux"
pwren-gpios
reset-gpios
resets
```

The effective initialization order is:

```text
assert endpoint PERST and controller PERST
enable PCIe slot power
deassert PERST
enable pcie_aux clock
assert APP_HOLD_PHY_RST so LTSSM is held while PHY registers are programmed
deassert pcie_power_up_rst_n through the PRCI reset
write PHY/SerDes lane parameters through the mgmt cr_para interface
clear APP_HOLD_PHY_RST
set controller device type to Root Complex
enable LTSSM
poll DesignWare debug registers until link-up and not-in-training
```

Important FU740 management registers used by both implementations:

```text
PCIEX8MGMT_PERST_N
PCIEX8MGMT_APP_LTSSM_ENABLE
PCIEX8MGMT_APP_HOLD_PHY_RST
PCIEX8MGMT_DEVICE_TYPE
PCIEX8MGMT_PHY0_CR_PARA_*
PCIEX8MGMT_PHY1_CR_PARA_*
```

The common DesignWare link status check reads the DBI debug register:

```text
PHY_DEBUG_R1 / PCIE_PORT_DEBUG1
  bit 4   link up
  bit 29  link in training
```

The link is considered usable when bit 4 is set and bit 29 is clear.

## PCIe PHY IP inference

The public FU740-C000 manual says the SoC integrates a PCIe Gen3 x8 controller
and PHY, and that the PHY uses a PIPE 4 interface. It does not explicitly name
the PHY IP vendor.

The software-visible register naming is a strong clue:

```text
PCIEX8MGMT_PHY0_CR_PARA_ADDR
PCIEX8MGMT_PHY0_CR_PARA_RD_EN
PCIEX8MGMT_PHY0_CR_PARA_RD_DATA
PCIEX8MGMT_PHY0_CR_PARA_SEL
PCIEX8MGMT_PHY0_CR_PARA_WR_DATA
PCIEX8MGMT_PHY0_CR_PARA_WR_EN
PCIEX8MGMT_PHY0_CR_PARA_ACK
PCIEX8MGMT_PHY1_CR_PARA_*
```

Together with the DesignWare PCIe controller driver, the `CR_PARA`/`cr_para`
PHY parameter access interface strongly suggests a Synopsys DesignWare PCIe PHY
or Synopsys-compatible PHY macro behind SiFive's FU740 PCIe management wrapper.

Treat the precise statement as:

```text
Controller: Synopsys DesignWare PCIe.
PHY: very likely Synopsys DesignWare PCIe PHY/PCS, inferred from CR_PARA naming
     and the DWC glue driver, but not explicitly named in the public FU740 manual.
```

## U-Boot checkpoints

At the U-Boot prompt:

```text
version
bdinfo
fdt addr ${fdtcontroladdr}
fdt print /soc/pcie@e00000000
pci enum
pci 0
pci header <bus.dev.fn>
nvme scan
nvme info
```

Source paths to inspect after `./lab/unmatched.sh build u-boot`:

```text
drivers/pci/
drivers/pci/pcie_dw_common.c
drivers/pci/pcie_dw_sifive.c
arch/riscv/dts/hifive-unmatched-a00.dts
configs/sifive_unmatched_defconfig
```

Search terms:

```bash
rg -n "pcie|pci|nvme|hifive-unmatched|fu740" <u-boot-source>
```

## Linux checkpoints

The local defconfig enables the important PCIe pieces:

```text
CONFIG_PCI=y
CONFIG_PCIEPORTBUS=y
CONFIG_PCI_HOST_GENERIC=y
CONFIG_PCIE_FU740=y
CONFIG_BLK_DEV_NVME=m
CONFIG_SATA_AHCI=y
CONFIG_DRM_RADEON=m
CONFIG_DRM_AMDGPU=m
CONFIG_DRM_NOUVEAU=m
```

After Linux boots:

```bash
dmesg -T | grep -Ei 'pci|pcie|nvme|ahci|radeon|amdgpu|msi|iommu'
cat /proc/iomem | grep -Ei 'pci|pcie'
find /sys/bus/pci/devices -maxdepth 2 -type f -name vendor -print -exec cat {} \;
lspci -nn
lspci -vv
lsmod | grep -Ei 'nvme|ahci|radeon|amdgpu|nouveau'
```

Kernel source paths to inspect after `./lab/unmatched.sh build linux`:

```text
drivers/pci/controller/pcie-fu740.c
arch/riscv/boot/dts/sifive/hifive-unmatched-a00.dts
arch/riscv/boot/dts/sifive/fu740-c000.dtsi
```

Search terms:

```bash
rg -n "fu740|pcie|pci|msi|interrupt-map|ranges" <linux-source>
```

## Device experiments

Start simple:

1. Empty PCIe slot: confirm host bridge probes without endpoints.
2. NVMe SSD: verify U-Boot `nvme scan`, then Linux `nvme` module and block
   device creation.
3. AHCI/SATA card: verify BAR assignment and `ahci` probe.
4. GPU: use a known-good low-power AMD card first; Freedom-U-SDK notes mention
   Radeon HD 6450 and RX 550/570/580 class cards.

For every hardware change, save three logs:

```bash
dmesg -T > dmesg-pcie-$(date +%Y%m%d-%H%M%S).log
lspci -nnvv > lspci-$(date +%Y%m%d-%H%M%S).log
cat /proc/iomem > iomem-$(date +%Y%m%d-%H%M%S).log
```

## Failure triage

- No U-Boot PCIe output: inspect U-Boot DTB node status, reset/clock handling,
  and whether the endpoint needs more power or PERST delay.
- U-Boot sees device but Linux does not: compare U-Boot control DTB with Linux
  DTB, especially PCIe `ranges`, interrupts, and MSI properties.
- Linux host bridge probes but endpoint fails: collect `dmesg` with
  `pci=earlydump` or `pci=assign-busses` as a temporary bootarg experiment.
- NVMe visible but no block device: check whether `nvme` is built as a module
  and loaded, and whether the rootfs has `kernel-modules`.

## JTAG over USB

HiFive Unmatched exposes JTAG through the onboard FTDI USB debug interface.
The useful OpenOCD parameters are:

```text
adapter driver ftdi
ftdi_device_desc "Dual RS232-HS"
ftdi_vid_pid 0x0403 0x6010
jtag newtap riscv cpu -irlen 5 -expected-id 0x20000913
```

A known-good OpenOCD config exists locally in the Zephyr tree:

```text
/home/adrian/doc/nfp/merlin/MINIC_20240710/SDK/zephyr/boards/riscv/hifive_unmatched/support/openocd_hifive_unmatched.cfg
```

That config creates five RISC-V targets, one monitor hart plus four U74
application harts:

```text
riscv.cpu.0
riscv.cpu.1
riscv.cpu.2
riscv.cpu.3
riscv.cpu.4
```

It also enables SMP target grouping and defines the onboard QSPI flash bank:

```text
target smp riscv.cpu.0 riscv.cpu.1 riscv.cpu.2 riscv.cpu.3 riscv.cpu.4
flash bank onboard_spi_flash0 fespi 0x20000000 0 0 0 riscv.cpu.0 0x10040000
```

### Host-side setup

Confirm the FTDI device is visible:

```bash
lsusb -d 0403:6010
```

Start OpenOCD:

```bash
openocd -f /home/adrian/doc/nfp/merlin/MINIC_20240710/SDK/zephyr/boards/riscv/hifive_unmatched/support/openocd_hifive_unmatched.cfg
```

If USB permissions are not set up, use `sudo` for a quick test:

```bash
sudo openocd -f /home/adrian/doc/nfp/merlin/MINIC_20240710/SDK/zephyr/boards/riscv/hifive_unmatched/support/openocd_hifive_unmatched.cfg
```

OpenOCD exposes the usual ports:

```text
3333  GDB remote
4444  telnet command console
6666  TCL
```

### GDB attach

The current host has `gdb-multiarch` available. Attach without an ELF:

```bash
gdb-multiarch
```

Then in GDB:

```gdb
set architecture riscv:rv64
target extended-remote :3333
monitor targets
monitor halt
info registers
```

When debugging a specific stage, pass the matching ELF to GDB:

```bash
gdb-multiarch /path/to/u-boot
gdb-multiarch /path/to/fw_dynamic.elf
gdb-multiarch /path/to/vmlinux
```

Typical commands:

```gdb
target extended-remote :3333
monitor reset halt
b main
c
```

### Telnet control

Open a telnet session:

```bash
telnet localhost 4444
```

Useful OpenOCD commands:

```text
scan_chain
targets
halt
resume
reset halt
```

### Early boot halt

For very early boot debugging, set FU740 `MSEL[3:0] = 0000`. The FU740 manual
defines this as the mode that loops forever waiting for a debugger. This is
useful when debugging ROM/SPL handoff before normal firmware gets far enough
for breakpoints to be reliable.

For normal SD or SPI flash boot debugging, keep the board in the desired boot
mode and attach after reset.

### Troubleshooting

- If `openocd` is missing, install or build an OpenOCD with RISC-V target
  support. Some older distro packages do not include the RISC-V target.
- If OpenOCD cannot find the FTDI device, check `lsusb -d 0403:6010`.
- If access is denied, use `sudo` temporarily or add a udev rule for
  `0403:6010`.
- If the JTAG chain is unstable, lower `adapter speed 10000` in the config to
  `adapter speed 1000`.
- If OpenOCD reports that the `riscv` target type is unknown, the OpenOCD build
  is too old or lacks RISC-V support.
