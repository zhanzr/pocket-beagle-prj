# Applications

User-space applications for the PocketBeagle. Each subdirectory is one
self-contained project with its own `Makefile` and `README.md`.

| Project | Description |
|---|---|
| `blink_hello/` | Hello world + USR3 LED blinker + ADC/temperature sampling |

## Common workflow (per project, from the project directory)

Fast loop during development - cross-build on the host and push only the
binary (no source transfer, board-side `gcc` not involved):

```
make push run
```

This requires an ARM cross toolchain; `CROSS` defaults to the
`arm-none-linux-gnueabihf-` prefix (expected on PATH) and can be overridden:

```
make push run CROSS=/path/to/toolchain/bin/arm-none-linux-gnueabihf-
```

Full source deploy (project sources scp'd to the board, built on-board with
gcc):

```
make deploy        # scp sources to pocket:~/<project>
make build-remote  # make on the board
make run
```

Board-side paths default to `BOARD=pocket`, `BOARD_DIR=<project>` (under the
home directory); override with `make ... BOARD=pocket BOARD_DIR=whatever`.

Cross flags: `-march=armv7-a -mfpu=neon -mfloat-abi=hard`. See the
repository root `README.md` for toolchain details, the board connection, and
PRU notes.
