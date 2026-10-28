# coremark

EEMBC CoreMark 1.0 benchmark, ported from the reference project at
`mp157-dk1-prj/apps/coremark` (STM32MP157 DK1) to the PocketBeagle.
Upstream CoreMark sources + the `linux` port (`linux/core_portme.*`,
POSIX timers, heap memory) are used unchanged; the port work is the build
configuration: Cortex-A8 target and this repository's board/transfer
workflow. See `LICENSE.md` (Apache 2.0).

Like the reference, two instruction-set variants are built:

- `coremark-arm`   : compiled with `-marm`  (A32 instruction set)
- `coremark-thumb` : compiled with `-mthumb` (T32/Thumb-2 instruction set)

Optimization: `-O3 -funroll-loops -flto` on top of
`-mcpu=cortex-a8 -mfpu=neon -mfloat-abi=hard`. Runs use `PERFORMANCE_RUN=1`,
`SEED_METHOD=SEED_VOLATILE`, `MEM_METHOD=MEM_MALLOC` (heap), compile-time
pinned `ITERATIONS` (default 40000, >= 10 s runtime for valid scores; the
`linux` port's default `SEED_ARG` would ignore `-DITERATIONS` and fall back
to auto-calibration, hence `SEED_VOLATILE`).

## Build and run

```
make                 # or: make ITERATIONS=40000
make push run        # rsync both binaries to pocket:~/coremark and run both
```

From PowerShell (msys rsync needs msys ssh transport):

```
make push run CROSS=D:/arm-none-linux-gnu-15.3.rel2/bin/arm-none-linux-gnueabihf- ^
  RSYNC_SSH="C:/msys64/usr/bin/ssh.exe -F C:/Users/<you>/.ssh/config"
```

`make size` prints the text/data/bss sizes. `make deploy build-remote`
builds natively on the board instead (board gcc 14 supports `-marm`/`-mthumb`
too).

## Results — PocketBeagle (AM3358, single Cortex-A8 @ 1 GHz)

Clock verified during the run: `scaling_cur_freq` = 1000000 kHz,
governor `performance`. Validated runs, 40000 iterations each (both
CRC-checked correct):

| Binary         | ISA (our units) | CoreMark/s   | CoreMark/MHz | text   | data | bss |
|----------------|-----------------|--------------|--------------|--------|------|-----|
| coremark-arm   | ARM (A32)       | 3067.014262  | 3.07         | 25 926 | 328  | 32  |
| coremark-thumb | Thumb-2 (T32)   | 3106.554831  | 3.11         | 20 990 | 328  | 32  |

Observations:

- Thumb-2 is ~1.3 % faster and ~19 % denser - same direction as the
  reference board (Cortex-A7, ~0.5 % faster): Thumb-2 gives full performance
  with higher code density on armv7-A.
- ~3067-3107 CoreMark/s @ 1 GHz = 3.07-3.11 CoreMark/MHz, in line with
  published Cortex-A8 figures.
- Pure single-ISA binaries are not feasible: glibc/crt objects carry their
  own ISA attributes (mixed `Tag_ARM_ISA_use` + `Tag_THUMB_ISA_use` in both
  final ELFs), same as the reference project. The build flags guarantee the
  ISA of all project compilation units, which dominate the hot path; with
  LTO the `core_*` functions are inlined/internalized anyway.
