#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>

#define LED_DIR       "/sys/class/leds/beaglebone:green:usr3"
#define IIO_BASE      "/sys/bus/iio/devices"
#define ADC_CHANNELS  8
#define ADC_MV_MAX    1800
#define ADC_STEPS     4096
#define BLINK_US      500000

static volatile sig_atomic_t g_run = 1;

static void on_signal(int sig)
{
    (void)sig;
    g_run = 0;
}

static int read_file(const char *path, char *buf, size_t size)
{
    int fd = open(path, O_RDONLY);
    ssize_t n;

    if (fd < 0) {
        return -1;
    }
    n = read(fd, buf, size - 1);
    close(fd);
    if (n < 0) {
        return -1;
    }
    buf[n] = '\0';
    return 0;
}

static int read_int_file(const char *path, long *out)
{
    char buf[64];

    if (read_file(path, buf, sizeof(buf)) != 0) {
        return -1;
    }
    *out = strtol(buf, NULL, 10);
    return 0;
}

static int write_file(const char *path, const char *value)
{
    int fd = open(path, O_WRONLY);
    ssize_t n;

    if (fd < 0) {
        return -1;
    }
    n = write(fd, value, strlen(value));
    close(fd);
    return (n < 0) ? -1 : 0;
}

static int read_active_trigger(char *buf, size_t size)
{
    char raw[512];
    char *start, *end;
    int fd = open(LED_DIR "/trigger", O_RDONLY);
    ssize_t n;

    if (fd < 0) {
        return -1;
    }
    n = read(fd, raw, sizeof(raw) - 1);
    close(fd);
    if (n <= 0) {
        return -1;
    }
    raw[n] = '\0';
    start = strchr(raw, '[');
    end = (start != NULL) ? strchr(start, ']') : NULL;
    if (start == NULL || end == NULL) {
        return -1;
    }
    *end = '\0';
    start++;
    strncpy(buf, start, size - 1);
    buf[size - 1] = '\0';
    return 0;
}

static void find_adc_dir(char *dst, size_t size)
{
    char dir[192], probe[256];
    int i;

    dst[0] = '\0';
    for (i = 0; i < 16; i++) {
        snprintf(dir, sizeof(dir), IIO_BASE "/iio:device%d", i);
        snprintf(probe, sizeof(probe), "%s/in_voltage0_raw", dir);
        if (access(probe, R_OK) == 0) {
            snprintf(dst, size, "%s", dir);
            return;
        }
    }
}

static int read_adc(const char *adc_dir, long raw[ADC_CHANNELS])
{
    char path[320];
    int ch;

    for (ch = 0; ch < ADC_CHANNELS; ch++) {
        snprintf(path, sizeof(path), "%s/in_voltage%d_raw", adc_dir, ch);
        if (read_int_file(path, &raw[ch]) != 0) {
            return -1;
        }
    }
    return 0;
}

static int read_die_temp_mc(long *out_mc)
{
    char path[128];
    int i;

    for (i = 0; i < 8; i++) {
        snprintf(path, sizeof(path), "/sys/class/thermal/thermal_zone%d/temp", i);
        if (access(path, R_OK) == 0 && read_int_file(path, out_mc) == 0) {
            return 0;
        }
    }
    for (i = 0; i < 8; i++) {
        snprintf(path, sizeof(path), "/sys/class/hwmon/hwmon%d/temp1_input", i);
        if (access(path, R_OK) == 0 && read_int_file(path, out_mc) == 0) {
            return 0;
        }
    }
    return -1;
}

int main(void)
{
    char old_trigger[128] = "none";
    char adc_dir[192];
    long raw[ADC_CHANNELS];
    long temp_mc = 0;
    int have_trigger = read_active_trigger(old_trigger, sizeof(old_trigger)) == 0;
    int adc_ok;
    unsigned long cycles = 0;
    int ch;

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    printf("hello world\n");

    find_adc_dir(adc_dir, sizeof(adc_dir));
    adc_ok = (adc_dir[0] != '\0');
    printf("adc: %s (%d ch)\n", adc_ok ? adc_dir : "not found", ADC_CHANNELS);

    if (have_trigger && strcmp(old_trigger, "none") != 0) {
        if (write_file(LED_DIR "/trigger", "none") != 0) {
            fprintf(stderr, "warn: cannot detach trigger '%s' "
                            "(no write access), blinking may be overridden\n",
                    old_trigger);
        }
    }

    while (g_run) {
        if (write_file(LED_DIR "/brightness", "1") != 0) {
            perror(LED_DIR "/brightness");
            fprintf(stderr, "hint: the debian user must be in the 'gpio' "
                            "group (or use sudo)\n");
            return 1;
        }

        cycles++;
        printf("blink %lu:", cycles);
        if (adc_ok && read_adc(adc_dir, raw) == 0) {
            printf(" adc raw:");
            for (ch = 0; ch < ADC_CHANNELS; ch++) {
                printf(" %ld", raw[ch]);
            }
            printf(" mv:");
            for (ch = 0; ch < ADC_CHANNELS; ch++) {
                printf(" %ld", raw[ch] * ADC_MV_MAX / ADC_STEPS);
            }
        } else {
            printf(" adc: read failed");
        }
        if (read_die_temp_mc(&temp_mc) == 0) {
            printf(" die %ld.%ld C", temp_mc / 1000,
                   (labs(temp_mc) % 1000) / 100);
        } else {
            printf(" die temp: not exposed");
        }
        printf("\n");
        fflush(stdout);

        usleep(BLINK_US);
        if (!g_run) {
            break;
        }
        if (write_file(LED_DIR "/brightness", "0") != 0) {
            perror(LED_DIR "/brightness");
            return 1;
        }
        usleep(BLINK_US);
    }

    if (have_trigger && strcmp(old_trigger, "none") != 0) {
        write_file(LED_DIR "/trigger", old_trigger);
    }
    printf("bye\n");
    return 0;
}
