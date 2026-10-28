# ssd1317_md066_64x48_iic

SSD1317 "MD066" 0.66" 64x48 mono OLED demo for the PocketBeagle, ported
from the reference projects `mp157-dk1-prj/apps/ssd1317_md066_64x48_iic`
(MP157 DK1, Linux user-space architecture) and
`at32-start-f407/f425-start/bare/ssd1317_md066_64x48_iic` (AT32F425, driver
logic). Ported from the Zhongjingyuan vendor example (STM32F103RC_IIC).

Same architecture as the MP157 reference - **no library dependencies**,
only kernel UAPI headers (`<linux/i2c-dev.h>`, `<linux/gpio.h>`): the
module is driven via i2c-dev, RES via the GPIO char-device, found by its
line name (this image names GPIO lines after the header silkscreen).

## Wiring (3.3 V module)

| Module | PocketBeagle | Resource |
|---|---|---|
| GND | P1.16 | GND |
| VCC | P1.14 | VOUT-3.3V |
| SCL | P1.28 | I2C2_SCL -> `/dev/i2c-2` |
| SDA | P1.26 | I2C2_SDA |
| RES | P2.22 | GPIO1_14, gpiod line `"P2.22"`, active low |

Module 7-bit I2C address 0x3C (0x78 in 8-bit write notation).

**Gotcha:** the module does not ACK while RES is floating (the boot-default
input state holds the SSD1317 in reset). The demo requests RES output-high
before probing; if you scan the bus with the module freshly wired and get
nothing, drive RES high first (e.g. `gpioset --daemonize P2.22=1`).

## Behavior

Demo loop (repeats until Ctrl-C, capped at 30 s per run):
banner/info -> inverted info -> all-on -> all-off -> 8-px stripes ->
4-px stripes -> checkerboard -> vertical bars -> border -> gradient ->
animated bar sweep with console FPS report -> `bye` on exit.

Measured: ~70 fps on the bar-sweep pass (i2c-2 at its default speed,
6 x 65-byte transactions per frame).

## Build and run

```
make push run       # cross-build, push binary (rsync), run
make deploy build-remote   # or build natively on the board
```

From PowerShell add `CROSS=D:/.../arm-none-linux-gnueabihf-` (see
`app/README.md` for the rsync/transport notes).

Note: an interactive `gpioset --daemonize P2.22=...` left running holds the
line and makes the demo fail with "RES line busy" - `pkill gpioset` first.
