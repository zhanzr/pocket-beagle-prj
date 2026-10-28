# Applications

User-space applications for the PocketBeagle. Each subdirectory is one
self-contained project with its own `Makefile` and `README.md`.

| Project | Description |
|---|---|
| `blink_hello/` | Hello world + USR3 LED blinker + ADC/temperature sampling |
| `coremark/` | EEMBC CoreMark 1.0, ARM and Thumb-2 variants, ported from the MP157 DK1 reference project |
| `ssd1317_md066_64x48_iic/` | SSD1317 64x48 I2C OLED demo (i2c-dev + GPIO uAPI, no libs), ported from the MP157/AT32 references |
| `jd9851_md140_240x240_cst816d/` | JD9851 240x240 SPI TFT + CST816D touch (spidev + i2c-dev + GPIO uAPI), ported from the nano-f411 reference |

## Common workflow (per project, from the project directory)

Fast loop during development - cross-build on the host and push only the
binary (no source transfer, board-side `gcc` not involved):

```
make push run
```

Transfers use **rsync when available** (incremental - unchanged files are
not re-sent), falling back to `scp` otherwise. Requirements:

- board has rsync (3.5.0, preinstalled) and a clean non-interactive ssh
  stdout (the login banner had to be disabled once in `/etc/pam.d/sshd`,
  see root README);
- host: from an MSYS environment plain `rsync`/`ssh` work as-is. From
  PowerShell, msys rsync cannot use Windows OpenSSH as transport - either
  work inside MSYS or point the transport at msys ssh:

```
make push run RSYNC_SSH="C:/msys64/usr/bin/ssh.exe -F C:/Users/<you>/.ssh/config"
```

`CROSS` defaults to the `arm-none-linux-gnueabihf-` prefix (expected on
PATH) and can be overridden:

```
make push run CROSS=/path/to/toolchain/bin/arm-none-linux-gnueabihf-
```

Full source deploy (project sources transferred to the board, built
on-board with gcc):

```
make deploy        # rsync/scp sources to pocket:~/<project>
make build-remote  # make on the board
make run
```

Board-side paths default to `BOARD=pocket`, `BOARD_DIR=<project>` (under the
home directory); override with `make ... BOARD=pocket BOARD_DIR=whatever`.

Cross flags: `-march=armv7-a -mfpu=neon -mfloat-abi=hard`. See the
repository root `README.md` for toolchain details, the board connection, and
PRU notes.
