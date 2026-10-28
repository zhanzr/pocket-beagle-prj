# Applications

User-space applications for the PocketBeagle. Each subdirectory is one
self-contained project with its own `Makefile` and `README.md`.

| Project | Description |
|---|---|
| `blink_hello/` | Hello world + USR3 LED blinker |

## Common workflow (per project, from the project directory)

```
make deploy        # scp sources to pocket:~/<project>
make build-remote  # gcc on the board
make run           # run on the board via ssh
```

Deploy target names default to `BOARD=pocket`, `BOARD_DIR=<project>`
(board-side path under the home directory); override with
`make deploy BOARD=pocket BOARD_DIR=whatever`.

Cross build from Windows is available where the Makefile provides a `cross`
target (uses `D:\arm-none-linux-gnu-15.3.rel2` toolchain, flags
`-march=armv7-a -mfpu=neon -mfloat-abi=hard`).

See the repository root `README.md` for toolchain details, the board
connection, and PRU notes.
