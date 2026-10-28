# jd9851_md140_240x240_cst816d

JD9851 1.4" 240x240 IPS + CST816D capacitive touch, 4-line SPI + I2C,
ported from the nano-f411 reference
(`f4-demo/nano-f411/app/jd9851_md140_240x240_cst816d`, vendor-verbatim init
from `module-tiky-lcm/TK014F1828_1246/STM32_LCD_TK014F1828_1246_hardSPI_-
hal_captouch`). Same Linux user-space architecture as the ssd1317 project:
i2c-dev for touch, GPIO char-device uAPI for DC, spidev for the panel - no
library dependencies.

## Wiring (3.3 V module)

| Module | PocketBeagle | Resource |
|---|---|---|
| VDD | P1.14 | VOUT-3.3V |
| GND | P1.16 | GND |
| SCK | P1.08 | SPI0_CLK -> `/dev/spidev0.0` |
| MOSI | P1.12 | SPI0_MOSI |
| CS | P1.06 | SPI0_CS0 (hardware, per-transfer) |
| DC | P2.22 | GPIO1_14, gpiod line `"P2.22"` (0 = cmd, 1 = data) |
| TOUCH_SCL | P1.28 | I2C2_SCL -> `/dev/i2c-2`, 7-bit addr 0x15 |
| TOUCH_SDA | P1.26 | I2C2_SDA |

No reset and no backlight pin on this module (backlight hardwired). The
panel has no reset pin either - **a latched bad state survives process
restarts; power-cycle the module to recover**.

## Board setup (one-time, already applied)

1. `/boot/uEnv.txt`: `uboot_overlay_addr0=BB-SPIDEV0-00A0.dtbo` + reboot
   -> creates `/dev/spidev0.0` (SPI0: P1.06/P1.08/P1.10/P1.12).
2. `/dev/spidev*` are root-only (0600): either run with `sudo`, or allow
   the gpio group once:

```
ssh -t pocket "echo 'KERNEL==\"spidev*\", MODE=\"0660\", GROUP=\"gpio\"' | sudo tee /etc/udev/rules.d/99-spidev.rules && sudo udevadm trigger"
```

## Behavior

Demo loop until Ctrl-C (60 s cap): banner -> TEST_STAND (frame, gray
ramp, color bands, five timed solid fills) -> info page (normal +
inverted, shows measured fill times) -> animated HSV gradient. Touch
down/release + X/Y print on the console; touch can be tested any time the
info/banner pages are up.

Measured: full-frame solid fill ~94 ms at 16 MHz (~10 fps; the spidev
`bufsiz` limit of 4096 B caps one transfer - writes are chunked
accordingly; raising `spidev.bufsiz` boots would allow bigger bursts).

## Build and run

```
make push run       # cross-build, push, run (needs sudo for spidev)
make deploy build-remote
```

From PowerShell add `CROSS=D:/.../arm-none-linux-gnueabihf-` (see
`app/README.md`).
