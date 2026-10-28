# blink_hello

First project for the PocketBeagle. Pure Cortex-A8, no PRU involved.

## Behavior

1. Prints `hello world` on stdout.
2. Blinks the on-board **USR3 LED** at 1 Hz (0.5 s on / 0.5 s off).
   USR0-USR2 are left to the system (heartbeat / mmc / activity).
3. Every blink cycle samples the board's 8-channel ADC (AM335x AIN0-AIN7 via
   IIO: raw counts + millivolts) and prints one line per blink. Unconnected
   AIN pins float - values are noise, that is expected.
4. Every blink cycle tries to read a die/CPU temperature (thermal zone or
   hwmon). The stock image does not expose the AM335x die sensor, so it
   prints `die temp: not exposed`; if a kernel/dtb update ever provides
   `/sys/class/thermal/thermal_zone*` or hwmon temp, it is picked up
   automatically with no code change.
5. On Ctrl-C (SIGINT) or SIGTERM it restores USR3's previous trigger and
   prints `bye`.

No sudo is needed: LED and ADC sysfs files are `root:gpio` mode 664 and the
`debian` user is in the `gpio` group.

Sample output:

```
hello world
adc: /sys/bus/iio/devices/iio:device0 (8 ch)
blink 1: adc raw: 2182 1908 4095 4095 4095 4095 4095 4095 mv: 958 838 1798 1798 1798 1798 1798 1798 die temp: not exposed
blink 2: adc raw: 2191 1897 ...
...
bye
```

## Build and run

Fast development loop (cross-build here, push only the binary):

```
make push run      # build on the host with an ARM cross gcc, scp binary, run
```

`CROSS` defaults to `arm-none-linux-gnueabihf-` (prefix expected on PATH);
override with your toolchain location, e.g.:

```
make push run CROSS=D:/arm-none-linux-gnu-15.3.rel2/bin/arm-none-linux-gnueabihf-
```

Full source deploy (build on the board itself):

```
make deploy        # scp sources to pocket:~/blink_hello
make build-remote  # gcc on the board
make run
```

or manually: `ssh pocket`, `cd blink_hello`, `make`, `./blink_hello`.
