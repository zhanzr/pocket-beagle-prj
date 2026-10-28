# blink_hello

First project for the PocketBeagle. Pure Cortex-A8, no PRU involved.

## Behavior

1. Prints `hello world` on stdout.
2. Blinks the on-board **USR3 LED** at 1 Hz (0.5 s on / 0.5 s off).
   USR0-USR2 are left to the system (heartbeat / mmc / activity).
3. On Ctrl-C (SIGINT) or SIGTERM it restores USR3's previous trigger and
   prints `bye`.

LED controlled via the kernel LED class:
`/sys/class/leds/beaglebone:green:usr3/{trigger,brightness}`. On this image
both files are `root:gpio` mode 664 and the `debian` user is in the `gpio`
group, so **no sudo is needed**.

## Build and run on the board

```
make deploy        # scp sources to pocket:~/blink_hello
make build-remote  # gcc on the board
make run           # ./blink_hello
```

or manually:

```
ssh pocket
cd blink_hello
make
sudo ./blink_hello
```

## Cross build from Windows (optional)

```
make cross   # uses D:\arm-none-linux-gnu-15.3.rel2 toolchain
scp blink_hello pocket:blink_hello/
ssh pocket "./blink_hello/blink_hello"
```
